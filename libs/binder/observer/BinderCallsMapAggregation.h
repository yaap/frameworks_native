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
#include "../BuildFlags.h"
#include "BinderStatsUtils.h"

namespace android {

/**
 * Aggregates Binder Calls stats using a map.
 */
// TODO (b/458340205): Remove after BinderCallsVectorAggregation is rolled out.
class BinderCallsMapAggregation {
    // Limiting duration to 120'000'000 (2 minutes) to prevent overflow in 64-bit integer
    // sum squared.
    static constexpr int64_t kMaxDurationMicros = 120'000'000;
    static constexpr int64_t kNanoSecondsPerSec = 1000'000'000LL;

private:
    /**
     * KeyEqual function for Binder Spam aggregation of BinderCallData.
     */
    struct SpamStatsKeyEqual {
        bool operator()(const BinderCallData& lhs, const BinderCallData& rhs) const {
            return lhs.transactionCode == rhs.transactionCode && lhs.senderUid == rhs.senderUid &&
                    lhs.interfaceDescriptor == rhs.interfaceDescriptor;
        }
    };
    /**
     * Custom Hash for Binder Spam aggregation of BinderCallData in std::unordered_map.
     */
    struct SpamStatsKeyHash {
        size_t operator()(const BinderCallData& bcd) const {
            size_t h = std::hash<uid_t>{}(bcd.senderUid);
            h = std::__hash_combine(h, std::hash<std::u16string_view>{}(bcd.interfaceDescriptor));
            h = std::__hash_combine(h, std::hash<uint32_t>{}(bcd.transactionCode));
            return h;
        }
    };

    struct MetricAggregation {
        MetricAggregation() = default;
        uint32_t totalCalls = 0;
        uint32_t callsWithLatency = 0;
        int64_t durationSumMicros = 0;
        uint64_t callDurationSumSquaredMicros = 0;
        uint64_t cpuTimeCount = 0;
        uint64_t cpuTimeSumMicros = 0;
        uint64_t cpuTimeSumSquaredMicros = 0;
    };

public:
    using StatsBufferMap =
            std::unordered_map<BinderCallData,
                               std::unordered_map<int64_t /*startTimeSec*/, MetricAggregation>,
                               SpamStatsKeyHash, SpamStatsKeyEqual>;

    void addCallStatsLocked(BinderCallData&& datum) {
        int64_t startTimeSec = datum.startTimeNanos / kNanoSecondsPerSec;
        // Check if the buffer period has passed.
        auto [it, inserted] = mStatsBuffer[datum].try_emplace(startTimeSec, MetricAggregation());
        it->second.totalCalls++;
        if (datum.hasLatencyData()) {
            it->second.callsWithLatency++;
            uint64_t durationMicros = (datum.endTimeNanos - datum.startTimeNanos) / 1000;
            it->second.durationSumMicros += durationMicros;
            durationMicros = std::min<int64_t>(durationMicros, kMaxDurationMicros);
            if (kBinderObserverV2Enabled) {
                it->second.callDurationSumSquaredMicros += durationMicros * durationMicros;
            }
        }
        if (datum.cpuTimeNanos > 0) {
            it->second.cpuTimeCount++;
            int64_t cpuTimeMicros = datum.cpuTimeNanos / 1000;
            it->second.cpuTimeSumMicros += cpuTimeMicros;
            cpuTimeMicros = std::min<int64_t>(cpuTimeMicros, kMaxDurationMicros);
            it->second.cpuTimeSumSquaredMicros += cpuTimeMicros * cpuTimeMicros;
        }
    }

    StatsBufferMap& getBufferLocked() {
        return mStatsBuffer;
    }

private:
    StatsBufferMap mStatsBuffer;
};
} // namespace android
