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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <utils/Errors.h>
#include <codecvt>
#include <locale>
#include <map>

#include "../BuildFlags.h"
#include "../observer/BinderObserver.h"
#include "../observer/BinderObserverConfig.h"

namespace android {

using ::testing::_;
using ::testing::Return;

class BinderObserverConfigTest : public ::testing::Test {
protected:
    class MockEnvironment : public BinderObserverConfig::Environment {
    public:
        MOCK_METHOD(bool, fileExists, (const std::string& path), (override));
        MOCK_METHOD(std::string, readFileLine, (const std::string& path), (override));
        MOCK_METHOD(uid_t, getUid, (), (override));
        MOCK_METHOD(std::string, getProcessName, (), (override));
        MOCK_METHOD(BinderObserverConfig::ShardingConfig, getSystemServerSharding,
                    (bool debugMonitorAll), (override));
        MOCK_METHOD(BinderObserverConfig::ShardingConfig, getOtherProcessesSharding, (),
                    (override));
        MOCK_METHOD(int64_t, getCpuTimeNanos, (), (override));
        MOCK_METHOD(size_t, hashString8, (const std::string& content), (override));
        MOCK_METHOD(size_t, hashString16, (const std::u16string_view& content), (override));
    };

    const std::string kFullBinderSpamDetectionConfigPath =
            "/data/system/anomaly_service/anomaly_detection.full_binder_spam_detection";
    const std::string kBootIdPath = "/proc/sys/kernel/random/boot_id";
    const std::string kProcessOffsetToken = "16e12b27-2a84-4355"; // used for process offset
    const std::string kAidlOffsetToken = "-84cd-948348d6c998";    // used for aidl offset
    const std::string kCpuOffsetToken =
            "2a84-4355-84cd-948348d6c998"; // bootId.substr(9, 27), used for cpu offset

    static constexpr BinderObserverConfig::ShardingConfig kMonitorEverythingSharding = {
            .processMod = 1,
            .spamMod = 1,
            .callMod = 1,
            .cpuSamplingMod = 1,
    };

    static constexpr BinderObserverConfig::ShardingConfig kSometimesEverythingSharding = {
            .processMod = 2,
            .spamMod = 1,
            .callMod = 1,
            .cpuSamplingMod = 1,
    };

    static constexpr BinderObserverConfig::ShardingConfig kModerateSharding = {
            .processMod = 55,
            .spamMod = 11,
            .callMod = 22,
            .cpuSamplingMod = 10,
    };

    static constexpr BinderObserverConfig::ShardingConfig kOnlySpamSharding = {
            .processMod = 100,
            .spamMod = 33,
            .callMod = 0,
            .cpuSamplingMod = 0,
    };

    static constexpr BinderObserverConfig::ShardingConfig kMonitorNothingSharding = {
            .processMod = 0,
            .spamMod = 0,
            .callMod = 0,
            .cpuSamplingMod = 0,
    };

    static std::unique_ptr<BinderObserverConfig> createConfig(
            BinderObserverConfig::Environment* environment) {
        return BinderObserverConfig::createConfig(
                std::unique_ptr<BinderObserverConfig::Environment>(environment));
    }

    void SetUp() override {
        // Note: this should be owned (and destroyed) by the config class under test.
        mEnv = new MockEnvironment();
        const std::string bootId = std::string(kProcessOffsetToken) + kAidlOffsetToken;
        ON_CALL(*mEnv, fileExists(kFullBinderSpamDetectionConfigPath)).WillByDefault(Return(false));
        ON_CALL(*mEnv, readFileLine(kBootIdPath)).WillByDefault(Return(bootId));
        ON_CALL(*mEnv, hashString8(kProcessOffsetToken)).WillByDefault(Return(5678));
        ON_CALL(*mEnv, hashString8(kAidlOffsetToken)).WillByDefault(Return(6789));
        ON_CALL(*mEnv, hashString8(kCpuOffsetToken)).WillByDefault(Return(7891));
    }

    // Configure mEnv so that the created config will be enabled/disabled (if possible)
    void setUpForSharding(BinderObserverConfig::ShardingConfig sharding, bool enabled = true) {
        EXPECT_CALL(*mEnv, getUid()).WillOnce(Return(123));
        EXPECT_CALL(*mEnv, getProcessName()).WillOnce(Return("other_process"));
        ON_CALL(*mEnv, hashString8(kProcessOffsetToken)).WillByDefault(Return(234));

        // Make the process hash such that the total is divisible by processMod.
        size_t processHash = sharding.processMod != 0
                ? sharding.processMod - ((123 + 234) % sharding.processMod)
                : 345; // impossible, will be disabled
        if (!enabled) {
            // If we don't want it to be enabled, change it so that the mod is not zero
            ++processHash;
        }

        ON_CALL(*mEnv, hashString8("other_process")).WillByDefault(Return(processHash));

        EXPECT_CALL(*mEnv, getOtherProcessesSharding()).WillOnce(Return(sharding));
    }

    MockEnvironment* mEnv;
};

TEST_F(BinderObserverConfigTest, CreateConfigOtherProcessMonitorEverything) {
    EXPECT_CALL(*mEnv, getUid()).WillOnce(Return(123));
    EXPECT_CALL(*mEnv, getProcessName()).WillOnce(Return("other_process"));
    ON_CALL(*mEnv, hashString8(kProcessOffsetToken)).WillByDefault(Return(234));
    ON_CALL(*mEnv, hashString8("other_process")).WillByDefault(Return(345)); // process hash
    EXPECT_CALL(*mEnv, getOtherProcessesSharding()).WillOnce(Return(kMonitorEverythingSharding));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // Since processMod 1 it should be enabled
    EXPECT_TRUE(config->isEnabled());
}

TEST_F(BinderObserverConfigTest, CreateConfigOtherProcessMonitorNothing) {
    EXPECT_CALL(*mEnv, getUid()).WillOnce(Return(123));
    EXPECT_CALL(*mEnv, getProcessName()).WillOnce(Return("other_process"));
    ON_CALL(*mEnv, hashString8(kProcessOffsetToken)).WillByDefault(Return(234));
    ON_CALL(*mEnv, hashString8("other_process")).WillByDefault(Return(345)); // process hash
    EXPECT_CALL(*mEnv, getOtherProcessesSharding()).WillOnce(Return(kMonitorNothingSharding));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // Since processMod is 0 it should be disabled
    EXPECT_FALSE(config->isEnabled());
}

TEST_F(BinderObserverConfigTest, CreateConfigOtherProcessEnabledByHash) {
    EXPECT_CALL(*mEnv, getUid()).WillOnce(Return(123));
    EXPECT_CALL(*mEnv, getProcessName()).WillOnce(Return("other_process"));
    ON_CALL(*mEnv, hashString8(kProcessOffsetToken)).WillByDefault(Return(234));
    ON_CALL(*mEnv, hashString8("other_process")).WillByDefault(Return(303)); // process hash
    EXPECT_CALL(*mEnv, getOtherProcessesSharding()).WillOnce(Return(kModerateSharding));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // the resulting token should be (123 + 234 + 303) % 55 = 0, so enabled
    EXPECT_TRUE(config->isEnabled());
}

TEST_F(BinderObserverConfigTest, CreateConfigOtherProcessDisabledByHash) {
    EXPECT_CALL(*mEnv, getUid()).WillOnce(Return(123));
    EXPECT_CALL(*mEnv, getProcessName()).WillOnce(Return("other_process"));
    ON_CALL(*mEnv, hashString8(kProcessOffsetToken)).WillByDefault(Return(234));
    ON_CALL(*mEnv, hashString8("other_process")).WillByDefault(Return(345)); // process hash
    EXPECT_CALL(*mEnv, getOtherProcessesSharding()).WillOnce(Return(kModerateSharding));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // the resulting token should be (123 + 234 + 345) % 55 = 42, so not enabled
    EXPECT_FALSE(config->isEnabled());
}

TEST_F(BinderObserverConfigTest, CreateConfigSystemServerEnabledByHash) {
    EXPECT_CALL(*mEnv, getUid()).WillOnce(Return(1000));
    EXPECT_CALL(*mEnv, getProcessName()).WillOnce(Return("system_server"));
    ON_CALL(*mEnv, hashString8(kProcessOffsetToken)).WillByDefault(Return(234));
    ON_CALL(*mEnv, hashString8("system_server")).WillByDefault(Return(306)); // process hash
    EXPECT_CALL(*mEnv, getSystemServerSharding(false)).WillOnce(Return(kModerateSharding));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // the resulting token should be (1000 + 234 + 306) % 55 = 0, so enabled
    EXPECT_TRUE(config->isEnabled());
}

TEST_F(BinderObserverConfigTest, CreateConfigSystemServerDisabledByHash) {
    EXPECT_CALL(*mEnv, getUid()).WillOnce(Return(1000));
    EXPECT_CALL(*mEnv, getProcessName()).WillOnce(Return("system_server"));
    ON_CALL(*mEnv, hashString8(kProcessOffsetToken)).WillByDefault(Return(234));
    ON_CALL(*mEnv, hashString8("system_server")).WillByDefault(Return(345)); // process hash
    EXPECT_CALL(*mEnv, getSystemServerSharding(false)).WillOnce(Return(kModerateSharding));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // the resulting token should be (1000 + 234 + 345) % 55 = 39, so not enabled
    EXPECT_FALSE(config->isEnabled());
}

TEST_F(BinderObserverConfigTest, CreateConfigSystemServerEnableMonitorAll) {
    EXPECT_CALL(*mEnv, fileExists(kFullBinderSpamDetectionConfigPath)).WillOnce(Return(true));
    EXPECT_CALL(*mEnv, getUid()).WillOnce(Return(1000));
    EXPECT_CALL(*mEnv, getProcessName()).WillOnce(Return("system_server"));
    ON_CALL(*mEnv, hashString8(kProcessOffsetToken)).WillByDefault(Return(234));
    ON_CALL(*mEnv, hashString8("system_server")).WillByDefault(Return(345)); // process hash
    EXPECT_CALL(*mEnv, getSystemServerSharding(true)).WillOnce(Return(kMonitorEverythingSharding));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // Expect enabled.
    EXPECT_TRUE(config->isEnabled());
}

TEST_F(BinderObserverConfigTest, CreateConfigFakeSystemServer) {
    EXPECT_CALL(*mEnv, getUid()).WillOnce(Return(123)); // not the right uid
    EXPECT_CALL(*mEnv, getProcessName()).WillOnce(Return("system_server"));
    ON_CALL(*mEnv, hashString8(kProcessOffsetToken)).WillByDefault(Return(234));
    ON_CALL(*mEnv, hashString8("other_process")).WillByDefault(Return(345)); // process hash
    ON_CALL(*mEnv, getSystemServerSharding(false))
            .WillByDefault(Return(kMonitorEverythingSharding));
    EXPECT_CALL(*mEnv, getOtherProcessesSharding()).WillOnce(Return(kMonitorNothingSharding));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // Not enabled because it is not the real system_server.
    EXPECT_FALSE(config->isEnabled());
}

TEST_F(BinderObserverConfigTest, GetTrackingInfoModerateShardingNotTracked) {
    setUpForSharding(kModerateSharding);

    ON_CALL(*mEnv, hashString8(kAidlOffsetToken)).WillByDefault(Return(1234));
    ON_CALL(*mEnv, hashString16(std::u16string_view(u"IContentProvider")))
            .WillByDefault(Return(2345));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // setUpForSharding should have ensured we have enabled config.
    EXPECT_TRUE(config->isEnabled());

    // spam: (1234 + 2345 + 21) % 11 = 3
    // calls: (1234 + 2345 + 21) % 22 = 14
    BinderObserverConfig::TrackingInfo expected = {.trackSpam = false,
                                                   .trackLatency = false,
                                                   .trackCpu = false};
    BinderObserverConfig::TrackingInfo result = config->getTrackingInfo(u"IContentProvider", 21);
    EXPECT_EQ(result, expected);
}

TEST_F(BinderObserverConfigTest, GetTrackingInfoModerateShardingTrackSpamOnly) {
    setUpForSharding(kModerateSharding);

    ON_CALL(*mEnv, hashString8(kAidlOffsetToken)).WillByDefault(Return(1234));
    ON_CALL(*mEnv, hashString16(std::u16string_view(u"IContentProvider")))
            .WillByDefault(Return(2342));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // setUpForSharding should have ensured we have enabled config.
    EXPECT_TRUE(config->isEnabled());

    // spam: (1234 + 2342 + 21) % 11 = 0
    // calls: (1234 + 2342 + 21) % 22 = 11
    BinderObserverConfig::TrackingInfo expected = {.trackSpam = true,
                                                   .trackLatency = false,
                                                   .trackCpu = false};
    BinderObserverConfig::TrackingInfo result = config->getTrackingInfo(u"IContentProvider", 21);
    EXPECT_EQ(result, expected);
}

TEST_F(BinderObserverConfigTest, GetTrackingInfoModerateShardingTrackSpamAndCalls) {
    setUpForSharding(kModerateSharding);

    ON_CALL(*mEnv, hashString8(kAidlOffsetToken)).WillByDefault(Return(1234));
    ON_CALL(*mEnv, hashString16(std::u16string_view(u"IContentProvider")))
            .WillByDefault(Return(2331));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // setUpForSharding should have ensured we have enabled config.
    EXPECT_TRUE(config->isEnabled());

    // spam: (1234 + 2331 + 21) % 11 = 0
    // calls: (1234 + 2331 + 21) % 22 = 0
    BinderObserverConfig::TrackingInfo expected = {.trackSpam = true,
                                                   .trackLatency = true,
                                                   .trackCpu = false};
    BinderObserverConfig::TrackingInfo result = config->getTrackingInfo(u"IContentProvider", 21);
    EXPECT_EQ(result, expected);
}

TEST_F(BinderObserverConfigTest, GetTrackingInfoMonitorEverythingShardingTrackSpamAndCalls) {
    setUpForSharding(kMonitorEverythingSharding);

    ON_CALL(*mEnv, hashString8(kAidlOffsetToken)).WillByDefault(Return(1234));
    ON_CALL(*mEnv, hashString16(std::u16string_view(u"IContentProvider")))
            .WillByDefault(Return(2345));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // setUpForSharding should have ensured we have enabled config.
    EXPECT_TRUE(config->isEnabled());

    // The numbers should not matter, everything should be monitored
    BinderObserverConfig::TrackingInfo expected = {.trackSpam = true,
                                                   .trackLatency = true,
                                                   .trackCpu = true};
    BinderObserverConfig::TrackingInfo result = config->getTrackingInfo(u"IContentProvider", 21);
    EXPECT_EQ(result, expected);
}

TEST_F(BinderObserverConfigTest, GetTrackingInfoMonitorNothingShardingTrackNothing) {
    setUpForSharding(kMonitorNothingSharding);

    ON_CALL(*mEnv, hashString8(kAidlOffsetToken)).WillByDefault(Return(1234));
    ON_CALL(*mEnv, hashString16(std::u16string_view(u"IContentProvider")))
            .WillByDefault(Return(2345));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // setUpForSharding should have ensured we have disiabled config.
    EXPECT_FALSE(config->isEnabled());

    // The numbers should not matter, nothing should be monitored
    BinderObserverConfig::TrackingInfo expected = {.trackSpam = false,
                                                   .trackLatency = false,
                                                   .trackCpu = false};
    BinderObserverConfig::TrackingInfo result = config->getTrackingInfo(u"IContentProvider", 21);
    EXPECT_EQ(result, expected);
}

TEST_F(BinderObserverConfigTest, GetTrackingInfoSometimesEverythingShardingTrackNothing) {
    setUpForSharding(kSometimesEverythingSharding, false);

    ON_CALL(*mEnv, hashString8(kAidlOffsetToken)).WillByDefault(Return(1234));
    ON_CALL(*mEnv, hashString16(std::u16string_view(u"IContentProvider")))
            .WillByDefault(Return(2345));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // setUpForSharding should have created a disabled config
    EXPECT_FALSE(config->isEnabled());

    // The numbers should not matter, nothing should be tracked because we are disabled
    BinderObserverConfig::TrackingInfo expected = {.trackSpam = false,
                                                   .trackLatency = false,
                                                   .trackCpu = false};
    BinderObserverConfig::TrackingInfo result = config->getTrackingInfo(u"IContentProvider", 21);
    EXPECT_EQ(result, expected);
}

TEST_F(BinderObserverConfigTest, GetTrackingInfoOnlySpamShardingTrackSpam) {
    setUpForSharding(kOnlySpamSharding);

    ON_CALL(*mEnv, hashString8(kAidlOffsetToken)).WillByDefault(Return(1234));
    ON_CALL(*mEnv, hashString16(std::u16string_view(u"IContentProvider")))
            .WillByDefault(Return(2342));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // setUpForSharding should have created an enabled config
    EXPECT_TRUE(config->isEnabled());

    // spam: (1234 + 2342 + 21) % 33 = 0
    // calls: the numbers don't matter since callMod is 0
    BinderObserverConfig::TrackingInfo expected = {.trackSpam = true,
                                                   .trackLatency = false,
                                                   .trackCpu = false};
    BinderObserverConfig::TrackingInfo result = config->getTrackingInfo(u"IContentProvider", 21);
    EXPECT_EQ(result, expected);
}

TEST_F(BinderObserverConfigTest, CPUTrackingOffWhenkBinderObserverV2Disabled) {
    if (kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Binder Observer V2 is enabled";
    }
    setUpForSharding(kModerateSharding);

    ON_CALL(*mEnv, hashString8(kAidlOffsetToken)).WillByDefault(Return(1234));
    ON_CALL(*mEnv, hashString16(std::u16string_view(u"IContentProvider")))
            .WillByDefault(Return(2331));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // setUpForSharding should have ensured we have enabled config.
    EXPECT_TRUE(config->isEnabled());

    // spam: (1234 + 2331 + 21) % 1 = 0
    // calls: (1234 + 2331 + 21) % 1 = 0
    BinderObserverConfig::TrackingInfo expected = {.trackSpam = true,
                                                   .trackLatency = true,
                                                   .trackCpu = false};
    BinderObserverConfig::TrackingInfo result = config->getTrackingInfo(u"IContentProvider", 21);
    EXPECT_EQ(result, expected);
}

TEST_F(BinderObserverConfigTest, GetTrackingInfoTrackCpu) {
    if (!kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Binder Observer V2 is not enabled";
    }
    setUpForSharding(kMonitorEverythingSharding);

    ON_CALL(*mEnv, hashString8(kAidlOffsetToken)).WillByDefault(Return(1234));
    ON_CALL(*mEnv, hashString16(std::u16string_view(u"IContentProvider")))
            .WillByDefault(Return(2331));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // setUpForSharding should have ensured we have enabled config.
    EXPECT_TRUE(config->isEnabled());

    // spam: (1234 + 2331 + 21) % 11 = 0
    // calls: (1234 + 2331 + 21) % 22 = 0
    // cpu: should be tracked because sample rate is 1
    BinderObserverConfig::TrackingInfo expected = {.trackSpam = true,
                                                   .trackLatency = true,
                                                   .trackCpu = true};
    BinderObserverConfig::TrackingInfo result = config->getTrackingInfo(u"IContentProvider", 21);
    EXPECT_EQ(result, expected);
}

TEST_F(BinderObserverConfigTest, GetTrackingInfoNoTrackCpuWhenLatencyTrackingOff) {
    if (!kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Binder Observer V2 is not enabled";
    }
    setUpForSharding(kOnlySpamSharding);

    ON_CALL(*mEnv, hashString8(kAidlOffsetToken)).WillByDefault(Return(1234));
    ON_CALL(*mEnv, hashString16(std::u16string_view(u"IContentProvider")))
            .WillByDefault(Return(2342));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);

    // setUpForSharding should have ensured we have enabled config.
    EXPECT_TRUE(config->isEnabled());

    // spam: (1234 + 2331 + 21) % 1 = 0
    // calls: (1234 + 2331 + 21) % 1 = 0
    // cpu: should not be tracked because sample rate is 0
    BinderObserverConfig::TrackingInfo expected = {.trackSpam = true,
                                                   .trackLatency = false,
                                                   .trackCpu = false};
    BinderObserverConfig::TrackingInfo result = config->getTrackingInfo(u"IContentProvider", 21);
    EXPECT_EQ(result, expected);
}

TEST_F(BinderObserverConfigTest, GetTrackingInfoTrackCpuOnce) {
    if (!kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Binder Observer V2 is not enabled";
    }
    setUpForSharding(kModerateSharding); // cpuSamplingMod = 10

    ON_CALL(*mEnv, hashString8(kAidlOffsetToken)).WillByDefault(Return(1234));
    ON_CALL(*mEnv, hashString16(std::u16string_view(u"IContentProvider")))
            .WillByDefault(Return(2331));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);
    EXPECT_TRUE(config->isEnabled());

    // spam: (1234 + 2331 + 21) % 11 = 0 -> trackSpam = true
    // calls: (1234 + 2331 + 21) % 22 = 0 -> trackLatency = true

    int cpuTrackCount = 0;
    for (int i = 0; i < kModerateSharding.cpuSamplingMod; i++) {
        BinderObserverConfig::TrackingInfo result =
                config->getTrackingInfo(u"IContentProvider", 21);
        EXPECT_TRUE(result.trackSpam);
        EXPECT_TRUE(result.trackLatency);
        if (result.trackCpu) {
            cpuTrackCount++;
        }
    }
    EXPECT_EQ(cpuTrackCount, 1);
}

TEST_F(BinderObserverConfigTest, GetTrackingInfoTrackCpuWithOffset) {
    if (!kBinderObserverV2Enabled) {
        GTEST_SKIP() << "Binder Observer V2 is not enabled";
    }
    setUpForSharding(kModerateSharding); // cpuSamplingMod = 10

    ON_CALL(*mEnv, hashString8(kCpuOffsetToken)).WillByDefault(Return(7893)); // offset = 3

    ON_CALL(*mEnv, hashString8(kAidlOffsetToken)).WillByDefault(Return(1234));
    ON_CALL(*mEnv, hashString16(std::u16string_view(u"IContentProvider")))
            .WillByDefault(Return(2331));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);
    EXPECT_TRUE(config->isEnabled());

    // spam: (1234 + 2331 + 21) % 11 = 0 -> trackSpam = true
    // calls: (1234 + 2331 + 21) % 22 = 0 -> trackLatency = true

    bool cpuWasTracked = false;
    for (int i = 0; i < kModerateSharding.cpuSamplingMod; i++) {
        BinderObserverConfig::TrackingInfo result =
                config->getTrackingInfo(u"IContentProvider", 21);
        EXPECT_TRUE(result.trackSpam);
        EXPECT_TRUE(result.trackLatency);
        if (result.trackCpu) {
            // Should be tracked when (i + 3) % 10 == 0, which is when i = 7
            EXPECT_EQ(i, 7);
            cpuWasTracked = true;
        }
    }
    EXPECT_TRUE(cpuWasTracked);
}

// --- BinderObserver Tests ---

TEST_F(BinderObserverConfigTest, BinderObserverCpuTimeNanosCorrect) {
    EXPECT_CALL(*mEnv, getUid()).WillRepeatedly(Return(1000));
    EXPECT_CALL(*mEnv, getProcessName()).WillRepeatedly(Return("system_server"));
    EXPECT_CALL(*mEnv, getSystemServerSharding(false))
            .WillRepeatedly(Return(kMonitorEverythingSharding));
    EXPECT_CALL(*mEnv, getCpuTimeNanos()).WillRepeatedly(Return(2000002));

    std::unique_ptr<BinderObserverConfig> config = createConfig(mEnv);
    BinderObserver observer(std::move(config));

    std::shared_ptr<BinderStatsSpscQueue> queue;

    BinderObserver::CallInfo callInfo;
    callInfo.startTimeNanos = 1000;
    callInfo.cpuUsageStartTimeNanos = 1000000; // 1ms

    callInfo.trackingInfo = {.trackSpam = true, .trackLatency = true, .trackCpu = true};
    callInfo.callingUid = 1000;
    callInfo.code = 1;
    callInfo.interfaceDescriptor = String16("test");
    callInfo.aidlMethodName = String16("method");

    // The first call will trigger a flush, so we need to call it twice.
    observer.onEndTransaction(queue, callInfo);
    observer.onEndTransaction(queue, callInfo);

    ASSERT_NE(nullptr, queue);
    auto item = queue->tryPop();
    ASSERT_TRUE(item.has_value());

    // cpuTimeNanos should be exactly 1ms and 2 ns (1000002 ns).
    EXPECT_EQ(item->cpuTimeNanos, 1000002);
}

} // namespace android