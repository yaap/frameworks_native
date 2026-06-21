/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <sys/mman.h>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

struct MagicRingBufferEntry {
    uint32_t size;
};

// Lockless Single-Producer Single-Consumer (SPSC) Magic Ring Buffer
//
// How it works:
// 1. Monotonic Counters: mHi (write) and mLo (read) grow indefinitely.
//    Available data = (mHi - mLo). Free space = N - (mHi - mLo).
// 2. Virtual Memory Aliasing ("Magic"): Allocates 2*N bytes of virtual memory.
//    The second half [N, 2N) is mapped to the physical pages of the first half [0, N).
//    Hardware handles wrap-around automatically, allowing zero-copy contiguous access.
// 3. Workflow:
//    - Producer calls reserve<T>(), constructs data in-place, then calls commit().
//    - Consumer calls peek() to read data, then pop() to advance mLo.
template <int64_t N>
struct MagicRingBuffer {
    static constexpr int64_t CACHE_LINE_SIZE = 64;
    static constexpr int64_t PAGE_ALIGNMENT = 16384;

    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
    static_assert(N % PAGE_ALIGNMENT == 0, "N must be a multiple of page alignment");

    MagicRingBuffer() = default;

    bool initMagicMapping(int shmFd, off_t structFileOffset = 0) {
        off_t bytesOffset = structFileOffset + offsetof(MagicRingBuffer, mBytes);

        void* result = mmap(mBytes + N, N, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, shmFd,
                            bytesOffset);
        return result != MAP_FAILED;
    }

    static constexpr int64_t align8(int64_t size) { return (size + 7) & ~7; }

    template <typename T>
        requires std::derived_from<T, MagicRingBufferEntry>
    T* reserve() {
        uint32_t reqSize = sizeof(T);
        int64_t alignedSize = align8(reqSize);

        int64_t currentLo = mLo.load(std::memory_order_acquire);
        int64_t currentHi = mHi.load(std::memory_order_relaxed);

        if (N + currentLo < alignedSize + currentHi) {
            return nullptr;
        }

        T* e = reinterpret_cast<T*>(mBytes + (currentHi % N));
        e->size = reqSize;
        return e;
    }

    template <typename T>
    T* append(size_t count = 1) {
        int64_t currentLo = mLo.load(std::memory_order_acquire);
        int64_t currentHi = mHi.load(std::memory_order_relaxed);

        MagicRingBufferEntry* e = reinterpret_cast<MagicRingBufferEntry*>(mBytes + (currentHi % N));

        int64_t currentAllocSize = align8(e->size);
        uint32_t reqExtraSize = sizeof(T) * count;
        int64_t alignedExtraSize = align8(reqExtraSize);

        if (N + currentLo < currentAllocSize + alignedExtraSize + currentHi) {
            return nullptr;
        }

        T* ptr = reinterpret_cast<T*>(mBytes + (currentHi % N) + currentAllocSize);
        e->size = static_cast<uint32_t>(currentAllocSize + reqExtraSize);
        return ptr;
    }

    void commit() {
        int64_t currentHi = mHi.load(std::memory_order_relaxed);
        MagicRingBufferEntry* e = reinterpret_cast<MagicRingBufferEntry*>(mBytes + (currentHi % N));

        mHi.store(currentHi + align8(e->size), std::memory_order_release);
    }

    MagicRingBufferEntry* peek() {
        int64_t currentHi = mHi.load(std::memory_order_acquire);
        int64_t currentLo = mLo.load(std::memory_order_relaxed);
        int64_t available = currentHi - currentLo;

        if (available < static_cast<int64_t>(sizeof(MagicRingBufferEntry))) {
            return nullptr;
        }

        MagicRingBufferEntry* e = reinterpret_cast<MagicRingBufferEntry*>(mBytes + (currentLo % N));
        if (available < align8(e->size)) {
            return nullptr;
        }

        return e;
    }

    void pop() {
        int64_t currentLo = mLo.load(std::memory_order_relaxed);
        MagicRingBufferEntry* e = reinterpret_cast<MagicRingBufferEntry*>(mBytes + (currentLo % N));

        mLo.store(currentLo + align8(e->size), std::memory_order_release);
    }

private:
    alignas(PAGE_ALIGNMENT) uint8_t mBytes[2 * N];
    alignas(CACHE_LINE_SIZE) std::atomic<int64_t> mHi{0};
    alignas(CACHE_LINE_SIZE) std::atomic<int64_t> mLo{0};
};