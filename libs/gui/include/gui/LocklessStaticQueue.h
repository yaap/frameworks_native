/*
 * Copyright (C) 2025 The Android Open Source Project
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

#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <type_traits>

#include <utils/Log.h>

namespace android {

// Standard fixed size lockless SPSC queue
template <typename T, size_t N>
class LocklessStaticQueue {
private:
    static inline constexpr std::size_t cacheAlign = 64;

    alignas(cacheAlign) std::atomic<uint64_t> mLo{0};
    alignas(cacheAlign) std::atomic<uint64_t> mHi{0};

    alignas(cacheAlign) T mBuffer[N];

    // Invariants:
    // mLo <= mHi <= mLo + N
    //
    // mLo, mHi are monotonically increasing
    // mLo == mHi     => empty
    // mHi == mLo + N => full

    // When not full, producer writes to mBuffer[mHi % N] then increments mHi.
    // When not empty, consumer reads from mBuffer[mLo % N].

public:
    LocklessStaticQueue() = default;

    bool empty() const { return getLo() == getHi(); }

    bool full() const { return getHi() == getLo() + N; }

    void pushBack() {
        assert(!full());
        uint64_t hi = mHi.load(std::memory_order_relaxed);
        mHi.store(hi + 1, std::memory_order_release);
    }

    void popFront() {
        assert(!empty());
        uint64_t lo = mLo.load(std::memory_order_relaxed);
        mLo.store(lo + 1, std::memory_order_release);
    }

    T& front() {
        assert(!empty());
        return mBuffer[mLo.load(std::memory_order_acquire) % N];
    }

    const T& front() const {
        assert(!empty());
        return mBuffer[mLo.load(std::memory_order_acquire) % N];
    }

    uint64_t getLo() const { return mLo.load(std::memory_order_acquire); }

    uint64_t getHi() const { return mHi.load(std::memory_order_acquire); }

    // Helper to get write pointer
    T& getWriteSlot() {
        assert(!full());
        uint64_t hi = mHi.load(std::memory_order_relaxed);
        return mBuffer[hi % N];
    }

    T& at(uint64_t index) { return mBuffer[index % N]; }
};

} // namespace android
