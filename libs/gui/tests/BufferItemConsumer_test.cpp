/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "BufferItemConsumer_test"
//#define LOG_NDEBUG 0

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <gui/BufferItemConsumer.h>
#include <gui/IGraphicBufferConsumer.h>
#include <gui/IProducerListener.h>
#include <gui/Surface.h>
#include <system/window.h>
#include <ui/BufferQueueDefs.h>
#include <ui/GraphicBuffer.h>
#include <utils/Errors.h>

#include <unordered_set>

namespace android {

static constexpr int kWidth = 100;
static constexpr int kHeight = 100;
static constexpr int kMaxLockedBuffers = 3;
static constexpr int kFormat = HAL_PIXEL_FORMAT_RGBA_8888;
static constexpr int kUsage = GRALLOC_USAGE_SW_READ_RARELY;
static constexpr int kFrameSleepUs = 30 * 1000;

class BufferItemConsumerTest : public ::testing::Test {
   protected:
    struct BufferFreedListener
        : public BufferItemConsumer::BufferFreedListener {
        explicit BufferFreedListener(BufferItemConsumerTest* test)
            : mTest(test) {}
        void onBufferFreed(const sp<GraphicBuffer>& /* gBuffer */) override {
            mTest->HandleBufferFreed();
        }
        BufferItemConsumerTest* mTest;
    };

    struct TrackingProducerListener : public BnProducerListener {
        TrackingProducerListener(BufferItemConsumerTest* test) : mTest(test) {}

        virtual void onBufferReleased() override {}
        virtual bool needsReleaseNotify() override { return true; }
        virtual void onBuffersDiscarded(const std::vector<int32_t>&) override {}
        virtual void onBufferDetached(int slot, uint64_t bufferId) override {
            mTest->HandleBufferDetached(slot, bufferId);
        }

        BufferItemConsumerTest* mTest;
    };

    void SetUp() override {
        mBuffers.resize(BufferQueueDefs::NUM_BUFFER_SLOTS);

        sp<Surface> surface;
        std::tie(mBIC, surface) = BufferItemConsumer::create(kUsage, kMaxLockedBuffers, true);
        String8 name("BufferItemConsumer_Under_Test");
        mBIC->setName(name);
        mBFL = new BufferFreedListener(this);
        mBIC->setBufferFreedListener(mBFL);

        sp<IProducerListener> producerListener = new TrackingProducerListener(this);
        mProducer = surface->getIGraphicBufferProducer();
        IGraphicBufferProducer::QueueBufferOutput bufferOutput;
        ASSERT_EQ(NO_ERROR,
                  mProducer->connect(producerListener, NATIVE_WINDOW_API_CPU,
                                     true, &bufferOutput));
        ASSERT_EQ(NO_ERROR,
                  mProducer->setMaxDequeuedBufferCount(kMaxLockedBuffers));
    }

    int GetFreedBufferCount() {
        std::lock_guard<std::mutex> lock(mMutex);
        return mFreedBufferCount;
    }

    void HandleBufferFreed() {
        std::lock_guard<std::mutex> lock(mMutex);
        mFreedBufferCount++;
        ALOGD("HandleBufferFreed, mFreedBufferCount=%d", mFreedBufferCount);
    }

    void HandleBufferDetached(int slot, uint64_t bufferId) {
        std::lock_guard<std::mutex> lock(mMutex);
        mDetachedBufferSlots.push_back(slot);
        mDetachedBufferIds.push_back(bufferId);
        ALOGD("HandleBufferDetached, slot=%d bufferId=%" PRIu64 " mDetachedBufferSlots-count=%zu",
              slot, bufferId, mDetachedBufferSlots.size());
    }

    void DequeueBuffer(int* outSlot) {
        ASSERT_NE(outSlot, nullptr);

        int slot;
        sp<Fence> outFence;
        status_t ret = mProducer->dequeueBuffer(&slot, &outFence, kWidth, kHeight, 0, 0,
                                                nullptr, nullptr);
        ASSERT_GE(ret, 0);

        ALOGD("dequeueBuffer: slot=%d", slot);
        if (ret & IGraphicBufferProducer::BUFFER_NEEDS_REALLOCATION) {
            ret = mProducer->requestBuffer(slot, &mBuffers[slot]);
            ASSERT_EQ(NO_ERROR, ret);
        }
        *outSlot = slot;
    }

    void QueueBuffer(int slot) {
        ALOGD("enqueueBuffer: slot=%d", slot);
        IGraphicBufferProducer::QueueBufferInput bufferInput(
            0ULL, true, HAL_DATASPACE_UNKNOWN, Rect::INVALID_RECT,
            NATIVE_WINDOW_SCALING_MODE_FREEZE, 0, Fence::NO_FENCE);
        IGraphicBufferProducer::QueueBufferOutput bufferOutput;
        status_t ret = mProducer->queueBuffer(slot, bufferInput, &bufferOutput);
        ASSERT_EQ(NO_ERROR, ret);
    }

    void AcquireBuffer(int* outSlot) {
        ASSERT_NE(outSlot, nullptr);
        BufferItem buffer;
        status_t ret = mBIC->acquireBuffer(&buffer, 0, false);
        ASSERT_EQ(NO_ERROR, ret);

        ALOGD("acquireBuffer: slot=%d", buffer.mSlot);
        *outSlot = buffer.mSlot;
    }

    void ReleaseBuffer(int slot) {
        ALOGD("releaseBuffer: slot=%d", slot);
        BufferItem buffer;
        buffer.mSlot = slot;
        buffer.mGraphicBuffer = mBuffers[slot];
        status_t ret = mBIC->releaseBuffer(buffer, Fence::NO_FENCE);
        ASSERT_EQ(NO_ERROR, ret);
    }

    void DetachBuffer(int slot) {
        ALOGD("detachBuffer: slot=%d", slot);
        status_t ret = mBIC->detachBuffer(mBuffers[slot]);
        ASSERT_EQ(NO_ERROR, ret);
    }

    std::mutex mMutex;
    int mFreedBufferCount{0};
    std::vector<int> mDetachedBufferSlots = {};
    std::vector<uint64_t> mDetachedBufferIds = {};

    sp<BufferItemConsumer> mBIC;
    sp<BufferFreedListener> mBFL;
    sp<IGraphicBufferProducer> mProducer;
    sp<IGraphicBufferConsumer> mConsumer;
    std::vector<sp<GraphicBuffer>> mBuffers;
};

// Test that detaching buffer from consumer side triggers onBufferFreed.
TEST_F(BufferItemConsumerTest, TriggerBufferFreed_DetachBufferFromConsumer) {
    int slot;
    // Producer: generate a placeholder buffer.
    DequeueBuffer(&slot);
    QueueBuffer(slot);

    ASSERT_EQ(0, GetFreedBufferCount());
    // Consumer: acquire the buffer and then detach it.
    AcquireBuffer(&slot);
    status_t ret = mBIC->detachBuffer(slot);
    ASSERT_EQ(NO_ERROR, ret);

    // Sleep to give some time for callbacks to happen.
    usleep(kFrameSleepUs);
    ASSERT_EQ(1, GetFreedBufferCount());
}

// Test that detaching buffer from producer side triggers onBufferFreed.
TEST_F(BufferItemConsumerTest, TriggerBufferFreed_DetachBufferFromProducer) {
    int slot;
    // Let buffer go through the cycle at least once.
    DequeueBuffer(&slot);
    QueueBuffer(slot);
    AcquireBuffer(&slot);
    ReleaseBuffer(slot);

    ASSERT_EQ(0, GetFreedBufferCount());

    // Producer: generate the buffer again.
    DequeueBuffer(&slot);

    // Producer: detach the buffer.
    status_t ret = mProducer->detachBuffer(slot);
    ASSERT_EQ(NO_ERROR, ret);

    // Sleep to give some time for callbacks to happen.
    usleep(kFrameSleepUs);
    ASSERT_EQ(1, GetFreedBufferCount());
}

// Test that abandoning BufferItemConsumer triggers onBufferFreed.
TEST_F(BufferItemConsumerTest, TriggerBufferFreed_AbandonBufferItemConsumer) {
    int slot;
    // Let buffer go through the cycle at least once.
    DequeueBuffer(&slot);
    QueueBuffer(slot);
    AcquireBuffer(&slot);
    ReleaseBuffer(slot);

    // Abandon the BufferItemConsumer.
    mBIC->abandon();

    // Sleep to give some time for callbacks to happen.
    usleep(kFrameSleepUs);
    ASSERT_EQ(1, GetFreedBufferCount());
}

// Test that delete BufferItemConsumer triggers onBufferFreed.
TEST_F(BufferItemConsumerTest, TriggerBufferFreed_DeleteBufferItemConsumer) {
    int slot;
    // Let buffer go through the cycle at least once.
    DequeueBuffer(&slot);
    QueueBuffer(slot);
    AcquireBuffer(&slot);
    ReleaseBuffer(slot);

    // Delete the BufferItemConsumer.
    mBIC.clear();

    // Sleep to give some time for callbacks to happen.
    usleep(kFrameSleepUs);
    ASSERT_EQ(1, GetFreedBufferCount());
}

TEST_F(BufferItemConsumerTest, ResizeAcquireCount) {
    EXPECT_EQ(OK, mBIC->setMaxAcquiredBufferCount(kMaxLockedBuffers + 1));
    EXPECT_EQ(OK, mBIC->setMaxAcquiredBufferCount(kMaxLockedBuffers + 2));
    EXPECT_EQ(OK, mBIC->setMaxAcquiredBufferCount(kMaxLockedBuffers - 1));
    EXPECT_EQ(OK, mBIC->setMaxAcquiredBufferCount(kMaxLockedBuffers - 2));
    EXPECT_EQ(OK, mBIC->setMaxAcquiredBufferCount(kMaxLockedBuffers + 1));
    EXPECT_EQ(OK, mBIC->setMaxAcquiredBufferCount(kMaxLockedBuffers - 1));
}

TEST_F(BufferItemConsumerTest, AttachBuffer) {
    ASSERT_EQ(OK, mBIC->setMaxAcquiredBufferCount(1));

    int slot;
    DequeueBuffer(&slot);
    QueueBuffer(slot);
    AcquireBuffer(&slot);

    sp<GraphicBuffer> newBuffer1 = sp<GraphicBuffer>::make(kWidth, kHeight, kFormat, kUsage);
    sp<GraphicBuffer> newBuffer2 = sp<GraphicBuffer>::make(kWidth, kHeight, kFormat, kUsage);

    // For some reason, you can attach an extra buffer?
    // b/400973991 to investigate
    EXPECT_EQ(OK, mBIC->attachBuffer(newBuffer1));
    EXPECT_EQ(INVALID_OPERATION, mBIC->attachBuffer(newBuffer2));

    ReleaseBuffer(slot);

    EXPECT_EQ(OK, mBIC->attachBuffer(newBuffer2));
    EXPECT_EQ(OK, mBIC->releaseBuffer(newBuffer1, Fence::NO_FENCE));
    EXPECT_EQ(OK, mBIC->releaseBuffer(newBuffer2, Fence::NO_FENCE));
}

// Test that delete BufferItemConsumer triggers onBufferFreed.
TEST_F(BufferItemConsumerTest, DetachBufferWithBuffer) {
    int slot;
    // Let buffer go through the cycle at least once.
    DequeueBuffer(&slot);
    QueueBuffer(slot);
    AcquireBuffer(&slot);

    sp<GraphicBuffer> buffer = mBuffers[slot];
    EXPECT_EQ(OK, mBIC->detachBuffer(buffer));
    EXPECT_THAT(mDetachedBufferSlots, testing::ElementsAre(slot));
    EXPECT_THAT(mDetachedBufferIds, testing::ElementsAre(buffer->getId()));
}

TEST_F(BufferItemConsumerTest, UnlimitedSlots_AcquireReleaseAll) {
    ASSERT_EQ(OK, mProducer->extendSlotCount(256));
    mBuffers.resize(256);

    ASSERT_EQ(OK, mProducer->setMaxDequeuedBufferCount(100));

    std::unordered_set<int> slots;
    for (int i = 0; i < 100; i++) {
        int slot;
        DequeueBuffer(&slot);
        slots.insert(slot);
    }
    EXPECT_EQ(100u, slots.size());

    for (int dequeuedSlot : slots) {
        QueueBuffer(dequeuedSlot);

        int slot;
        AcquireBuffer(&slot);
        ReleaseBuffer(slot);
    }
}

TEST_F(BufferItemConsumerTest, UnlimitedSlots_AcquireDetachAll) {
    ASSERT_EQ(OK, mProducer->extendSlotCount(256));
    mBuffers.resize(256);

    ASSERT_EQ(OK, mProducer->setMaxDequeuedBufferCount(100));

    std::unordered_set<int> slots;
    for (int i = 0; i < 100; i++) {
        int slot;
        DequeueBuffer(&slot);
        slots.insert(slot);
    }
    EXPECT_EQ(100u, slots.size());

    for (int dequeuedSlot : slots) {
        QueueBuffer(dequeuedSlot);

        int slot;
        AcquireBuffer(&slot);
        DetachBuffer(slot);
    }
}

TEST_F(BufferItemConsumerTest, OnSetFrameRateCallback) {
    class MockListener : public BufferItemConsumer::FrameAvailableListener {
    public:
        virtual void onFrameAvailable(const BufferItem&) override {}

        MOCK_METHOD(void, onSetFrameRate,
                    (float frameRate, int8_t compatibility, int8_t changeFrameRateStrategy),
                    (override));
    };

    sp<MockListener> mockListener = sp<MockListener>::make();
    mBIC->setFrameAvailableListener(mockListener);

    float expectedFrameRate = 60.0f;
    int8_t expectedCompatibility = ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE;
    int8_t expectedChangeFrameRateStrategy = ANATIVEWINDOW_CHANGE_FRAME_RATE_ALWAYS;

    EXPECT_CALL(*mockListener,
                onSetFrameRate(expectedFrameRate, expectedCompatibility,
                               expectedChangeFrameRateStrategy))
            .Times(1);

    status_t ret = mProducer->setFrameRate(expectedFrameRate, expectedCompatibility,
                                           expectedChangeFrameRateStrategy);
    ASSERT_EQ(NO_ERROR, ret);
}

TEST_F(BufferItemConsumerTest, Cache_ProducerDetach_FreesSlot) {
    auto [consumer, surface] = BufferItemConsumer::create(kUsage, 3);

    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));

    sp<GraphicBuffer> buffer;
    sp<Fence> fence;
    ASSERT_EQ(OK, surface->dequeueBuffer(&buffer, &fence));
    ASSERT_EQ(OK, surface->queueBuffer(buffer, fence));

    {
        BufferItem item;
        ASSERT_EQ(OK, consumer->acquireBuffer(&item, 0));
        ASSERT_EQ(OK, consumer->releaseBuffer(item, item.mFence));
    }

    wp<GraphicBuffer> weakBufferToDelete = buffer;

    ASSERT_EQ(OK, surface->dequeueBuffer(&buffer, &fence));
    ASSERT_EQ(OK, surface->detachBuffer(buffer));
    EXPECT_EQ(weakBufferToDelete.promote(), buffer);

    buffer = nullptr;
    EXPECT_EQ(nullptr, weakBufferToDelete.promote());
}

TEST_F(BufferItemConsumerTest, Cache_ProducerDetachNextBuffer_FreesSlot) {
    auto [consumer, surface] = BufferItemConsumer::create(kUsage, 3);

    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));

    sp<GraphicBuffer> buffer;
    sp<Fence> fence;
    ASSERT_EQ(OK, surface->dequeueBuffer(&buffer, &fence));
    ASSERT_EQ(OK, surface->queueBuffer(buffer, fence));

    {
        BufferItem item;
        ASSERT_EQ(OK, consumer->acquireBuffer(&item, 0));
        ASSERT_EQ(OK, consumer->releaseBuffer(item, item.mFence));
    }

    wp<GraphicBuffer> weakBufferToDelete = buffer;

    ASSERT_EQ(OK, surface->detachNextBuffer(&buffer, &fence));
    EXPECT_EQ(weakBufferToDelete.promote(), buffer);

    buffer = nullptr;
    EXPECT_EQ(nullptr, weakBufferToDelete.promote());
}

TEST_F(BufferItemConsumerTest, Cache_Disconnect_FreesSlot) {
    auto [consumer, surface] = BufferItemConsumer::create(kUsage, 3);

    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));

    sp<GraphicBuffer> buffer;
    sp<Fence> fence;
    ASSERT_EQ(OK, surface->dequeueBuffer(&buffer, &fence));
    ASSERT_EQ(OK, surface->queueBuffer(buffer, fence));

    {
        BufferItem item;
        ASSERT_EQ(OK, consumer->acquireBuffer(&item, 0));
        ASSERT_EQ(OK, consumer->releaseBuffer(item, item.mFence));
    }
    wp<GraphicBuffer> weakBufferToDelete = buffer;

    ASSERT_EQ(OK, surface->disconnect(NATIVE_WINDOW_API_CPU));

    buffer = nullptr;
    EXPECT_EQ(nullptr, weakBufferToDelete.promote());
}

TEST_F(BufferItemConsumerTest, SetMaxAcquiredBufferCount_TriggersCallback) {
    auto [consumer, surface] = BufferItemConsumer::create(kUsage, 3);

    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));

    ASSERT_EQ(OK, surface->setMaxDequeuedBufferCount(10));
    ASSERT_EQ(OK, consumer->setMaxAcquiredBufferCount(10));

    for (int i = 0; i < 10; i++) {
        sp<GraphicBuffer> buffer;
        sp<Fence> fence;
        ASSERT_EQ(OK, surface->dequeueBuffer(&buffer, &fence));
        ASSERT_EQ(OK, surface->queueBuffer(buffer, fence));
    }
    for (int i = 0; i < 10; i++) {
        BufferItem item;
        ASSERT_EQ(OK, consumer->acquireBuffer(&item, 0));
        ASSERT_EQ(OK, consumer->releaseBuffer(item, item.mFence));
    }

    int freedCount = 0;
    auto callback = [&](auto&) { freedCount++; };

    ASSERT_EQ(OK, surface->setMaxDequeuedBufferCount(1));
    ASSERT_EQ(OK, consumer->setMaxAcquiredBufferCount(1, callback));

    // 8 are freed because there's one dequeued and one acquired slot max, down from ten.
    EXPECT_EQ(8, freedCount);
}

TEST_F(BufferItemConsumerTest, Abandon_TriggersCallback) {
    auto [consumer, surface] = BufferItemConsumer::create(kUsage, 3);

    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));

    ASSERT_EQ(OK, surface->setMaxDequeuedBufferCount(10));
    ASSERT_EQ(OK, consumer->setMaxAcquiredBufferCount(10));

    for (int i = 0; i < 10; i++) {
        sp<GraphicBuffer> buffer;
        sp<Fence> fence;
        ASSERT_EQ(OK, surface->dequeueBuffer(&buffer, &fence));
        ASSERT_EQ(OK, surface->queueBuffer(buffer, fence));
    }
    for (int i = 0; i < 10; i++) {
        BufferItem item;
        ASSERT_EQ(OK, consumer->acquireBuffer(&item, 0));
        ASSERT_EQ(OK, consumer->releaseBuffer(item, item.mFence));
    }

    int freedCount = 0;
    auto callback = [&](auto&) { freedCount++; };

    consumer->abandon(callback);
    EXPECT_EQ(10, freedCount);
}

TEST_F(BufferItemConsumerTest, DiscardFreeBuffers_TriggersCallback) {
    auto [consumer, surface] = BufferItemConsumer::create(kUsage, 3);

    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));

    ASSERT_EQ(OK, surface->setMaxDequeuedBufferCount(10));
    ASSERT_EQ(OK, consumer->setMaxAcquiredBufferCount(10));

    for (int i = 0; i < 10; i++) {
        sp<GraphicBuffer> buffer;
        sp<Fence> fence;
        ASSERT_EQ(OK, surface->dequeueBuffer(&buffer, &fence));
        ASSERT_EQ(OK, surface->queueBuffer(buffer, fence));
    }
    for (int i = 0; i < 10; i++) {
        BufferItem item;
        ASSERT_EQ(OK, consumer->acquireBuffer(&item, 0));
        ASSERT_EQ(OK, consumer->releaseBuffer(item, item.mFence));
    }
    int freedCount = 0;
    auto callback = [&](auto&) { freedCount++; };

    status_t ret = consumer->discardFreeBuffers(callback);
    ASSERT_EQ(NO_ERROR, ret);
    EXPECT_EQ(10, freedCount);
}

TEST_F(BufferItemConsumerTest, TriggerBufferFreed_UsageChange) {
    auto [consumer, surface] = BufferItemConsumer::create(kUsage, 3);

    struct MockFreedListener : public BufferItemConsumer::BufferFreedListener {
        int mCount = 0;
        void onBufferFreed(const sp<GraphicBuffer>& /* graphicBuffer */) override { mCount++; }
    };
    sp<MockFreedListener> listener = sp<MockFreedListener>::make();
    consumer->setBufferFreedListener(listener);

    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, nullptr));

    // 1. Cycle one buffer to put it in the free list
    sp<GraphicBuffer> buffer;
    sp<Fence> fence;
    ASSERT_EQ(OK, surface->dequeueBuffer(&buffer, &fence));
    ASSERT_EQ(OK, surface->queueBuffer(buffer, fence));

    BufferItem item;
    ASSERT_EQ(OK, consumer->acquireBuffer(&item, 0));
    ASSERT_EQ(OK, consumer->releaseBuffer(item));

    ASSERT_EQ(0, listener->mCount);

    // 2. Trigger usage change on the producer side.
    native_window_set_usage(surface.get(), kUsage | GRALLOC_USAGE_SW_WRITE_OFTEN);

    // This dequeue should trigger reallocation because of usage change,
    // which should notify the consumer via onBuffersReleased -> onBufferFreed.
    ASSERT_EQ(OK, surface->dequeueBuffer(&buffer, &fence));

    // 3. Verify consumer was notified
    EXPECT_EQ(1, listener->mCount);
}

} // namespace android
