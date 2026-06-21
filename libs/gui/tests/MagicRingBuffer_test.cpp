#include <fcntl.h>
#include <gtest/gtest.h>
#include <gui/MagicRingBuffer.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstring>

namespace android {

static int memfd_create_fallback(const char* name, unsigned int flags) {
    return syscall(SYS_memfd_create, name, flags);
}

class MagicRingBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        shm_fd = memfd_create_fallback("magic_ring_buffer_test", MFD_CLOEXEC);
        ASSERT_GE(shm_fd, 0);

        constexpr size_t size = sizeof(RingBuffer);
        ASSERT_EQ(0, ftruncate(shm_fd, size));

        void* mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        ASSERT_NE(MAP_FAILED, mem);

        ring_buffer = new (mem) RingBuffer();
        ASSERT_TRUE(ring_buffer->initMagicMapping(shm_fd, 0));
    }

    void TearDown() override {
        if (ring_buffer) {
            munmap(ring_buffer, sizeof(RingBuffer));
        }
        if (shm_fd >= 0) {
            close(shm_fd);
        }
    }

    struct TestEntry : public MagicRingBufferEntry {
        uint32_t data;
    };

    static constexpr int64_t BUFFER_SIZE = 16384;
    using RingBuffer = MagicRingBuffer<BUFFER_SIZE>;

    int shm_fd = -1;
    RingBuffer* ring_buffer = nullptr;
};

TEST_F(MagicRingBufferTest, BasicReserveCommitPeekPop) {
    auto* entry = ring_buffer->reserve<TestEntry>();
    ASSERT_NE(nullptr, entry);
    entry->data = 42;
    ring_buffer->commit();

    auto* peeked = static_cast<TestEntry*>(ring_buffer->peek());
    ASSERT_NE(nullptr, peeked);
    EXPECT_EQ(42u, peeked->data);
    EXPECT_EQ(sizeof(TestEntry), peeked->size);

    ring_buffer->pop();
    EXPECT_EQ(nullptr, ring_buffer->peek());
}

TEST_F(MagicRingBufferTest, ReserveAppendCommit) {
    struct ComplexEntry : public MagicRingBufferEntry {
        uint32_t type;
        uint32_t* array1;
        uint64_t* array2;
    };

    auto* entry = ring_buffer->reserve<ComplexEntry>();
    ASSERT_NE(nullptr, entry);
    entry->type = 123;
    entry->array1 = ring_buffer->append<uint32_t>(2);
    ASSERT_NE(nullptr, entry->array1);
    entry->array1[0] = 10;
    entry->array1[1] = 20;

    entry->array2 = ring_buffer->append<uint64_t>(1);
    ASSERT_NE(nullptr, entry->array2);
    entry->array2[0] = 30000000000ull;

    ring_buffer->commit();

    auto* peeked = static_cast<ComplexEntry*>(ring_buffer->peek());
    ASSERT_NE(nullptr, peeked);
    EXPECT_EQ(123u, peeked->type);

    // Convert pointers relative to the base if they are absolute in the struct
    // Wait, the pointers are absolute addresses in the mapped memory, so they remain valid.
    EXPECT_EQ(10u, peeked->array1[0]);
    EXPECT_EQ(20u, peeked->array1[1]);
    EXPECT_EQ(30000000000ull, peeked->array2[0]);

    // The size of the entry should account for the allocations
    uint32_t expected_size = sizeof(ComplexEntry);
    expected_size = RingBuffer::align8(expected_size) + 2 * sizeof(uint32_t);
    expected_size = RingBuffer::align8(expected_size) + 1 * sizeof(uint64_t);
    EXPECT_EQ(expected_size, peeked->size);

    ring_buffer->pop();
}

TEST_F(MagicRingBufferTest, WrapAround) {
    // Fill the buffer almost to the end
    size_t entry_size = RingBuffer::align8(sizeof(TestEntry));
    size_t num_entries = BUFFER_SIZE / entry_size;

    for (size_t i = 0; i < num_entries - 1; ++i) {
        auto* entry = ring_buffer->reserve<TestEntry>();
        ASSERT_NE(nullptr, entry);
        entry->data = i;
        ring_buffer->commit();
    }

    // Buffer is almost full, reserve should fail if we try to reserve more than available
    for (size_t i = 0; i < num_entries - 1; ++i) {
        auto* peeked = static_cast<TestEntry*>(ring_buffer->peek());
        ASSERT_NE(nullptr, peeked);
        EXPECT_EQ(i, peeked->data);
        ring_buffer->pop();
    }

    // Now lo and hi are near the end of the buffer.
    // Reserve an entry that will wrap around into the aliased memory region.
    auto* entry1 = ring_buffer->reserve<TestEntry>();
    ASSERT_NE(nullptr, entry1);
    entry1->data = 99;
    ring_buffer->commit();

    auto* entry2 = ring_buffer->reserve<TestEntry>();
    ASSERT_NE(nullptr, entry2);
    entry2->data = 100;
    ring_buffer->commit();

    auto* peeked1 = static_cast<TestEntry*>(ring_buffer->peek());
    ASSERT_NE(nullptr, peeked1);
    EXPECT_EQ(99u, peeked1->data);
    ring_buffer->pop();

    auto* peeked2 = static_cast<TestEntry*>(ring_buffer->peek());
    ASSERT_NE(nullptr, peeked2);
    EXPECT_EQ(100u, peeked2->data);
    ring_buffer->pop();
}

TEST_F(MagicRingBufferTest, FullBuffer) {
    size_t entry_size = RingBuffer::align8(sizeof(TestEntry));
    size_t num_entries = BUFFER_SIZE / entry_size;

    for (size_t i = 0; i < num_entries; ++i) {
        auto* entry = ring_buffer->reserve<TestEntry>();
        ASSERT_NE(nullptr, entry) << "Failed at index " << i;
        entry->data = i;
        ring_buffer->commit();
    }

    // Buffer should be full
    EXPECT_EQ(nullptr, ring_buffer->reserve<TestEntry>());

    // Pop one, should be able to reserve one
    ring_buffer->pop();
    auto* entry = ring_buffer->reserve<TestEntry>();
    ASSERT_NE(nullptr, entry);
    entry->data = 999;
    ring_buffer->commit();
}

} // namespace android