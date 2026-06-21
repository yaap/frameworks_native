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
#include <android/os/binder/BinderCallsStats.h>
#include <android/os/binder/BinderSpamStats.h>
#include <android/os/binder/BnBinderStatsConsumerService.h>
#include <dlfcn.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <utils/SystemClock.h>

#include <../BuildFlags.h>
#include <../JvmUtils.h>
#include <../observer/BinderStatsPusher.h>
#include <../observer/BinderStatsUtils.h>
#include <android_os_binder_flags.h>
#include <jni.h>
#include "fakeservicemanager/FakeServiceManager.h"

using android::FakeServiceManager;
using namespace android;
using namespace testing;
using os::binder::BinderCallsStats;
using os::binder::BinderSpamStats;

constexpr bool kBinderStatsLatencyHistogram = android::os::binder::flags::binder_stats_v3();

#ifdef LIBBINDER_BINDER_OBSERVER_V2
constexpr int64_t kSpamAggregationWindowSec = 1;
constexpr int64_t kLatencyAggregationWindowSec = 1; // Same as spam for now
#else
constexpr int64_t kSpamAggregationWindowSec = 5;
constexpr int64_t kLatencyAggregationWindowSec = 5; // Same as spam for now
#endif

// --- Mocks ---
/**
 * Mock for IBinderStatsConsumerService to intercept calls for reporting stats.
 */
class MockBinderStatsConsumerService : public os::binder::BnBinderStatsConsumerService {
public:
    MOCK_METHOD(binder::Status, reportSpamStats, (const std::vector<BinderSpamStats>&), (override));
    MOCK_METHOD(binder::Status, reportCallStats, (const std::vector<BinderCallsStats>&),
                (override));
    MOCK_METHOD(binder::Status, reportSecondGranularityStats,
                (const std::vector<os::binder::SingleSecondBinderStats>&), (override));
    MOCK_METHOD(BBinder*, localBinder, (), (override));
};

/**
 * Mock for IServiceManager to control service lookup.
 */
class MockServiceManager : public FakeServiceManager {
public:
    MOCK_METHOD(sp<IBinder>, checkService, (const String16& name), (const, override));
};

// --- Test Fixture ---

/**
 * Initializes the default service manager with a mock implementation once per test run.
 */
void initServiceManagerOnce() {
    static std::once_flag gSmOnce;
    std::call_once(gSmOnce, [] {
        sp<NiceMock<MockServiceManager>> mockServiceManager =
                sp<NiceMock<MockServiceManager>>::make();
        setDefaultServiceManager(mockServiceManager);
    });
}
/**
 * Struct for expected BinderSpamStats to support designated initializers.
 */
struct ExpectedSpamStats {
    uint32_t clientUid = 0;
    String16 interfaceDescriptor;
    String16 aidlMethod;
    int32_t secondsWithAtLeast125Calls = 0;
    int32_t secondsWithAtLeast250Calls = 0;
    int32_t peakCallCountPerSecond = 0;
};

/**
 * Helper function to create a BinderSpamStats for spam comparison.
 */
BinderSpamStats createExpectedSpamStats(const ExpectedSpamStats& p) {
    auto stats = BinderSpamStats();
    stats.clientUid = static_cast<int32_t>(p.clientUid);
    stats.interfaceDescriptor = p.interfaceDescriptor;
    stats.aidlMethod = p.aidlMethod;
    stats.secondsWithAtLeast125Calls = p.secondsWithAtLeast125Calls;
    stats.secondsWithAtLeast250Calls = p.secondsWithAtLeast250Calls;
    stats.peakCallCountPerSecond = p.peakCallCountPerSecond;
    return stats;
}

/**
 * Struct for expected BinderCallsStats to support designated initializers.
 */
struct ExpectedCallStats {
    uint32_t clientUid = 0;
    String16 interfaceDescriptor;
    String16 aidlMethod;
    int64_t callCount = 0;
    int64_t durationSumMicros = 0;
    int64_t callDurationSumSquaredMicros = 0;
    int32_t secondsWithAtLeast10Calls = 0;
    int32_t secondsWithAtLeast50Calls = 0;
    int64_t cpuTimeCount = 0;
    int64_t cpuTimeSumMicros = 0;
    int64_t cpuTimeSumSquaredMicros = 0;
};

/**
 * Helper function to create a BinderCallsStats for latency comparison.
 */
BinderCallsStats createExpectedCallStats(const ExpectedCallStats& p) {
    auto stats = BinderCallsStats();
    stats.clientUid = static_cast<int32_t>(p.clientUid);
    stats.interfaceDescriptor = p.interfaceDescriptor;
    stats.aidlMethod = p.aidlMethod;
    stats.callCount = p.callCount;
    stats.durationSumMicros = p.durationSumMicros;
    stats.callDurationSumSquaredMicros = p.callDurationSumSquaredMicros;
    stats.secondsWithAtLeast10Calls = p.secondsWithAtLeast10Calls;
    stats.secondsWithAtLeast50Calls = p.secondsWithAtLeast50Calls;
    stats.cpuTimeCount = p.cpuTimeCount;
    stats.cpuTimeSumMicros = p.cpuTimeSumMicros;
    stats.cpuTimeSumSquaredMicros = p.cpuTimeSumSquaredMicros;
    return stats;
}

struct ExpectedSingleSecondStats {
    uint32_t clientUid;
    String16 interfaceDescriptor;
    String16 aidlMethod;
    int64_t callCount = 0;
    int64_t durationSumMicros = 0;
    int64_t callDurationSumSquaredMicros = 0;
    int64_t cpuTimeCount = 0;
    int64_t cpuTimeSumMicros = 0;
    int64_t cpuTimeSumSquaredMicros = 0;
    std::vector<int64_t> rawLatencies = {};
};

os::binder::SingleSecondBinderStats createExpectedSingleSecondBinderStats(
        const ExpectedSingleSecondStats& p) {
    auto stats = os::binder::SingleSecondBinderStats();
    stats.clientUid = static_cast<int32_t>(p.clientUid);
    stats.interfaceDescriptor = p.interfaceDescriptor;
    stats.aidlMethod = p.aidlMethod.empty() ? String16(u"") : p.aidlMethod;
    stats.durationCount = p.callCount;
    stats.callCount = p.callCount;
    stats.durationMicrosSum = p.durationSumMicros;
    stats.durationMicrosSquaredSum = p.callDurationSumSquaredMicros;
    stats.cpuTimeCount = p.cpuTimeCount;
    stats.cpuTimeMicrosSum = p.cpuTimeSumMicros;
    stats.cpuTimeMicrosSquaredSum = p.cpuTimeSumSquaredMicros;
    return stats;
}

// Helper to calculate the compact histogram data from raw latency values for testing
std::vector<uint8_t> calculateCompactHistogramData(const std::vector<int64_t>& latencyValues) {
    std::vector<uint8_t> result;
    if (!kBinderStatsLatencyHistogram) {
        return result;
    }
    for (int64_t value : latencyValues) {
        result.push_back(android::HistogramScale::getBinIndex(value));
    }
    std::sort(result.begin(), result.end());
    return result;
}

os::binder::SingleSecondBinderStats createExpectedSingleSecondStatsWithHistogram(
        const ExpectedSingleSecondStats& p) {
    auto stats = createExpectedSingleSecondBinderStats(p);
    stats.durationBinIndices = calculateCompactHistogramData(p.rawLatencies);
    return stats;
}

MATCHER_P(SpamStatsEq, expectedStats, "") {
    return arg.clientUid == expectedStats.clientUid &&
            arg.interfaceDescriptor == expectedStats.interfaceDescriptor &&
            arg.aidlMethod == expectedStats.aidlMethod &&
            arg.secondsWithAtLeast125Calls == expectedStats.secondsWithAtLeast125Calls &&
            arg.secondsWithAtLeast250Calls == expectedStats.secondsWithAtLeast250Calls &&
            arg.peakCallCountPerSecond == expectedStats.peakCallCountPerSecond;
}

MATCHER_P(CallStatsEq, expectedStats, "") {
    return arg.clientUid == expectedStats.clientUid &&
            arg.interfaceDescriptor == expectedStats.interfaceDescriptor &&
            arg.aidlMethod == expectedStats.aidlMethod &&
            arg.callCount == expectedStats.callCount &&
            arg.durationSumMicros == expectedStats.durationSumMicros &&
            arg.secondsWithAtLeast10Calls == expectedStats.secondsWithAtLeast10Calls &&
            arg.secondsWithAtLeast50Calls == expectedStats.secondsWithAtLeast50Calls;
}

MATCHER_P(CallStatsWithCpuEq, expectedStats, "") {
    return arg.clientUid == expectedStats.clientUid &&
            arg.interfaceDescriptor == expectedStats.interfaceDescriptor &&
            arg.aidlMethod == expectedStats.aidlMethod &&
            arg.callCount == expectedStats.callCount &&
            arg.durationSumMicros == expectedStats.durationSumMicros &&
            arg.secondsWithAtLeast10Calls == expectedStats.secondsWithAtLeast10Calls &&
            arg.secondsWithAtLeast50Calls == expectedStats.secondsWithAtLeast50Calls &&
            arg.cpuTimeCount == expectedStats.cpuTimeCount &&
            arg.cpuTimeSumMicros == expectedStats.cpuTimeSumMicros &&
            arg.cpuTimeSumSquaredMicros == expectedStats.cpuTimeSumSquaredMicros;
}

MATCHER_P(SingleSecondBinderStatsEq, expectedStats, "") {
    auto actualHistogram = arg.durationBinIndices;
    auto expectedHistogram = expectedStats.durationBinIndices;
    std::sort(actualHistogram.begin(), actualHistogram.end());
    std::sort(expectedHistogram.begin(), expectedHistogram.end());

    return arg.clientUid == expectedStats.clientUid &&
            arg.interfaceDescriptor == expectedStats.interfaceDescriptor &&
            arg.aidlMethod == expectedStats.aidlMethod &&
            arg.durationCount == expectedStats.durationCount &&
            arg.callCount == expectedStats.callCount &&
            arg.durationMicrosSum == expectedStats.durationMicrosSum &&
            arg.durationMicrosSquaredSum == expectedStats.durationMicrosSquaredSum &&
            arg.cpuTimeCount == expectedStats.cpuTimeCount &&
            arg.cpuTimeMicrosSum == expectedStats.cpuTimeMicrosSum &&
            arg.cpuTimeMicrosSquaredSum == expectedStats.cpuTimeMicrosSquaredSum &&
            actualHistogram == expectedHistogram;
}

class BinderStatsPusherHelper {
public:
    static void doPush(BinderStatsPusher& pusher, std::vector<BinderCallData> data,
                       const int64_t nowSec) {
        auto addCallDataFn = pusher.getAddCallDataToBufferLockedFunction();
        for (auto& datum : data) {
            addCallDataFn(std::move(datum));
        }
#if defined(LIBBINDER_BINDER_OBSERVER_V2)
        // For V2, aggregation is triggered by the data stream.
        // To trigger processing of the pushed data, we need to add data points
        // for subsequent seconds. We add two points to be sure to flush the
        // previous second buffer.
        int64_t maxEndTimeSec = 0;
        if (!data.empty()) {
            for (const auto& d : data) {
                if (d.endTimeNanos > 0) {
                    maxEndTimeSec =
                            std::max((long long)maxEndTimeSec, d.endTimeNanos / 1000'000'000LL);
                }
            }
        }

        if (maxEndTimeSec == 0) {
            // Fallback for tests that don't set endTimeNanos
            maxEndTimeSec = nowSec + 1;
        }

        BinderCallData flush_datum1 = {.endTimeNanos = (maxEndTimeSec + 1) * 1000'000'000LL};
        addCallDataFn(std::move(flush_datum1));
        BinderCallData flush_datum2 = {.endTimeNanos = (maxEndTimeSec + 2) * 1000'000'000LL};
        addCallDataFn(std::move(flush_datum2));

#else
        pusher.pushLocked(nowSec);
#endif
    }
};
class BinderStatsPusherTest : public Test {
protected:
    sp<NiceMock<MockBinderStatsConsumerService>> mockStatsService;
    sp<NiceMock<MockServiceManager>> mockServiceManager;
    BinderStatsPusher pusher;
    void SetUp() override {
        mockStatsService = sp<NiceMock<MockBinderStatsConsumerService>>::make();
        initServiceManagerOnce();
        mockServiceManager = sp<NiceMock<MockServiceManager>>::cast(defaultServiceManager());
        ASSERT_NE(mockServiceManager, nullptr)
                << "Default service manager is not the expected mock type";
        // Default behavior: Service Manager returns the mock Stats Service
        ON_CALL(*mockServiceManager.get(), checkService(String16("binder_stats_consumer")))
                .WillByDefault(Return(IInterface::asBinder(mockStatsService)));
        ON_CALL(*mockStatsService.get(), localBinder()).WillByDefault(Return(nullptr));
    }
    void TearDown() override { testing::Mock::VerifyAndClear(defaultServiceManager().get()); }
};
// --- Test Cases ---

/**
 * Unit Test
 */
TEST_F(BinderStatsPusherTest, GetBinderStatsService) {
    EXPECT_CALL(*mockServiceManager, checkService(String16("binder_stats_consumer")))
            .Times(1)
            .WillOnce(Return(IInterface::asBinder(mockStatsService)));
    auto service = pusher.getBinderStatsServiceLocked(15);
    ASSERT_EQ(service, mockStatsService);
}

TEST_F(BinderStatsPusherTest, AggregateSpamNoSpamBelowThreshold) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Spam stats are not supported with V2 observer";
    }
    std::vector<BinderCallData> data;
    int64_t currentTimeSec = 14;
    // Create data within the delay window (kDelaySeconds = 2)
    for (int i = 0; i < 50; ++i) { // Less than kMinSpamCount (125)
        data.push_back(BinderCallData{
                .startTimeNanos = (currentTimeSec - 5) * 1000'000'000LL,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IFoo"),
                .aidlMethodName = String16("MethodName1"),
                .transactionCode = 1,
                .senderUid = 1001,
        });
    }
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateSpamOneSecondSpam) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Spam stats are not supported with V2 observer";
    }

    std::vector<BinderCallData> data;
    int64_t currentTimeNanos = 9'100'000'000;
    int32_t totalCalls = 150;
    // Create enough data in the *same second* to trigger spam, far enough in the past
    for (int i = 0; i < totalCalls; ++i) { // More than kMinSpamCount (125)
        data.push_back(BinderCallData{
                .startTimeNanos = currentTimeNanos - 8000'000'000,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IFoo"),
                .aidlMethodName = String16("MethodName2"),
                .transactionCode = 1,
                .senderUid = 1001,
        });
    }
    auto expectedStats =
            createExpectedSpamStats({.clientUid = data[0].senderUid,
                                     .interfaceDescriptor = data[0].interfaceDescriptor,
                                     .aidlMethod = data[0].aidlMethodName,
                                     .secondsWithAtLeast125Calls = 1,
                                     .peakCallCountPerSecond = totalCalls});
    EXPECT_CALL(*mockStatsService, reportSpamStats(ElementsAre(SpamStatsEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeNanos / 1000'000'000);
}

TEST_F(BinderStatsPusherTest, AggregateSpamDelayedSpam) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Spam stats are not supported with V2 observer";
    }
    std::vector<BinderCallData> data;
    int64_t currentTimeNanos = 9'100'000'000;
    // Create spam data within the delay window (kDelaySeconds = 2)
    for (int i = 0; i < 150; ++i) {
        data.push_back(BinderCallData{
                .startTimeNanos = currentTimeNanos - 1000'000'000,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IBar"),
                .aidlMethodName = String16("MethodName2"),
                .transactionCode = 2,
                .senderUid = 1002,
        });
    }
    // Expect no calls, data is delayed
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeNanos / 1000'000'000);
}

TEST_F(BinderStatsPusherTest, AggregateSpamMixedOlderAndDelayed) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Binder Observer V2 is enabled";
    }

    std::vector<BinderCallData> data;
    int64_t currentTimeNanos = 9'100'000'000;
    int32_t totalCalls = 130;
    for (int i = 0; i < totalCalls; ++i) {
        data.push_back(BinderCallData{
                .startTimeNanos = currentTimeNanos - 8000'000'000,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IBaz"),
                .aidlMethodName = String16("MethodName3"),
                .transactionCode = 3,
                .senderUid = 1003,
        });
    }
    // Delayed spam data (within kDelaySeconds)
    for (int i = 0; i < 140; ++i) {
        data.push_back(BinderCallData{
                .startTimeNanos = currentTimeNanos - 1000'000'000,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IQux"),
                .aidlMethodName = String16("MethodName4"),
                .transactionCode = 4,
                .senderUid = 1004,
        });
    }
    // Expect immediate spam to be reported now
    auto expectedImmediateStats =
            createExpectedSpamStats({.clientUid = data[0].senderUid,
                                     .interfaceDescriptor = data[0].interfaceDescriptor,
                                     .aidlMethod = data[0].aidlMethodName,
                                     .secondsWithAtLeast125Calls = 1,
                                     .peakCallCountPerSecond = totalCalls});
    EXPECT_CALL(*mockStatsService,
                reportSpamStats(ElementsAre(SpamStatsEq(expectedImmediateStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeNanos / 1000'000'000);
}

TEST_F(BinderStatsPusherTest, AggregateSpamSecondWatermark) {
    std::vector<BinderCallData> data;
    int64_t spamTimeNanos = 2'000'000'000LL;
    int64_t currentTimeSec = (spamTimeNanos / 1000'000'000LL) + kSpamAggregationWindowSec + 1;
    int32_t totalCalls = 300;
    // Create data exceeding the second watermark (250 calls/sec)
    std::vector<int64_t> rawLatencies;
    for (int i = 0; i < totalCalls; ++i) {
        data.push_back(BinderCallData{
                .startTimeNanos = spamTimeNanos,
                .endTimeNanos = spamTimeNanos + 1,
                .interfaceDescriptor = String16("IHighVolume"),
                .aidlMethodName = String16("MethodName5"),
                .transactionCode = 5,
                .senderUid = 1005,
        });
        rawLatencies.push_back(1);
    }
    if (kBinderObserverV2Enabled) {
        auto expectedStats = createExpectedSingleSecondStatsWithHistogram(
                {.clientUid = data[0].senderUid,
                 .interfaceDescriptor = data[0].interfaceDescriptor,
                 .aidlMethod = data[0].aidlMethodName,
                 .callCount = totalCalls,
                 .rawLatencies = rawLatencies});
        EXPECT_CALL(*mockStatsService,
                    reportSecondGranularityStats(
                            UnorderedElementsAre(SingleSecondBinderStatsEq(expectedStats))))
                .Times(1);
        EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
        EXPECT_CALL(*mockStatsService, localBinder()).Times(1);
    } else {
        auto expectedStats =
                createExpectedSpamStats({.clientUid = data[0].senderUid,
                                         .interfaceDescriptor = data[0].interfaceDescriptor,
                                         .aidlMethod = data[0].aidlMethodName,
                                         .secondsWithAtLeast125Calls = 1,
                                         .secondsWithAtLeast250Calls = 1,
                                         .peakCallCountPerSecond = totalCalls});
        EXPECT_CALL(*mockStatsService, reportSpamStats(ElementsAre(SpamStatsEq(expectedStats))))
                .Times(1);
        EXPECT_CALL(*mockStatsService, localBinder()).Times(1);
    }

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateSpamAcrossMultipleSeconds) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Spam stats across multiple seconds aren't supported with V2 observer";
    }
    std::vector<BinderCallData> data;
    int64_t firstSpamSecondNanos = 2'000'000'000LL;  // 2s
    int64_t secondSpamSecondNanos = 3'000'000'000LL; // 3s
    int64_t currentTimeSec = (secondSpamSecondNanos / 1000'000'000LL) + kSpamAggregationWindowSec +
            1; // 3 + 5 + 1 = 9s
    // Spam for the first second
    int32_t totalCalls1 = 150;
    for (int i = 0; i < totalCalls1; ++i) {
        data.push_back(BinderCallData{
                .startTimeNanos = firstSpamSecondNanos,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IMultiSecond"),
                .aidlMethodName = String16("MethodName6"),
                .transactionCode = 6,
                .senderUid = 1006,
        });
    }
    // Spam for the second second
    int32_t totalCalls2 = 160;
    for (int i = 0; i < totalCalls2; ++i) {
        // Use the same UID, code, desc for aggregation
        data.push_back(BinderCallData{
                .startTimeNanos = secondSpamSecondNanos,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IMultiSecond"),
                .aidlMethodName = String16("MethodName6"),
                .transactionCode = 6,
                .senderUid = 1006,
        });
    }

    if (kBinderObserverV2Enabled) {
        auto expectedStats1 =
                createExpectedSpamStats({.clientUid = data[0].senderUid,
                                         .interfaceDescriptor = data[0].interfaceDescriptor,
                                         .aidlMethod = data[0].aidlMethodName,
                                         .secondsWithAtLeast125Calls = 1,
                                         .peakCallCountPerSecond = totalCalls1});
        auto expectedStats2 =
                createExpectedSpamStats({.clientUid = data[0].senderUid,
                                         .interfaceDescriptor = data[0].interfaceDescriptor,
                                         .aidlMethod = data[0].aidlMethodName,
                                         .secondsWithAtLeast125Calls = 1,
                                         .peakCallCountPerSecond = totalCalls2});
        EXPECT_CALL(*mockStatsService,
                    reportSpamStats(UnorderedElementsAre(SpamStatsEq(expectedStats1),
                                                         SpamStatsEq(expectedStats2))))
                .Times(1);
    } else {
        // Expect one atom representing spam across 2 seconds
        auto expectedStats = createExpectedSpamStats(
                {.clientUid = data[0].senderUid,
                 .interfaceDescriptor = data[0].interfaceDescriptor,
                 .aidlMethod = data[0].aidlMethodName,
                 .secondsWithAtLeast125Calls = 2,
                 .peakCallCountPerSecond = std::max(totalCalls1, totalCalls2)});
        EXPECT_CALL(*mockStatsService, reportSpamStats(ElementsAre(SpamStatsEq(expectedStats))))
                .Times(1);
    }
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateSpamProcessesDelayedDataOnSubsequentCall) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Binder Observer V2 is enabled";
    }

    std::vector<BinderCallData> callData1;
    int64_t callTimeSec1 = 10;
    int64_t spamDataTimeNanos = callTimeSec1 * 1000'000'000LL;
    int32_t totalCalls = 150;
    for (int i = 0; i < totalCalls; ++i) {
        callData1.push_back(BinderCallData{
                .startTimeNanos = spamDataTimeNanos,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("IDelayed"),
                .aidlMethodName = String16("MethodName7"),
                .transactionCode = 7,
                .senderUid = 1007,
        });
    }
    auto expectedStats =
            createExpectedSpamStats({.clientUid = callData1[0].senderUid,
                                     .interfaceDescriptor = callData1[0].interfaceDescriptor,
                                     .aidlMethod = callData1[0].aidlMethodName,
                                     .secondsWithAtLeast125Calls = 1,
                                     .peakCallCountPerSecond = totalCalls});
    // First push: data should be buffered as it's too recent
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1); // For the first push

    BinderStatsPusherHelper::doPush(pusher, callData1, callTimeSec1);

    // Second push: advance time so the previous data is now outside the aggregation window
    std::vector<BinderCallData> callData2; // Can be empty or contain new data
    int64_t call2_time_sec = callTimeSec1 + kSpamAggregationWindowSec + 1; // 10 + 5 + 1 = 16s
    EXPECT_CALL(*mockStatsService, reportSpamStats(ElementsAre(SpamStatsEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1); // For the second push

    BinderStatsPusherHelper::doPush(pusher, callData2, call2_time_sec);
}

TEST_F(BinderStatsPusherTest, AggregateSpamForDifferentMethodsSimultaneously) {
    std::vector<BinderCallData> data;
    int64_t spamTimeNanos = 4'000'000'000LL; // 4s
    int64_t currentTimeSec =
            (spamTimeNanos / 1000'000'000LL) + kSpamAggregationWindowSec + 1; // 4 + 5 + 1 = 10s
    // Spam for method 1
    int32_t totalCalls1 = 200;
    std::vector<int64_t> rawLatencies1;
    int64_t totalDurationMicros1 = 0;
    int64_t durationSumSquaredMicros1 = 0;

    for (int i = 0; i < totalCalls1; ++i) {
        int64_t durNanos = i * 1000;
        int64_t durMicros = durNanos / 1000;
        data.push_back({.startTimeNanos = spamTimeNanos,
                        .endTimeNanos = spamTimeNanos + durNanos,
                        .interfaceDescriptor = String16("IMultiMethod"),
                        .aidlMethodName = String16("MethodName9"),
                        .transactionCode = 9,
                        .senderUid = 1008});
        rawLatencies1.push_back(durNanos);
        totalDurationMicros1 += durMicros;
        durationSumSquaredMicros1 += durMicros * durMicros;
    }

    // Spam for method 2
    int32_t totalCalls2 = 220;
    std::vector<int64_t> rawLatencies2;
    int64_t totalDurationMicros2 = 0;
    int64_t durationSumSquaredMicros2 = 0;

    for (int i = 0; i < totalCalls2; ++i) {
        int64_t durNanos = i * 1000 + 3 * i;
        int64_t durMicros = durNanos / 1000;

        data.push_back({.startTimeNanos = spamTimeNanos,
                        .endTimeNanos = spamTimeNanos + durNanos,
                        .interfaceDescriptor = String16("IMultiMethod"),
                        .aidlMethodName = String16("MethodName8"),
                        .transactionCode = 8,
                        .senderUid = 1008});
        rawLatencies2.push_back(durNanos);
        totalDurationMicros2 += durMicros;
        durationSumSquaredMicros2 += durMicros * durMicros;
    }

    if (kBinderObserverV2Enabled) {
        auto expectedStats1 = createExpectedSingleSecondStatsWithHistogram(
                {.clientUid = 1008,
                 .interfaceDescriptor = String16("IMultiMethod"),
                 .aidlMethod = String16("MethodName9"),
                 .callCount = totalCalls1,
                 .durationSumMicros = totalDurationMicros1,
                 .callDurationSumSquaredMicros = durationSumSquaredMicros1,
                 .rawLatencies = rawLatencies1});
        auto expectedStats2 = createExpectedSingleSecondStatsWithHistogram(
                {.clientUid = 1008,
                 .interfaceDescriptor = String16("IMultiMethod"),
                 .aidlMethod = String16("MethodName8"),
                 .callCount = totalCalls2,
                 .durationSumMicros = totalDurationMicros2,
                 .callDurationSumSquaredMicros = durationSumSquaredMicros2,
                 .rawLatencies = rawLatencies2});
        EXPECT_CALL(*mockStatsService,
                    reportSecondGranularityStats(
                            UnorderedElementsAre(SingleSecondBinderStatsEq(expectedStats1),
                                                 SingleSecondBinderStatsEq(expectedStats2))))
                .Times(1);
    } else {
        auto expectedStats1 =
                createExpectedSpamStats({.clientUid = data[0].senderUid,
                                         .interfaceDescriptor = data[0].interfaceDescriptor,
                                         .aidlMethod = data[0].aidlMethodName,
                                         .secondsWithAtLeast125Calls = 1,
                                         .peakCallCountPerSecond = totalCalls1});
        auto expectedStats2 = createExpectedSpamStats(
                {.clientUid = data[totalCalls1].senderUid,
                 .interfaceDescriptor = data[totalCalls1].interfaceDescriptor,
                 .aidlMethod = data[totalCalls1].aidlMethodName,
                 .secondsWithAtLeast125Calls = 1,
                 .peakCallCountPerSecond = totalCalls2});
        EXPECT_CALL(*mockStatsService,
                    reportSpamStats(UnorderedElementsAre(SpamStatsEq(expectedStats1),
                                                         SpamStatsEq(expectedStats2))))
                .Times(1);
    }
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, SkipPushForLocalBinderWithoutJvm) {
    // Simulate a local binder service
    sp<BBinder> localBinderInstance = sp<BBinder>::make();
    ON_CALL(*mockStatsService.get(), localBinder())
            .WillByDefault(Return(localBinderInstance.get()));
    // getJavaVM() is expected to return nullptr in the test environment (JvmUtils.h)
    std::vector<BinderCallData> data;
    int64_t spamTimeNanos = 2'000'000'000LL;                                                   // 2s
    int64_t currentTimeSec = (spamTimeNanos / 1000'000'000LL) + kSpamAggregationWindowSec + 1; // 8s
    for (int i = 0; i < 150; ++i) {
        data.push_back(BinderCallData{
                .startTimeNanos = spamTimeNanos,
                .endTimeNanos = 0,
                .interfaceDescriptor = String16("ILocalSkipped"),
                .aidlMethodName = String16("MethodName10"),
                .transactionCode = 10,
                .senderUid = 1009,
        });
    }
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, DataNotDroppedWhenPushIsSkippedThenSucceeds) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Not supported for Binder Observer V2";
    }
    std::vector<BinderCallData> latencyCallData1;
    int64_t timeSec1 = 20;
    // Data old enough to be processed immediately
    int64_t latencyDataTimeNanos = (timeSec1 - kLatencyAggregationWindowSec) * 1000'000'000LL;
    int64_t totalDurationMicros = 0;
    int64_t durationSumSquaredMicros = 0;
    int32_t totalCalls = 15; // More than kLatencyCountFirstWatermark
    for (int i = 0; i < totalCalls; ++i) {
        int64_t durationMicros = (i + 1) * 100;
        latencyCallData1.push_back(BinderCallData{
                .startTimeNanos = latencyDataTimeNanos,
                .endTimeNanos = latencyDataTimeNanos + durationMicros * 1000,
                .interfaceDescriptor = String16("IServiceSkipped"),
                .aidlMethodName = String16("MethodName11"),
                .transactionCode = 11,
                .senderUid = 1010,
        });
        totalDurationMicros += durationMicros;
        durationSumSquaredMicros += durationMicros * durationMicros;
    }
    auto expectedStats =
            createExpectedCallStats({.clientUid = latencyCallData1[0].senderUid,
                                     .interfaceDescriptor = latencyCallData1[0].interfaceDescriptor,
                                     .aidlMethod = latencyCallData1[0].aidlMethodName,
                                     .callCount = totalCalls,
                                     .durationSumMicros = totalDurationMicros,
                                     .callDurationSumSquaredMicros = durationSumSquaredMicros,
                                     .secondsWithAtLeast10Calls = 1});
    // First push: Service is unavailable
    EXPECT_CALL(*mockServiceManager, checkService(String16("binder_stats_consumer")))
            .WillOnce(Return(nullptr));
    // localBinder() shouldn't be called if service is null
    EXPECT_CALL(*mockStatsService, localBinder()).Times(0);
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportSecondGranularityStats(_)).Times(0);

    BinderStatsPusherHelper::doPush(pusher, latencyCallData1, timeSec1);

    Mock::VerifyAndClearExpectations(mockServiceManager.get());
    Mock::VerifyAndClearExpectations(mockStatsService.get());

    // Second push: Service becomes available. Advance time beyond service check timeout.
    if (kBinderObserverV2Enabled) {
        int64_t timeSec2 = timeSec1 + 1;
        auto expectedV2Stats = createExpectedSingleSecondStatsWithHistogram(
                {.clientUid = latencyCallData1[0].senderUid,
                 .interfaceDescriptor = latencyCallData1[0].interfaceDescriptor,
                 .aidlMethod = String16(""),
                 .callCount = 15,
                 .durationSumMicros = totalDurationMicros,
                 .callDurationSumSquaredMicros = durationSumSquaredMicros});
        EXPECT_CALL(*mockServiceManager, checkService(String16("binder_stats_consumer")))
                .WillOnce(Return(IInterface::asBinder(mockStatsService)));
        EXPECT_CALL(*mockStatsService, localBinder()).Times(1);
        EXPECT_CALL(*mockStatsService,
                    reportSecondGranularityStats(
                            UnorderedElementsAre(SingleSecondBinderStatsEq(expectedV2Stats))))
                .Times(1);
        BinderStatsPusherHelper::doPush(pusher, {}, timeSec2);
    } else {
        int64_t timeSec2 = timeSec1 + 6;
        EXPECT_CALL(*mockServiceManager, checkService(String16("binder_stats_consumer")))
                .WillOnce(Return(IInterface::asBinder(mockStatsService)));
        EXPECT_CALL(*mockStatsService, localBinder()).Times(1); // Called when service is not null
        EXPECT_CALL(*mockStatsService,
                    reportCallStats(ElementsAre(CallStatsWithCpuEq(expectedStats))))
                .Times(1);
        EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
        pusher.pushLocked(timeSec2);
    }
}

TEST_F(BinderStatsPusherTest, sizeOfStruct) {
#ifdef __LP64__
    EXPECT_EQ(sizeof(android::BinderCallData), 48);
#else
    EXPECT_EQ(sizeof(android::BinderCallData), 36);
#endif
}

// --- Latency Tests ---
TEST_F(BinderStatsPusherTest, AggregateLatencyNoLatencyBelowThreshold) {
    std::vector<BinderCallData> data;
    int64_t currentTimeSec = 14;
    // Create data within the delay window
    for (int i = 0; i < 5; ++i) { // Less than kLatencyCountFirstWatermark (10)
        data.push_back(BinderCallData{
                .startTimeNanos =
                        (currentTimeSec - kLatencyAggregationWindowSec + 1) * 1000'000'000LL,
                .endTimeNanos =
                        (currentTimeSec - kLatencyAggregationWindowSec + 1) * 1000'000'000LL +
                        1000000 /* 1ms */,
                .interfaceDescriptor = String16("ILatencyFoo"),
                .aidlMethodName = String16("MethodName1"),
                .transactionCode = 1,
                .senderUid = 2001,
        });
    }
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyOneSecondLatency) {
    std::vector<BinderCallData> data;
    int64_t callTimeNanos = 2'000'000'000LL; // 2s
    int64_t currentTimeSec =
            (callTimeNanos / 1000'000'000LL) + kLatencyAggregationWindowSec + 1; // 2 + 5 + 1 = 8s
    int64_t totalDurationMicros = 0;
    int64_t durationSumSquaredMicros = 0;
    // Create enough data in the *same second* to trigger latency reporting
    for (int i = 0; i < 15; ++i) {              // More than kLatencyCountFirstWatermark (10)
        int64_t durationMicros = (i + 1) * 100; // e.g. 100us, 200us ..
        data.push_back(BinderCallData{
                .startTimeNanos = callTimeNanos,
                .endTimeNanos = callTimeNanos + durationMicros * 1000,
                .interfaceDescriptor = String16("ILatencyBar"),
                .aidlMethodName = String16("MethodName2"),
                .transactionCode = 2,
                .senderUid = 2002,
        });
        totalDurationMicros += durationMicros;
        durationSumSquaredMicros += durationMicros * durationMicros;
    }

    if (kBinderObserverV2Enabled) {
        std::vector<int64_t> rawLatencies;
        for (int i = 0; i < 15; ++i) {
            rawLatencies.push_back((i + 1) * 100 * 1000); // Nanos
        }
        auto expectedStats = createExpectedSingleSecondStatsWithHistogram(
                {.clientUid = data[0].senderUid,
                 .interfaceDescriptor = data[0].interfaceDescriptor,
                 .aidlMethod = data[0].aidlMethodName,
                 .callCount = 15,
                 .durationSumMicros = totalDurationMicros,
                 .callDurationSumSquaredMicros = durationSumSquaredMicros,
                 .rawLatencies = rawLatencies});
        EXPECT_CALL(*mockStatsService,
                    reportSecondGranularityStats(
                            UnorderedElementsAre(SingleSecondBinderStatsEq(expectedStats))))
                .Times(1);
    } else {
        auto expectedStats =
                createExpectedCallStats({.clientUid = data[0].senderUid,
                                         .interfaceDescriptor = data[0].interfaceDescriptor,
                                         .aidlMethod = data[0].aidlMethodName,
                                         .callCount = 15,
                                         .durationSumMicros = totalDurationMicros,
                                         .callDurationSumSquaredMicros = durationSumSquaredMicros,
                                         .secondsWithAtLeast10Calls = 1});
        EXPECT_CALL(*mockStatsService,
                    reportCallStats(ElementsAre(CallStatsWithCpuEq(expectedStats))))
                .Times(1);
    }
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyDelayedLatency) {
    std::vector<BinderCallData> data;
    int64_t currentTimeNanos = 9'100'000'000;
    // Create latency data within the aggregation window
    for (int i = 0; i < 15; ++i) {
        data.push_back(BinderCallData{
                .startTimeNanos = currentTimeNanos - 1000'000'000,
                .endTimeNanos = currentTimeNanos - 1000'000'000 + 500000 /* 0.5ms */,
                .interfaceDescriptor = String16("ILatencyBaz"),
                .aidlMethodName = String16("MethodName3"),
                .transactionCode = 3,
                .senderUid = 2003,
        });
    }
    // Expect no calls, data is delayed
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeNanos / 1000'000'000);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyProcessesDelayedDataOnSubsequentCall) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Binder Observer V2 is enabled";
    }
    std::vector<BinderCallData> callData1;
    int64_t callTimeSec1 = 10;
    int64_t latencyDataTimeNanos = callTimeSec1 * 1000'000'000LL; // 10s, will be delayed
    int64_t totalDurationMicros1 = 0;
    int64_t durationSumSquaredMicros1 = 0;
    for (int i = 0; i < 15; ++i) {
        int64_t durationMicros = (i + 1) * 150;
        callData1.push_back(BinderCallData{
                .startTimeNanos = latencyDataTimeNanos,
                .endTimeNanos = latencyDataTimeNanos + durationMicros * 1000,
                .interfaceDescriptor = String16("ILatencyDelayed"),
                .aidlMethodName = String16("MethodName4"),
                .transactionCode = 4,
                .senderUid = 2004,
        });
        totalDurationMicros1 += durationMicros;
        durationSumSquaredMicros1 += durationMicros * durationMicros;
    }

    // First push: data should be buffered as it's too recent
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, callData1, callTimeSec1);

    // Second push: advance time so the previous data is now outside the aggregation window
    std::vector<BinderCallData> callData2;                                    // Can be empty
    int64_t call2_time_sec = callTimeSec1 + kLatencyAggregationWindowSec + 1; // 10 + 5 + 1 = 16s

    auto expectedStats =
            createExpectedCallStats({.clientUid = callData1[0].senderUid,
                                     .interfaceDescriptor = callData1[0].interfaceDescriptor,
                                     .aidlMethod = callData1[0].aidlMethodName,
                                     .callCount = 15,
                                     .durationSumMicros = totalDurationMicros1,
                                     .callDurationSumSquaredMicros = durationSumSquaredMicros1,
                                     .secondsWithAtLeast10Calls = 1});
    EXPECT_CALL(*mockStatsService, reportCallStats(ElementsAre(CallStatsWithCpuEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, callData2, call2_time_sec);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyWithCpu) {
    std::vector<BinderCallData> data;
    int64_t callTimeNanos = 2'000'000'000LL; // 2s
    int64_t currentTimeSec =
            (callTimeNanos / 1000'000'000LL) + kLatencyAggregationWindowSec + 1; // 2 + 5 + 1 = 8s
    int64_t totalDurationMicros = 0;
    int64_t durationSumSquaredMicros = 0;
    int64_t totalCpuTimeMicros = 0;
    int64_t totalcpuTimeSumSquaredMicros = 0;
    // Create enough data in the *same second* to trigger latency reporting
    for (int i = 0; i < 15; ++i) {              // More than kLatencyCountFirstWatermark (10)
        int64_t durationMicros = (i + 1) * 100; // e.g. 100us, 200us ..
        int64_t cpuTimeMicros = (i + 1) * 50;
        data.push_back(BinderCallData{
                .startTimeNanos = callTimeNanos,
                .endTimeNanos = callTimeNanos + durationMicros * 1000,
                .cpuTimeNanos = cpuTimeMicros * 1000,
                .interfaceDescriptor = String16("ILatencyCpu"),
                .aidlMethodName = String16("MethodName2"),
                .transactionCode = 2,
                .senderUid = 2002,
        });
        totalDurationMicros += durationMicros;
        durationSumSquaredMicros += durationMicros * durationMicros;
        totalCpuTimeMicros += cpuTimeMicros;
        totalcpuTimeSumSquaredMicros += cpuTimeMicros * cpuTimeMicros;
    }
    if (kBinderObserverV2Enabled) {
        std::vector<int64_t> rawLatencies;
        for (int i = 0; i < 15; ++i) {
            rawLatencies.push_back((i + 1) * 100 * 1000); // Nanos
        }
        auto expectedStats = createExpectedSingleSecondStatsWithHistogram(
                {.clientUid = data[0].senderUid,
                 .interfaceDescriptor = data[0].interfaceDescriptor,
                 .aidlMethod = data[0].aidlMethodName,
                 .callCount = 15,
                 .durationSumMicros = totalDurationMicros,
                 .callDurationSumSquaredMicros = durationSumSquaredMicros,
                 .cpuTimeCount = 15,
                 .cpuTimeSumMicros = totalCpuTimeMicros,
                 .cpuTimeSumSquaredMicros = totalcpuTimeSumSquaredMicros,
                 .rawLatencies = rawLatencies});
        EXPECT_CALL(*mockStatsService,
                    reportSecondGranularityStats(
                            UnorderedElementsAre(SingleSecondBinderStatsEq(expectedStats))))
                .Times(1);
    } else {
        auto expectedStats =
                createExpectedCallStats({.clientUid = data[0].senderUid,
                                         .interfaceDescriptor = data[0].interfaceDescriptor,
                                         .aidlMethod = data[0].aidlMethodName,
                                         .callCount = 15,
                                         .durationSumMicros = totalDurationMicros,
                                         .callDurationSumSquaredMicros = durationSumSquaredMicros,
                                         .secondsWithAtLeast10Calls = 1,
                                         .cpuTimeCount = 15,
                                         .cpuTimeSumMicros = totalCpuTimeMicros,
                                         .cpuTimeSumSquaredMicros = totalcpuTimeSumSquaredMicros});
        EXPECT_CALL(*mockStatsService,
                    reportCallStats(ElementsAre(CallStatsWithCpuEq(expectedStats))))
                .Times(1);
    }
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyMultipleSeconds) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "This test is for Binder Observer V1 only.";
    }
    std::vector<BinderCallData> data;
    int64_t firstCallSecondNanos = 2'000'000'000LL;  // 2s
    int64_t secondCallSecondNanos = 3'000'000'000LL; // 3s
    int64_t currentTimeSec = (secondCallSecondNanos / 1000'000'000LL) +
            kLatencyAggregationWindowSec + 1; // 3 + 5 + 1 = 9s
    int64_t totalDurationMicros1 = 0;
    int64_t durationSumSquaredMicros1 = 0;
    const int totalCalls1 = 12;
    // Data for the first second
    for (int i = 0; i < totalCalls1; ++i) { // 12 calls
        int64_t durationMicros = (i + 1) * 100;
        data.push_back(BinderCallData{
                .startTimeNanos = firstCallSecondNanos,
                .endTimeNanos = firstCallSecondNanos + durationMicros * 1000,
                .interfaceDescriptor = String16("ILatencyMultiSec"),
                .aidlMethodName = String16("MethodName5"),
                .transactionCode = 5,
                .senderUid = 2005,
        });
        totalDurationMicros1 += durationMicros;
        durationSumSquaredMicros1 += durationMicros * durationMicros;
    }
    int64_t totalDurationMicros2 = 0;
    int64_t durationSumSquaredMicros2 = 0;
    const int totalCalls2 = 8;
    // Data for the second second
    for (int i = 0; i < totalCalls2; ++i) { // 8 calls
        int64_t durationMicros = (i + 1) * 120;
        // Use the same UID, code, desc for aggregation
        data.push_back(BinderCallData{
                .startTimeNanos = secondCallSecondNanos,
                .endTimeNanos = secondCallSecondNanos + durationMicros * 1000,
                .interfaceDescriptor = String16("ILatencyMultiSec"),
                .aidlMethodName = String16("MethodName5"),
                .transactionCode = 5,
                .senderUid = 2005,
        });
        totalDurationMicros2 += durationMicros;
        durationSumSquaredMicros2 += durationMicros * durationMicros;
    }
    if (kBinderObserverV2Enabled) {
        auto expectedStats1 = createExpectedSingleSecondStatsWithHistogram(
                {.clientUid = data[0].senderUid,
                 .interfaceDescriptor = data[0].interfaceDescriptor,
                 .aidlMethod = data[0].aidlMethodName,
                 .callCount = totalCalls1,
                 .durationSumMicros = totalDurationMicros1,
                 .callDurationSumSquaredMicros = durationSumSquaredMicros1});
        auto expectedStats2 = createExpectedSingleSecondStatsWithHistogram(
                {.clientUid = data[0].senderUid,
                 .interfaceDescriptor = data[0].interfaceDescriptor,
                 .aidlMethod = data[0].aidlMethodName,
                 .callCount = totalCalls2,
                 .durationSumMicros = totalDurationMicros2,
                 .callDurationSumSquaredMicros = durationSumSquaredMicros2});
        EXPECT_CALL(*mockStatsService,
                    reportSecondGranularityStats(
                            UnorderedElementsAre(SingleSecondBinderStatsEq(expectedStats1),
                                                 SingleSecondBinderStatsEq(expectedStats2))))
                .Times(1);
        EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
        EXPECT_CALL(*mockStatsService, localBinder()).Times(2);
    } else {
        // Expect one atom representing aggregated data across 2 seconds
        // Total calls = 12 + 8 = 20
        // secondsWithAtLeast10Calls = 1 (only the first second has >= 10 calls)
        // secondsWithAtLeast50Calls = 0
        auto expectedStats = createExpectedCallStats(
                {.clientUid = data[0].senderUid,
                 .interfaceDescriptor = data[0].interfaceDescriptor,
                 .aidlMethod = data[0].aidlMethodName,
                 .callCount = totalCalls1 + totalCalls2,
                 .durationSumMicros = totalDurationMicros1 + totalDurationMicros2,
                 .callDurationSumSquaredMicros =
                         durationSumSquaredMicros1 + durationSumSquaredMicros2,
                 .secondsWithAtLeast10Calls = 1});
        EXPECT_CALL(*mockStatsService, reportCallStats(ElementsAre(CallStatsEq(expectedStats))))
                .Times(1);
        EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
        EXPECT_CALL(*mockStatsService, localBinder()).Times(1);
    }

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyMultipleSecondsV2) {
    if (!kBinderObserverV2Enabled) {
        GTEST_SKIP() << "This test is for Binder Observer V2 only.";
    }

    std::vector<BinderCallData> data;
    int64_t firstCallSecondNanos = 2'100'000'000LL;  // 2.1s
    int64_t secondCallSecondNanos = 3'100'000'000LL; // 3.1s

    int64_t totalDurationMicros1 = 0;
    int64_t durationSumSquaredMicros1 = 0;

    const int totalCalls1 = 12;
    // Data for the first second
    std::vector<int64_t> rawLatencies1;
    for (int i = 0; i < totalCalls1; ++i) { // 12 calls
        int64_t durationNanos = (i + 1) * 100'000;
        data.push_back(BinderCallData{
                .startTimeNanos = firstCallSecondNanos,
                .endTimeNanos = firstCallSecondNanos + durationNanos,
                .interfaceDescriptor = String16("ILatencyMultiSec"),
                .aidlMethodName = String16("MethodName5"),
                .transactionCode = 5,
                .senderUid = 2005,
        });
        int64_t durationMicros = durationNanos / 1000;
        totalDurationMicros1 += durationMicros;
        durationSumSquaredMicros1 += durationMicros * durationMicros;
        rawLatencies1.push_back(durationNanos);
    }

    int64_t totalDurationMicros2 = 0;
    int64_t durationSumSquaredMicros2 = 0;
    const int totalCalls2 = 8;
    // Data for the second second
    std::vector<int64_t> rawLatencies2;
    for (int i = 0; i < totalCalls2; ++i) { // 8 calls
        int64_t durationNanos = (i + 1) * 120'000;
        data.push_back(BinderCallData{
                .startTimeNanos = secondCallSecondNanos,
                .endTimeNanos = secondCallSecondNanos + durationNanos,
                .interfaceDescriptor = String16("ILatencyMultiSec"),
                .aidlMethodName = String16("MethodName5"),
                .transactionCode = 5,
                .senderUid = 2005,
        });
        int64_t durationMicros = durationNanos / 1000;
        rawLatencies2.push_back(durationNanos);
        totalDurationMicros2 += durationMicros;
        durationSumSquaredMicros2 += durationMicros * durationMicros;
    }

    // Expect two separate reports for each second.
    auto expectedStats1 = createExpectedSingleSecondStatsWithHistogram(
            {.clientUid = data[0].senderUid,
             .interfaceDescriptor = data[0].interfaceDescriptor,
             .aidlMethod = data[0].aidlMethodName,
             .callCount = totalCalls1,
             .durationSumMicros = totalDurationMicros1,
             .callDurationSumSquaredMicros = durationSumSquaredMicros1,
             .rawLatencies = rawLatencies1});
    auto expectedStats2 = createExpectedSingleSecondStatsWithHistogram(
            {.clientUid = data[0].senderUid,
             .interfaceDescriptor = data[0].interfaceDescriptor,
             .aidlMethod = data[0].aidlMethodName,
             .callCount = totalCalls2,
             .durationSumMicros = totalDurationMicros2,
             .callDurationSumSquaredMicros = durationSumSquaredMicros2,
             .rawLatencies = rawLatencies2});

    EXPECT_CALL(*mockStatsService,
                reportSecondGranularityStats(
                        UnorderedElementsAre(SingleSecondBinderStatsEq(expectedStats1))));
    EXPECT_CALL(*mockStatsService,
                reportSecondGranularityStats(
                        UnorderedElementsAre(SingleSecondBinderStatsEq(expectedStats2))));

    EXPECT_CALL(*mockStatsService, reportCallStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(2);

    BinderStatsPusherHelper::doPush(pusher, data, 0 /* nowSec not used */);
}

TEST_F(BinderStatsPusherTest, AggregateLatencyWithHistogramV2) {
    std::vector<BinderCallData> data;
    int64_t callTimeNanos = 2'000'000'000LL; // 2s
    // Current time far enough in the future to trigger aggregation
    int64_t currentTimeSec = (callTimeNanos / 1000'000'000LL) + kLatencyAggregationWindowSec + 1;

    std::vector<int64_t> rawLatencies = {10'000,   20'000,   100'000, 1500'000,
                                         2000'000, 2500'000, 5000'000}; // Latencies in nanos
    int64_t totalDurationMicros = 0;
    int64_t callDurationSumSquaredMicros = 0;

    for (int64_t durationNanos : rawLatencies) {
        int64_t durationMicros = durationNanos / 1000;
        data.push_back(BinderCallData{
                .startTimeNanos = callTimeNanos,
                .endTimeNanos = callTimeNanos + durationMicros * 1000,
                .interfaceDescriptor = String16("ILatencyHistogram"),
                .aidlMethodName = String16("MethodNameHistogram"),
                .transactionCode = 100,
                .senderUid = 3000,
        });
        totalDurationMicros += durationMicros;
        callDurationSumSquaredMicros += durationMicros * durationMicros;
    }

    // Add enough calls to trigger secondsWithAtLeast10Calls, but not 50
    for (int i = 0; i < 5; ++i) { // Total 7 (from rawLatencies) + 5 = 12 calls
        int64_t durationNanos = 500'000 + i * 1000;
        data.push_back(BinderCallData{
                .startTimeNanos = callTimeNanos,
                .endTimeNanos = callTimeNanos + durationNanos,
                .interfaceDescriptor = String16("ILatencyHistogram"),
                .aidlMethodName = String16("MethodNameHistogram"),
                .transactionCode = 100,
                .senderUid = 3000,
        });
        int64_t durationMicros = durationNanos / 1000;
        totalDurationMicros += durationMicros;
        callDurationSumSquaredMicros += durationMicros * durationMicros;
        rawLatencies.push_back(durationNanos);
    }

    auto expectedStats = createExpectedSingleSecondStatsWithHistogram(
            {.clientUid = data[0].senderUid,
             .interfaceDescriptor = data[0].interfaceDescriptor,
             .aidlMethod = data[0].aidlMethodName,
             .callCount = static_cast<int64_t>(data.size()),
             .durationSumMicros = totalDurationMicros,
             .callDurationSumSquaredMicros = callDurationSumSquaredMicros,
             .rawLatencies = rawLatencies});

    EXPECT_CALL(*mockStatsService,
                reportSecondGranularityStats(
                        UnorderedElementsAre(SingleSecondBinderStatsEq(expectedStats))))
            .Times(1);
    EXPECT_CALL(*mockStatsService, reportSpamStats(_)).Times(0);
    EXPECT_CALL(*mockStatsService, localBinder()).Times(1);

    BinderStatsPusherHelper::doPush(pusher, data, currentTimeSec);
}
