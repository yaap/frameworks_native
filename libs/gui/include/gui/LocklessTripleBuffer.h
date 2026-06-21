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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>

#include <utils/Log.h>

namespace android {

// A single-producer single-consumer buffer for transferring state between threads/processes. The
// producer calls producerAcquire() to get a pointer to a writable state object, which it then
// modifies as desired. Once the state is ready, the producer calls producerRelease() to make it
// available to the consumer. Only the most recent state is stored for the consumer (so the next
// call to producerRelease() will replace the previous state).
//
// The consumer calls consume() to get a pointer to the most-recently-published state from the
// producer. It is safe to use the returned pointer until the next call to consume(). Note that if
// the producer has not published a new state since the last call to consume(), the same pointer
// may be returned.
//
// Note: the memory region is writable by the client; this means that the producer should NOT read
// any data from pointer returned by producerAcquire(), since any data there may have been corrupted
// by the client.
template <typename T>
class LocklessTripleBuffer {
private:
    static inline constexpr std::size_t cacheAlign = 64;

    // Align to cache line size to avoid false sharing.
    struct alignas(cacheAlign) AlignedT {
        T value;
    };

    AlignedT mBuffers[3];
    // mIndexState logically contains 2 indices (1 for each of the low 2 bytes).
    // Lowest byte (byte 0): index of most recent published state (initially 1).
    // Byte 1: index of state the consumer currently owns (initially 2).
    alignas(cacheAlign) std::atomic<uint32_t> mIndexState{0x0201};

    // The index that the producer is currently writing to. Only accessed/updated by the producer.
    // The producer maintains the invariant that this value is always different from the 2 indices
    // in mIndexState; therefore the producer is never writing to an index that the consumer owns
    // or might claim ownership of.
    alignas(cacheAlign) uint32_t mWriteIndex = 0;

public:
    const AlignedT* getBuffers() { return mBuffers; }

    uint32_t getAcquiredIndex(const T* ptr) const { return (uint32_t)((AlignedT*)ptr - mBuffers); }

    static inline constexpr bool is_always_lock_free = std::atomic<uint32_t>::is_always_lock_free;

    // NOTE: This assumes that T has a meaningful (copy) = operator.
    // If/when that's NOT the case, consider adding a MakeLocklessTripleBuffer
    // static method with constructor arguments instead.
    // explicit LocklessTripleBuffer(T default_value = {}) {
    // for (int i = 0; i < 3; i++) {
    // mBuffers[i].value = default_value;
    //}
    //}
    explicit LocklessTripleBuffer() {}

    inline T* producerAcquire() {
        // Since the index state is modifiable by the untrusted client, we make sure the write index
        // is valid (within the [0, 3) range). The producer doesn't care which of the 3 buffers it
        // writes into next; if the client messes up the index, it can only affect the
        // client's correctness.
        const uint32_t index = std::min(mWriteIndex, 2u);
        return &(mBuffers[index].value);
    }

    inline void producerRelease() {
        // Index of the state that the producer just finished writing.
        const uint32_t indexToPublish = mWriteIndex;

        // When the producer publishes a new state, it needs to atomically read the index that
        // the consumer owns, and update the index of the most recent published state. This must
        // be done as a single atomic operation to meet the correctness requirements for consume().
        uint32_t oldIndexState = mIndexState.load(std::memory_order_relaxed);
        uint32_t newIndexState;
        do {
            // Keep the consumer-owned index, and update the most recent index.
            newIndexState = (oldIndexState & 0xff00) | indexToPublish;
            // Note semantics of compare_exchange_weak(): if it fails, the |oldIndexState| will
            // be reloaded with the current value of mIndexState.
        } while (!mIndexState.compare_exchange_weak(oldIndexState, newIndexState,
                                                    std::memory_order_release,
                                                    std::memory_order_relaxed));

        // The next write needs to be to an index that is not owned by the consumer, and is not
        // the one we just finished writing. Note that indexToPublish and consumerIndex are
        // never equal (since we can't have been writing to the index owned by the consumer), so
        // this math trick works.
        const uint32_t consumerIndex = (oldIndexState >> 8);
        mWriteIndex = 3 - (indexToPublish + consumerIndex);
    }

    inline T* consume() {
        // To acquire the most recent state, the consumer must get the index of the most recent
        // state, and then set the consumer-owned index to that index. This must be done as a single
        // atomic operation; otherwise, the producer might call producerRelease() between reading
        // the most recent state index and updating the consumer-owned index; this would lead to the
        // producer writing to the index that is owned by the consumer.
        //
        // Example of bad ordering with separate read and write:
        //  1. Producer writing to 0, most recent = 1, consumer-owned = 2.
        //  2. Consumer enters consume(), reads most recent (= 1), and does not progress farther.
        //  3. Producer runs producerRelease(). It sets most recent to 0, and sees
        //     consumer-owned = 2, so it sets the new write index to 1.
        //  4. consume() continues executing, setting the consumer-owned value to 1.
        //  *** RACE CONDITION *** Now the producer is writing to the index that the consumer owns.
        //
        // Therefore we use atomic compare-and swap to do the read and write in a single operation.
        uint32_t mostRecentPublishedIndex, newIndexState;
        uint32_t oldIndexState = mIndexState.load(std::memory_order_relaxed);
        do {
            // Update the consumer-owned index to match the most recent index.
            mostRecentPublishedIndex = oldIndexState & 0xff;
            newIndexState = (mostRecentPublishedIndex << 8) | mostRecentPublishedIndex;
        } while (!mIndexState.compare_exchange_weak(oldIndexState, newIndexState,
                                                    std::memory_order_acquire,
                                                    std::memory_order_relaxed));

        return &(mBuffers[mostRecentPublishedIndex].value);
    }

    // Checks whether internal state appears to be correct.
    // Since this is a flat data structure, the writer can easily corrupt mIndexState,
    // crashing the reader.
    inline bool isInternalStateValid(uint32_t& indexStateOut) {
        indexStateOut = mIndexState.load(std::memory_order_relaxed);
        // Only lower 2 bits of lowest 2 bytes can be used, and those values cannot equal 3.
        return ((indexStateOut & (uint32_t)~0x00000303) == 0 && (indexStateOut & 0x3) != 0x3 &&
                (indexStateOut & 0x300) != 0x300);
    }

    // Logs and crashes when internal state is incorrect.
    inline void validateInternalState() {
        uint32_t indexState;
        LOG_ALWAYS_FATAL_IF(!isInternalStateValid(indexState),
                            "LocklessTripleBuffer has corrupt index state 0x%x", indexState);
    }
};

} // namespace android
