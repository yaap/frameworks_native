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

#include <gtest/gtest.h>

#include "../BuildFlags.h"
#include "../observer/BinderCallsMapAggregation.h"
#include "../observer/BinderCallsVectorAggregation.h"

using namespace android;

// --- BinderCallsMapAggregation Tests ---
class BinderCallsMapAggregationTest : public testing::Test {};

TEST_F(BinderCallsMapAggregationTest, InitialState) {
    BinderCallsMapAggregation buffer;
    EXPECT_TRUE(buffer.getBufferLocked().empty());
}

TEST_F(BinderCallsMapAggregationTest, AddSingleCall) {
    BinderCallsMapAggregation buffer;
    BinderCallData datum;
    datum.senderUid = 1000;
    datum.transactionCode = 1;
    datum.interfaceDescriptor = String16(u"interface");
    datum.startTimeNanos = 1000'000'000;
    datum.endTimeNanos = 1001'000'000;
    datum.cpuTimeNanos = 100'000;

    buffer.addCallStatsLocked(std::move(datum));

    auto& statsBuffer = buffer.getBufferLocked();
    ASSERT_EQ(statsBuffer.size(), 1);

    auto& outer_key = statsBuffer.begin()->first;
    EXPECT_EQ(outer_key.senderUid, 1000);
    EXPECT_EQ(outer_key.transactionCode, 1);
    EXPECT_EQ(outer_key.interfaceDescriptor, String16(u"interface"));

    auto& inner_map = statsBuffer.begin()->second;
    ASSERT_EQ(inner_map.size(), 1);

    auto& metrics = inner_map.begin()->second;
    EXPECT_EQ(metrics.totalCalls, 1);
    EXPECT_EQ(metrics.callsWithLatency, 1);
    EXPECT_EQ(metrics.durationSumMicros, 1000);
    if (kBinderObserverV2Enabled) {
        EXPECT_EQ(metrics.callDurationSumSquaredMicros, 1000 * 1000);
    }
    EXPECT_EQ(metrics.cpuTimeCount, 1);
    EXPECT_EQ(metrics.cpuTimeSumMicros, 100);
    EXPECT_EQ(metrics.cpuTimeSumSquaredMicros, 100 * 100);
}

TEST_F(BinderCallsMapAggregationTest, AddMultipleCallsSameKey) {
    BinderCallsMapAggregation buffer;
    BinderCallData datum1;
    datum1.senderUid = 1000;
    datum1.transactionCode = 1;
    datum1.interfaceDescriptor = String16(u"interface");
    datum1.startTimeNanos = 1000'000'000;
    datum1.endTimeNanos = 1001'000'000;
    datum1.cpuTimeNanos = 100'000;

    buffer.addCallStatsLocked(std::move(datum1));

    BinderCallData datum2;
    datum2.senderUid = 1000;
    datum2.transactionCode = 1;
    datum2.interfaceDescriptor = String16(u"interface");
    datum2.startTimeNanos = 1500'000'000;
    datum2.endTimeNanos = 1502'000'000;
    datum2.cpuTimeNanos = 200'000;

    buffer.addCallStatsLocked(std::move(datum2));

    auto& statsBuffer = buffer.getBufferLocked();
    ASSERT_EQ(statsBuffer.size(), 1);

    auto& inner_map = statsBuffer.begin()->second;
    ASSERT_EQ(inner_map.size(), 1);

    auto& metrics = inner_map.begin()->second;
    EXPECT_EQ(metrics.totalCalls, 2);
    EXPECT_EQ(metrics.callsWithLatency, 2);
    EXPECT_EQ(metrics.durationSumMicros, 1000 + 2000);
    if (kBinderObserverV2Enabled) {
        EXPECT_EQ(metrics.callDurationSumSquaredMicros, 1000 * 1000 + 2000 * 2000);
    }
    EXPECT_EQ(metrics.cpuTimeCount, 2);
    EXPECT_EQ(metrics.cpuTimeSumMicros, 100 + 200);
    EXPECT_EQ(metrics.cpuTimeSumSquaredMicros, 100 * 100 + 200 * 200);
}

TEST_F(BinderCallsMapAggregationTest, AddMultipleCallsDifferentKeys) {
    BinderCallsMapAggregation buffer;
    BinderCallData datum1;
    datum1.senderUid = 1000;
    datum1.transactionCode = 1;
    datum1.interfaceDescriptor = String16(u"interface");
    datum1.startTimeNanos = 1000'000'000;
    datum1.endTimeNanos = 1001'000'000;
    datum1.cpuTimeNanos = 100'000;

    buffer.addCallStatsLocked(std::move(datum1));

    BinderCallData datum2;
    datum2.senderUid = 1001;
    datum2.transactionCode = 2;
    datum2.interfaceDescriptor = String16(u"interface2");
    datum2.startTimeNanos = 1500'000'000;
    datum2.endTimeNanos = 1502'000'000;
    datum2.cpuTimeNanos = 200'000;

    buffer.addCallStatsLocked(std::move(datum2));

    auto& statsBuffer = buffer.getBufferLocked();
    ASSERT_EQ(statsBuffer.size(), 2);
}

TEST_F(BinderCallsMapAggregationTest, NoLatencyData) {
    BinderCallsMapAggregation buffer;
    BinderCallData datum;
    datum.senderUid = 1000;
    datum.transactionCode = 1;
    datum.interfaceDescriptor = String16(u"interface");
    datum.startTimeNanos = 1000'000'000;
    datum.endTimeNanos = -1; // No latency data
    datum.cpuTimeNanos = 100'000;

    buffer.addCallStatsLocked(std::move(datum));

    auto& statsBuffer = buffer.getBufferLocked();
    auto& inner_map = statsBuffer.begin()->second;
    auto& metrics = inner_map.begin()->second;

    EXPECT_EQ(metrics.totalCalls, 1);
    EXPECT_EQ(metrics.callsWithLatency, 0);
    EXPECT_EQ(metrics.durationSumMicros, 0);
    EXPECT_EQ(metrics.callDurationSumSquaredMicros, 0);
    EXPECT_EQ(metrics.cpuTimeCount, 1);
    EXPECT_EQ(metrics.cpuTimeSumMicros, 100);
    EXPECT_EQ(metrics.cpuTimeSumSquaredMicros, 100 * 100);
}

TEST_F(BinderCallsMapAggregationTest, NoCpuTime) {
    BinderCallsMapAggregation buffer;
    BinderCallData datum;
    datum.senderUid = 1000;
    datum.transactionCode = 1;
    datum.interfaceDescriptor = String16(u"interface");
    datum.startTimeNanos = 1000'000'000;
    datum.endTimeNanos = 1001'000'000;
    datum.cpuTimeNanos = -1; // No CPU time data

    buffer.addCallStatsLocked(std::move(datum));

    auto& statsBuffer = buffer.getBufferLocked();
    auto& inner_map = statsBuffer.begin()->second;
    auto& metrics = inner_map.begin()->second;

    EXPECT_EQ(metrics.totalCalls, 1);
    EXPECT_EQ(metrics.callsWithLatency, 1);
    EXPECT_EQ(metrics.durationSumMicros, 1000);
    if (kBinderObserverV2Enabled) {
        EXPECT_EQ(metrics.callDurationSumSquaredMicros, 1000 * 1000);
    }
    EXPECT_EQ(metrics.cpuTimeCount, 0);
    EXPECT_EQ(metrics.cpuTimeSumMicros, 0);
    EXPECT_EQ(metrics.cpuTimeSumSquaredMicros, 0);
}

TEST_F(BinderCallsMapAggregationTest, AddCallsDifferentTimeBuckets) {
    BinderCallsMapAggregation buffer;
    BinderCallData datum1;
    datum1.senderUid = 1000;
    datum1.transactionCode = 1;
    datum1.interfaceDescriptor = String16(u"interface");
    datum1.startTimeNanos = 1000'000'000; // 1s
    datum1.endTimeNanos = 1001'000'000;
    datum1.cpuTimeNanos = 100'000;

    buffer.addCallStatsLocked(std::move(datum1));

    BinderCallData datum2;
    datum2.senderUid = 1000;
    datum2.transactionCode = 1;
    datum2.interfaceDescriptor = String16(u"interface");
    datum2.startTimeNanos = 2500'000'000; // 2.5s
    datum2.endTimeNanos = 2502'000'000;
    datum2.cpuTimeNanos = 200'000;

    buffer.addCallStatsLocked(std::move(datum2));

    auto& statsBuffer = buffer.getBufferLocked();
    ASSERT_EQ(statsBuffer.size(), 1);

    auto& inner_map = statsBuffer.begin()->second;
    ASSERT_EQ(inner_map.size(), 2);

    // Check first bucket (1s)
    auto it1 = inner_map.find(1);
    ASSERT_NE(it1, inner_map.end());
    EXPECT_EQ(it1->second.totalCalls, 1);
    EXPECT_EQ(it1->second.durationSumMicros, 1000);

    // Check second bucket (2s)
    auto it2 = inner_map.find(2);
    ASSERT_NE(it2, inner_map.end());
    EXPECT_EQ(it2->second.totalCalls, 1);
    EXPECT_EQ(it2->second.durationSumMicros, 2000);
}

TEST_F(BinderCallsMapAggregationTest, DurationOverflow) {
    BinderCallsMapAggregation buffer;
    BinderCallData datum;
    datum.senderUid = 1000;
    datum.transactionCode = 1;
    datum.interfaceDescriptor = String16(u"interface");
    datum.startTimeNanos = 1000'000'000;
    datum.endTimeNanos = datum.startTimeNanos + 200'000'000'000; // > 120'000'000 micros
    datum.cpuTimeNanos = 200'000'000'000;                        // > 120'000'000 micros

    buffer.addCallStatsLocked(std::move(datum));

    auto& statsBuffer = buffer.getBufferLocked();
    auto& inner_map = statsBuffer.begin()->second;
    auto& metrics = inner_map.begin()->second;

    uint64_t expected_duration_micros = (200'000'000'000) / 1000;
    uint64_t limited_duration_micros = 120'000'000;
    int64_t expected_cpu_micros = (200'000'000'000) / 1000;
    int64_t limited_cpu_micros = 120'000'000;

    EXPECT_EQ(metrics.durationSumMicros, expected_duration_micros);
    if (kBinderObserverV2Enabled) {
        EXPECT_EQ(metrics.callDurationSumSquaredMicros,
                  limited_duration_micros * limited_duration_micros);
    }
    EXPECT_EQ(metrics.cpuTimeSumMicros, expected_cpu_micros);
    EXPECT_EQ(metrics.cpuTimeSumSquaredMicros, limited_cpu_micros * limited_cpu_micros);
}

// --- BinderCallsVectorAggregation Tests ---
class BinderCallsVectorAggregationTest : public testing::Test {};

TEST_F(BinderCallsVectorAggregationTest, DataProcessedOnTimeChange) {
    BinderCallsVectorAggregation buffer;
    std::vector<BinderCallData> processedData;
    auto callback = [&]([[maybe_unused]] int64_t, std::vector<BinderCallData>& data) {
        processedData.insert(processedData.end(), data.begin(), data.end());
    };

    // This call will set cutoffSec = 1. The data will be in currentSecondCalls.
    // callback is called with empty previousSecondCalls.
    buffer.addCallStatsLocked({.endTimeNanos = 1500'000'000}, callback);
    EXPECT_TRUE(processedData.empty());

    // This call will set cutoffSec = 2.
    // callback is called with empty previousSecondCalls.
    // currentSecondCalls (with 1.5s data) is swapped to previousSecondCalls.
    buffer.addCallStatsLocked({.endTimeNanos = 2500'000'000}, callback);
    EXPECT_TRUE(processedData.empty());

    // This call will set cutoffSec = 3.
    // callback is called with previousSecondCalls (containing 1.5s data).
    buffer.addCallStatsLocked({.endTimeNanos = 3500'000'000}, callback);
    ASSERT_EQ(processedData.size(), 1);
    EXPECT_EQ(processedData[0].endTimeNanos, 1500'000'000);
}

TEST_F(BinderCallsVectorAggregationTest, AddCallToPreviousBucket) {
    BinderCallsVectorAggregation buffer;
    std::vector<BinderCallData> processedData;
    auto callback = [&]([[maybe_unused]] int64_t, std::vector<BinderCallData>& data) {
        processedData = data;
    };

    // Set cutoffSec=1.
    buffer.addCallStatsLocked({.endTimeNanos = 1500'000'000}, callback);
    processedData.clear();

    // This call will go to previousSecondCalls.
    buffer.addCallStatsLocked({.endTimeNanos = 500'000'000}, callback);
    EXPECT_TRUE(processedData.empty()); // No processing yet.

    // This call will trigger processing of previousSecondCalls.
    buffer.addCallStatsLocked({.endTimeNanos = 2500'000'000}, callback);

    ASSERT_EQ(processedData.size(), 1);
    EXPECT_EQ(processedData[0].endTimeNanos, 500'000'000);
}

TEST_F(BinderCallsVectorAggregationTest, PreviousSecondBufferFull) {
    BinderCallsVectorAggregation buffer;
    bool callbackCalled = false;
    std::vector<BinderCallData> processedData;
    auto callback = [&]([[maybe_unused]] int64_t, std::vector<BinderCallData>& data) {
        callbackCalled = true;
        processedData = data;
    };

    // Set cutoffSec=1
    buffer.addCallStatsLocked({.endTimeNanos = 1500'000'000},
                              [&]([[maybe_unused]] int64_t,
                                  [[maybe_unused]] std::vector<BinderCallData>& data) {});
    callbackCalled = false;

    for (size_t i = 0; i < BinderCallsVectorAggregation::kMaxBinderCallsBufferSize; ++i) {
        buffer.addCallStatsLocked({.endTimeNanos = 500'000'000}, callback);
    }

    ASSERT_FALSE(callbackCalled);

    // This call should be dropped as the buffer is full.
    buffer.addCallStatsLocked({.endTimeNanos = 600'000'000}, callback);

    ASSERT_FALSE(callbackCalled);

    // This call will trigger processing of previousSecondCalls.
    buffer.addCallStatsLocked({.endTimeNanos = 2500'000'000}, callback);

    EXPECT_TRUE(callbackCalled);
    EXPECT_EQ(processedData.size(), BinderCallsVectorAggregation::kMaxBinderCallsBufferSize);

    // Verify that the 0.6s call is not present because it was dropped.
    for (const auto& call : processedData) {
        EXPECT_NE(call.endTimeNanos, 600'000'000);
    }
}

TEST_F(BinderCallsVectorAggregationTest, CurrentSecondBufferFull) {
    BinderCallsVectorAggregation buffer;
    std::vector<BinderCallData> processedData;
    auto callback = [&]([[maybe_unused]] int64_t, std::vector<BinderCallData>& data) {
        processedData.insert(processedData.end(), data.begin(), data.end());
    };

    // Set cutoffSec=1 and fill currentSecondCalls
    for (size_t i = 0; i < BinderCallsVectorAggregation::kMaxBinderCallsBufferSize; ++i) {
        buffer.addCallStatsLocked({.endTimeNanos = 1500'000'000}, callback);
    }
    processedData.clear();

    // This call should be dropped.
    buffer.addCallStatsLocked({.endTimeNanos = 1600'000'000}, callback);

    // Move to next second to swap buffers, and next to process.
    buffer.addCallStatsLocked({.endTimeNanos = 2500'000'000}, callback);
    buffer.addCallStatsLocked({.endTimeNanos = 3500'000'000}, callback);

    // Only kMaxBinderCallsBufferSize calls should have been processed.
    EXPECT_EQ(processedData.size(), BinderCallsVectorAggregation::kMaxBinderCallsBufferSize);

    // Verify no 1.6s call is present.
    for (const auto& call : processedData) {
        EXPECT_NE(call.endTimeNanos, 1600'000'000);
    }
}

TEST_F(BinderCallsVectorAggregationTest, DataHandlingAcrossMultipleSeconds) {
    BinderCallsVectorAggregation buffer;
    std::vector<BinderCallData> processedData;
    auto callback = [&]([[maybe_unused]] int64_t, std::vector<BinderCallData>& data) {
        processedData.insert(processedData.end(), data.begin(), data.end());
    };

    // sec 0
    buffer.addCallStatsLocked({.endTimeNanos = 500'000'000}, callback); // current (cutoff=0)

    // sec 1
    buffer.addCallStatsLocked({.endTimeNanos = 1500'000'000},
                              callback); // current (cutoff=1), previous={0.5s}
    buffer.addCallStatsLocked({.endTimeNanos = 1800'000'000}, callback); // current (cutoff=1)

    // sec 2: processes data from sec 0
    buffer.addCallStatsLocked({.endTimeNanos = 2500'000'000},
                              callback); // current (cutoff=2), cb on {0.5s}, previous={1.5s, 1.8s}

    ASSERT_EQ(processedData.size(), 1);
    EXPECT_EQ(processedData[0].endTimeNanos, 500'000'000);
    processedData.clear();

    // sec 3: processes data from sec 1
    buffer.addCallStatsLocked({.endTimeNanos = 3500'000'000},
                              callback); // current (cutoff=3), cb on {1.5s, 1.8s}, previous={2.5s}

    ASSERT_EQ(processedData.size(), 2);
    EXPECT_EQ(processedData[0].endTimeNanos, 1500'000'000);
    EXPECT_EQ(processedData[1].endTimeNanos, 1800'000'000);
}

TEST_F(BinderCallsVectorAggregationTest, TimeJumpDropsOldData) {
    BinderCallsVectorAggregation buffer;

    // This will set cutoffSec=3 and the data will be in current.
    buffer.addCallStatsLocked({.endTimeNanos = 3500'000'000}, [](int64_t, auto&) {});
    // This data for 1.5s is too old and will be dropped.
    // cutoffSec=3, so anything with endTimeNanos < 2s is dropped.
    buffer.addCallStatsLocked({.endTimeNanos = 1500'000'000}, [](int64_t, auto&) {});

    std::vector<BinderCallData> processedData;
    // This will set cutoffSec=4. The data from second 3 is moved to previous.
    buffer.addCallStatsLocked({.endTimeNanos = 4500'000'000},
                              [&]([[maybe_unused]] int64_t, std::vector<BinderCallData>& data) {
                                  processedData = data;
                              });

    // No data processed yet because previous was empty before the swap.
    ASSERT_TRUE(processedData.empty());

    // This will set cutoffSec=5 and process the data from second 3.
    buffer.addCallStatsLocked({.endTimeNanos = 5500'000'000},
                              [&]([[maybe_unused]] int64_t, std::vector<BinderCallData>& data) {
                                  processedData = data;
                              });

    // Data for 3.5s is in previousSecondCalls and gets processed.
    ASSERT_EQ(processedData.size(), 1);
    EXPECT_EQ(processedData[0].endTimeNanos, 3500'000'000);
}
