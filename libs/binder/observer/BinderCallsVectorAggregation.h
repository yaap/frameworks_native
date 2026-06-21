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
#include <vector>
#include "BinderStatsUtils.h"

namespace android {
/**
 * Aggregates Binder Calls Stats using vectors for the previous and current second.
 * Aggregates Binder Call Stats into 1-second windows based on endTimeNanos.
 */
class BinderCallsVectorAggregation {
    using aggregateStatsFn = std::function<void(int64_t, std::vector<BinderCallData>&)>;

public:
    // This size is sufficient for 99.9% of the cases.
    static constexpr size_t kMaxBinderCallsBufferSize = 8192;
    static constexpr int64_t kNanoSecondsPerSec = 1000'000'000LL;

    /**
     * Adds a single Binder call's statistics, aggregating calls into 1-second windows.
     * If the call's timestamp advances the current 1-second window, the aggregated
     * statistics for the previous window(s) are processed.
     *
     * @param callData The BinderCallData for a single completed Binder call.
     * @param processStatsLocked A function used to process the aggregated BinderCallData
     *     from completed 1-second windows.
     */
    void addCallStatsLocked(BinderCallData&& callData, const aggregateStatsFn& processStatsLocked) {
        const int64_t callSec = callData.endTimeNanos / kNanoSecondsPerSec;

        // Advance the time window and process older stats if the callData is for a future second.
        if (callSec >= currentSecond + 1) {
            processStatsLocked(callSec, previousSecondCalls);
            previousSecondCalls.clear();
            // If the callData is for more than one second ahead, also process the current second.
            if (callSec >= currentSecond + 2) {
                processStatsLocked(callSec, currentSecondCalls);
                currentSecondCalls.clear();
            }
            currentSecond = callSec;
            // The current second's calls become the previous second's calls.
            previousSecondCalls.swap(currentSecondCalls);
        }

        // Add the callData to the appropriate buffer if there's space.
        if (callSec == currentSecond) {
            if (currentSecondCalls.size() < kMaxBinderCallsBufferSize) {
                currentSecondCalls.emplace_back(std::move(callData));
            }
        } else if (callSec == currentSecond - 1) {
            if (previousSecondCalls.size() < kMaxBinderCallsBufferSize) {
                previousSecondCalls.emplace_back(std::move(callData));
            }
        }
        // Data older than `currentSecond - 1` is implicitly discarded.
    }

private:
    // This is set to the current second on every buffer swap.
    int64_t currentSecond = 0;
    std::vector<BinderCallData> currentSecondCalls;
    std::vector<BinderCallData> previousSecondCalls;
};
} // namespace android
