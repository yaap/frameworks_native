/*
 * Copyright 2019 The Android Open Source Project
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

#undef LOG_TAG
#define LOG_TAG "SchedulerUnittests"

#include <gmock/gmock.h>
#include <log/log.h>
#include <chrono>
#include <thread>
#include <utility>

#include <scheduler/Time.h>

#include "Scheduler/VsyncConfiguration.h"

using namespace testing;

namespace android::scheduler {

using namespace std::chrono_literals;

class TestableWorkDuration : public impl::WorkDuration {
public:
    TestableWorkDuration(Fps currentFps, nsecs_t sfDuration, nsecs_t appDuration,
                         nsecs_t sfEarlyDuration, nsecs_t appEarlyDuration,
                         nsecs_t sfEarlyGlDuration, nsecs_t appEarlyGlDuration,
                         nsecs_t hwcMinWorkDuration)
          : impl::WorkDuration(currentFps, sfDuration, appDuration, sfEarlyDuration,
                               appEarlyDuration, sfEarlyGlDuration, appEarlyGlDuration,
                               hwcMinWorkDuration) {}

    TestableWorkDuration(Fps currentFps, Duration minSfDuration, Duration maxSfDuration,
                         Duration appDuration)
          : impl::WorkDuration(currentFps, minSfDuration, maxSfDuration, appDuration) {}

protected:
    // The per-refresh-rate overrides may be set on the test device; keep the expectations
    // deterministic by not reading the property service.
    impl::DurationOverrides getDurationOverrides() const override { return {}; }
};

class TestableWorkDurationWithFpsOverrides : public impl::WorkDuration {
public:
    TestableWorkDurationWithFpsOverrides(Fps currentFps, nsecs_t sfDuration, nsecs_t appDuration,
                                         nsecs_t sfEarlyDuration, nsecs_t appEarlyDuration,
                                         nsecs_t sfEarlyGlDuration, nsecs_t appEarlyGlDuration,
                                         impl::DurationOverrides overrides)
          : impl::WorkDuration(currentFps, sfDuration, appDuration, sfEarlyDuration,
                               appEarlyDuration, sfEarlyGlDuration, appEarlyGlDuration, 0),
            mOverrides(std::move(overrides)) {}

protected:
    impl::DurationOverrides getDurationOverrides() const override { return mOverrides; }

private:
    const impl::DurationOverrides mOverrides;
};

class WorkDurationTest : public testing::Test {
protected:
    WorkDurationTest()
          : mWorkDuration(60_Hz, 10'500'000, 20'500'000, 16'000'000, 16'500'000, 13'500'000,
                          21'000'000, 1234) {}

    ~WorkDurationTest() = default;

    TestableWorkDuration mWorkDuration;
};

/* ------------------------------------------------------------------------
 * Test cases
 */
TEST_F(WorkDurationTest, getConfigsForRefreshRate_60Hz) {
    mWorkDuration.setRefreshRateFps(60_Hz);
    auto currentOffsets = mWorkDuration.getCurrentConfigs();
    auto offsets = mWorkDuration.getConfigsForRefreshRate(60_Hz);

    EXPECT_EQ(currentOffsets, offsets);
    EXPECT_EQ(offsets.late.sfOffset, 6'166'667);
    EXPECT_EQ(offsets.late.appOffset, 2'333'334);

    EXPECT_EQ(offsets.late.sfWorkDuration, 10'500'000ns);
    EXPECT_EQ(offsets.late.appWorkDuration, 20'500'000ns);

    EXPECT_EQ(offsets.early.sfOffset, 666'667);
    EXPECT_EQ(offsets.early.appOffset, 833'334);

    EXPECT_EQ(offsets.early.sfWorkDuration, 16'000'000ns);
    EXPECT_EQ(offsets.early.appWorkDuration, 16'500'000ns);

    EXPECT_EQ(offsets.earlyGpu.sfOffset, 3'166'667);
    EXPECT_EQ(offsets.earlyGpu.appOffset, 15'500'001);

    EXPECT_EQ(offsets.earlyGpu.sfWorkDuration, 13'500'000ns);
    EXPECT_EQ(offsets.earlyGpu.appWorkDuration, 21'000'000ns);
}

TEST_F(WorkDurationTest, getConfigsForRefreshRate_90Hz) {
    mWorkDuration.setRefreshRateFps(90_Hz);
    auto currentOffsets = mWorkDuration.getCurrentConfigs();
    auto offsets = mWorkDuration.getConfigsForRefreshRate(90_Hz);

    EXPECT_EQ(currentOffsets, offsets);
    EXPECT_EQ(offsets.late.sfOffset, 611'111);
    EXPECT_EQ(offsets.late.appOffset, 2'333'333);

    EXPECT_EQ(offsets.late.sfWorkDuration, 10'500'000ns);
    EXPECT_EQ(offsets.late.appWorkDuration, 20'500'000ns);

    EXPECT_EQ(offsets.early.sfOffset, -4'888'889);
    EXPECT_EQ(offsets.early.appOffset, 833'333);

    EXPECT_EQ(offsets.early.sfWorkDuration, 16'000'000ns);
    EXPECT_EQ(offsets.early.appWorkDuration, 16'500'000ns);

    EXPECT_EQ(offsets.earlyGpu.sfOffset, -2'388'889);
    EXPECT_EQ(offsets.earlyGpu.appOffset, 9'944'444);

    EXPECT_EQ(offsets.earlyGpu.sfWorkDuration, 13'500'000ns);
    EXPECT_EQ(offsets.earlyGpu.appWorkDuration, 21'000'000ns);
}

TEST_F(WorkDurationTest, getConfigsForRefreshRate_DefaultOffsets) {
    TestableWorkDuration phaseOffsetsWithDefaultValues(60_Hz, -1, -1, -1, -1, -1, -1, 0);

    auto validateOffsets = [](const auto& offsets, std::chrono::nanoseconds vsyncPeriod) {
        EXPECT_EQ(offsets.late.sfOffset, 1'000'000);
        EXPECT_EQ(offsets.late.appOffset, 1'000'000);

        EXPECT_EQ(offsets.late.sfWorkDuration, vsyncPeriod - 1'000'000ns);
        EXPECT_EQ(offsets.late.appWorkDuration, vsyncPeriod);

        EXPECT_EQ(offsets.early.sfOffset, 1'000'000);
        EXPECT_EQ(offsets.early.appOffset, 1'000'000);

        EXPECT_EQ(offsets.early.sfWorkDuration, vsyncPeriod - 1'000'000ns);
        EXPECT_EQ(offsets.early.appWorkDuration, vsyncPeriod);

        EXPECT_EQ(offsets.earlyGpu.sfOffset, 1'000'000);
        EXPECT_EQ(offsets.earlyGpu.appOffset, 1'000'000);

        EXPECT_EQ(offsets.earlyGpu.sfWorkDuration, vsyncPeriod - 1'000'000ns);
        EXPECT_EQ(offsets.earlyGpu.appWorkDuration, vsyncPeriod);

        EXPECT_EQ(offsets.hwcMinWorkDuration, 0ns);
    };

    const auto testForRefreshRate = [&](Fps refreshRate) {
        phaseOffsetsWithDefaultValues.setRefreshRateFps(refreshRate);
        auto currentOffsets = phaseOffsetsWithDefaultValues.getCurrentConfigs();
        auto offsets = phaseOffsetsWithDefaultValues.getConfigsForRefreshRate(refreshRate);
        EXPECT_EQ(currentOffsets, offsets);
        validateOffsets(offsets, std::chrono::nanoseconds(refreshRate.getPeriodNsecs()));
    };

    testForRefreshRate(90_Hz);
    testForRefreshRate(60_Hz);
}

TEST_F(WorkDurationTest, getConfigsForRefreshRate_unknownRefreshRate) {
    auto offsets = mWorkDuration.getConfigsForRefreshRate(14.7_Hz);

    EXPECT_EQ(offsets.late.sfOffset, 57'527'208);
    EXPECT_EQ(offsets.late.appOffset, 37'027'208);

    EXPECT_EQ(offsets.late.sfWorkDuration, 10'500'000ns);
    EXPECT_EQ(offsets.late.appWorkDuration, 20'500'000ns);

    EXPECT_EQ(offsets.early.sfOffset, 52'027'208);
    EXPECT_EQ(offsets.early.appOffset, 35'527'208);

    EXPECT_EQ(offsets.early.sfWorkDuration, 16'000'000ns);
    EXPECT_EQ(offsets.early.appWorkDuration, 16'500'000ns);

    EXPECT_EQ(offsets.earlyGpu.sfOffset, 54'527'208);
    EXPECT_EQ(offsets.earlyGpu.appOffset, 33'527'208);

    EXPECT_EQ(offsets.earlyGpu.sfWorkDuration, 13'500'000ns);
    EXPECT_EQ(offsets.earlyGpu.appWorkDuration, 21'000'000ns);
}

TEST_F(WorkDurationTest, minHwcWorkDuration) {
    EXPECT_EQ(mWorkDuration.getCurrentConfigs().hwcMinWorkDuration, 1234ns);
}

TEST_F(WorkDurationTest, workDurationIsARange) {
    const Duration minSfDuration = Duration::fromNs(10'500'000);
    const Duration maxSfDuration = Duration::fromNs(20'500'000);
    const Duration appDuration = Duration::fromNs(16'000'000);
    const TestableWorkDuration workDuration{60_Hz, minSfDuration, maxSfDuration, appDuration};

    auto currentOffsets = workDuration.getCurrentConfigs();
    auto offsets = workDuration.getConfigsForRefreshRate(60_Hz);

    EXPECT_EQ(currentOffsets, offsets);
    EXPECT_EQ(offsets.late.sfOffset, 6'166'667);
    EXPECT_EQ(offsets.late.appOffset, 6'833'334);

    EXPECT_EQ(offsets.late.sfWorkDuration, 10'500'000ns);
    EXPECT_EQ(offsets.late.appWorkDuration, 16'000'000ns);

    EXPECT_EQ(offsets.early.sfOffset, -3'833'333);
    EXPECT_EQ(offsets.early.appOffset, 13'500'001);

    EXPECT_EQ(offsets.early.sfWorkDuration, 20'500'000ns);
    EXPECT_EQ(offsets.early.appWorkDuration, 16'000'000ns);

    EXPECT_EQ(offsets.earlyGpu.sfOffset, -3'833'333);
    EXPECT_EQ(offsets.earlyGpu.appOffset, 13'500'001);

    EXPECT_EQ(offsets.earlyGpu.sfWorkDuration, 20'500'000ns);
    EXPECT_EQ(offsets.earlyGpu.appWorkDuration, 16'000'000ns);
}

TEST_F(WorkDurationTest, getConfigsForRefreshRate_perFpsOverrides) {
    TestableWorkDurationWithFpsOverrides workDuration(
            60_Hz, 10'500'000, 20'500'000, 16'000'000, 16'500'000, 13'500'000, 21'000'000,
            {{"debug.sf.late.sf.duration", {{24, 16'666'666}, {60, 15'666'666}, {90, 7'000'000}}},
             {"debug.sf.earlyGl.app.duration", {{60, 8'000'000}}}});

    // Exact matches win; the durations without an override keep the constructor values.
    auto offsets = workDuration.getConfigsForRefreshRate(60_Hz);
    EXPECT_EQ(offsets.late.sfWorkDuration, 15'666'666ns);
    EXPECT_EQ(offsets.late.appWorkDuration, 20'500'000ns);
    EXPECT_EQ(offsets.early.sfWorkDuration, 16'000'000ns);
    EXPECT_EQ(offsets.early.appWorkDuration, 16'500'000ns);
    EXPECT_EQ(offsets.earlyGpu.sfWorkDuration, 13'500'000ns);
    EXPECT_EQ(offsets.earlyGpu.appWorkDuration, 8'000'000ns);

    offsets = workDuration.getConfigsForRefreshRate(90_Hz);
    EXPECT_EQ(offsets.late.sfWorkDuration, 7'000'000ns);
    EXPECT_EQ(offsets.late.sfOffset, 4'111'111);
    EXPECT_EQ(offsets.late.appWorkDuration, 20'500'000ns);
    EXPECT_EQ(offsets.early.sfWorkDuration, 16'000'000ns);
    EXPECT_EQ(offsets.early.appWorkDuration, 16'500'000ns);
    EXPECT_EQ(offsets.earlyGpu.sfWorkDuration, 13'500'000ns);
    EXPECT_EQ(offsets.earlyGpu.appWorkDuration, 8'000'000ns);  // the closest override is .60

    // 24 Hz matches exactly, and 10 Hz uses the closest override (24 Hz).
    EXPECT_EQ(workDuration.getConfigsForRefreshRate(24_Hz).late.sfWorkDuration, 16'666'666ns);
    EXPECT_EQ(workDuration.getConfigsForRefreshRate(10_Hz).late.sfWorkDuration, 16'666'666ns);
}

TEST_F(WorkDurationTest, getConfigsForRefreshRate_closestFpsOverrides) {
    TestableWorkDurationWithFpsOverrides workDuration(
            60_Hz, 10'500'000, 20'500'000, 16'000'000, 16'500'000, 13'500'000, 21'000'000,
            {{"debug.sf.late.sf.duration",
              {{20, 16'500'000},
               {24, 16'666'666},
               {30, 16'666'666},
               {40, 16'500'000},
               {60, 15'666'666},
               {90, 13'111'111},
               {120, 10'333'333},
               {144, 10'347'222}}},
             {"debug.sf.early.app.duration", {{60, 16'666'666}, {144, 10'333'333}}}});

    const auto sfDurationFor = [&](Fps fps) {
        return workDuration.getConfigsForRefreshRate(fps).late.sfWorkDuration;
    };

    // Exact matches.
    EXPECT_EQ(sfDurationFor(60_Hz), 15'666'666ns);
    EXPECT_EQ(sfDurationFor(90_Hz), 13'111'111ns);
    EXPECT_EQ(sfDurationFor(120_Hz), 10'333'333ns);

    // Rates without an override use the closest one, whether it is lower or higher.
    EXPECT_EQ(sfDurationFor(10_Hz), 16'500'000ns);   // .20
    EXPECT_EQ(sfDurationFor(15_Hz), 16'500'000ns);   // .20
    EXPECT_EQ(sfDurationFor(48_Hz), 16'500'000ns);   // .40, delta 8 vs .60
    EXPECT_EQ(sfDurationFor(96_Hz), 13'111'111ns);   // .90, delta 6 vs .120
    EXPECT_EQ(sfDurationFor(165_Hz), 10'347'222ns);  // .144, the highest available

    // Fractional rates are resolved against the actual frequency, not a rounded integer.
    EXPECT_EQ(sfDurationFor(22.5_Hz), 16'666'666ns);  // .24, delta 1.5 vs .20
    EXPECT_EQ(sfDurationFor(59.94_Hz), 15'666'666ns); // .60

    // Equidistant rates are resolved towards the lower one.
    EXPECT_EQ(sfDurationFor(50_Hz), 16'500'000ns);  // .40 vs .60

    // Each property is resolved independently, with the constructor values as fallback.
    const auto offsets = workDuration.getConfigsForRefreshRate(10_Hz);
    EXPECT_EQ(offsets.late.sfWorkDuration, 16'500'000ns);
    EXPECT_EQ(offsets.late.sfOffset, 83'500'000);
    EXPECT_EQ(offsets.late.appWorkDuration, 20'500'000ns);
    EXPECT_EQ(offsets.early.sfWorkDuration, 16'000'000ns);
    EXPECT_EQ(offsets.early.appWorkDuration, 16'666'666ns);  // .60
    EXPECT_EQ(offsets.earlyGpu.sfWorkDuration, 13'500'000ns);
    EXPECT_EQ(offsets.earlyGpu.appWorkDuration, 21'000'000ns);

    EXPECT_EQ(workDuration.getConfigsForRefreshRate(120_Hz).early.appWorkDuration, 10'333'333ns);
}

class TestablePhaseOffsets : public impl::PhaseOffsets {
public:
    TestablePhaseOffsets(nsecs_t vsyncPhaseOffsetNs, nsecs_t sfVSyncPhaseOffsetNs,
                         std::optional<nsecs_t> earlySfOffsetNs,
                         std::optional<nsecs_t> earlyGpuSfOffsetNs,
                         std::optional<nsecs_t> earlyAppOffsetNs,
                         std::optional<nsecs_t> earlyGpuAppOffsetNs,
                         nsecs_t highFpsVsyncPhaseOffsetNs, nsecs_t highFpsSfVSyncPhaseOffsetNs,
                         std::optional<nsecs_t> highFpsEarlySfOffsetNs,
                         std::optional<nsecs_t> highFpsEarlyGpuSfOffsetNs,
                         std::optional<nsecs_t> highFpsEarlyAppOffsetNs,
                         std::optional<nsecs_t> highFpsEarlyGpuAppOffsetNs,
                         nsecs_t thresholdForNextVsync, nsecs_t hwcMinWorkDuration)
          : impl::PhaseOffsets(60_Hz, vsyncPhaseOffsetNs, sfVSyncPhaseOffsetNs, earlySfOffsetNs,
                               earlyGpuSfOffsetNs, earlyAppOffsetNs, earlyGpuAppOffsetNs,
                               highFpsVsyncPhaseOffsetNs, highFpsSfVSyncPhaseOffsetNs,
                               highFpsEarlySfOffsetNs, highFpsEarlyGpuSfOffsetNs,
                               highFpsEarlyAppOffsetNs, highFpsEarlyGpuAppOffsetNs,
                               thresholdForNextVsync, hwcMinWorkDuration) {}
};

class PhaseOffsetsTest : public testing::Test {
protected:
    PhaseOffsetsTest() = default;
    ~PhaseOffsetsTest() = default;

    TestablePhaseOffsets mPhaseOffsets{2'000'000, 6'000'000, 7'000'000,  8'000'000, 3'000'000,
                                       4'000'000, 2'000'000, 1'000'000,  2'000'000, 3'000'000,
                                       3'000'000, 4'000'000, 10'000'000, 1234};
};

TEST_F(PhaseOffsetsTest, getConfigsForRefreshRate_unknownRefreshRate) {
    auto offsets = mPhaseOffsets.getConfigsForRefreshRate(14.7_Hz);

    EXPECT_EQ(offsets.late.sfOffset, 6'000'000);
    EXPECT_EQ(offsets.late.appOffset, 2'000'000);

    EXPECT_EQ(offsets.late.sfWorkDuration, 62'027'208ns);
    EXPECT_EQ(offsets.late.appWorkDuration, 72'027'208ns);

    EXPECT_EQ(offsets.early.sfOffset, 7'000'000);
    EXPECT_EQ(offsets.early.appOffset, 3'000'000);

    EXPECT_EQ(offsets.early.sfWorkDuration, 61'027'208ns);
    EXPECT_EQ(offsets.early.appWorkDuration, 72'027'208ns);

    EXPECT_EQ(offsets.earlyGpu.sfOffset, 8'000'000);
    EXPECT_EQ(offsets.earlyGpu.appOffset, 4'000'000);

    EXPECT_EQ(offsets.earlyGpu.sfWorkDuration, 60'027'208ns);
    EXPECT_EQ(offsets.earlyGpu.appWorkDuration, 72'027'208ns);
}

TEST_F(PhaseOffsetsTest, getConfigsForRefreshRate_60Hz) {
    auto offsets = mPhaseOffsets.getConfigsForRefreshRate(60_Hz);

    EXPECT_EQ(offsets.late.sfOffset, 6'000'000);
    EXPECT_EQ(offsets.late.appOffset, 2'000'000);

    EXPECT_EQ(offsets.late.sfWorkDuration, 10'666'667ns);
    EXPECT_EQ(offsets.late.appWorkDuration, 20'666'667ns);

    EXPECT_EQ(offsets.early.sfOffset, 7'000'000);
    EXPECT_EQ(offsets.early.appOffset, 3'000'000);

    EXPECT_EQ(offsets.early.sfWorkDuration, 9'666'667ns);
    EXPECT_EQ(offsets.early.appWorkDuration, 20'666'667ns);

    EXPECT_EQ(offsets.earlyGpu.sfOffset, 8'000'000);
    EXPECT_EQ(offsets.earlyGpu.appOffset, 4'000'000);

    EXPECT_EQ(offsets.earlyGpu.sfWorkDuration, 8'666'667ns);
    EXPECT_EQ(offsets.earlyGpu.appWorkDuration, 20'666'667ns);
}

TEST_F(PhaseOffsetsTest, getConfigsForRefreshRate_90Hz) {
    auto offsets = mPhaseOffsets.getConfigsForRefreshRate(90_Hz);

    EXPECT_EQ(offsets.late.sfOffset, 1'000'000);
    EXPECT_EQ(offsets.late.appOffset, 2'000'000);

    EXPECT_EQ(offsets.late.sfWorkDuration, 10'111'111ns);
    EXPECT_EQ(offsets.late.appWorkDuration, 21'222'222ns);

    EXPECT_EQ(offsets.early.sfOffset, 2'000'000);
    EXPECT_EQ(offsets.early.appOffset, 3'000'000);

    EXPECT_EQ(offsets.early.sfWorkDuration, 9'111'111ns);
    EXPECT_EQ(offsets.early.appWorkDuration, 21'222'222ns);

    EXPECT_EQ(offsets.earlyGpu.sfOffset, 3'000'000);
    EXPECT_EQ(offsets.earlyGpu.appOffset, 4'000'000);

    EXPECT_EQ(offsets.earlyGpu.sfWorkDuration, 8'111'111ns);
    EXPECT_EQ(offsets.earlyGpu.appWorkDuration, 21'222'222ns);
}

TEST_F(PhaseOffsetsTest, getConfigsForRefreshRate_DefaultValues_60Hz) {
    TestablePhaseOffsets phaseOffsets{1'000'000, 1'000'000, {}, {}, {}, {},         2'000'000,
                                      1'000'000, {},        {}, {}, {}, 10'000'000, 1234};
    auto offsets = phaseOffsets.getConfigsForRefreshRate(60_Hz);

    EXPECT_EQ(offsets.late.sfOffset, 1'000'000);
    EXPECT_EQ(offsets.late.appOffset, 1'000'000);

    EXPECT_EQ(offsets.late.sfWorkDuration, 15'666'667ns);
    EXPECT_EQ(offsets.late.appWorkDuration, 16'666'667ns);

    EXPECT_EQ(offsets.early.sfOffset, 1'000'000);
    EXPECT_EQ(offsets.early.appOffset, 1'000'000);

    EXPECT_EQ(offsets.early.sfWorkDuration, 15'666'667ns);
    EXPECT_EQ(offsets.early.appWorkDuration, 16'666'667ns);

    EXPECT_EQ(offsets.earlyGpu.sfOffset, 1'000'000);
    EXPECT_EQ(offsets.earlyGpu.appOffset, 1'000'000);

    EXPECT_EQ(offsets.earlyGpu.sfWorkDuration, 15'666'667ns);
    EXPECT_EQ(offsets.earlyGpu.appWorkDuration, 16'666'667ns);
}

TEST_F(PhaseOffsetsTest, getConfigsForRefreshRate_DefaultValues_90Hz) {
    TestablePhaseOffsets phaseOffsets{1'000'000, 1'000'000, {}, {}, {}, {},         2'000'000,
                                      1'000'000, {},        {}, {}, {}, 10'000'000, 1234};
    auto offsets = phaseOffsets.getConfigsForRefreshRate(90_Hz);

    EXPECT_EQ(offsets.late.sfOffset, 1'000'000);
    EXPECT_EQ(offsets.late.appOffset, 2'000'000);

    EXPECT_EQ(offsets.late.sfWorkDuration, 10'111'111ns);
    EXPECT_EQ(offsets.late.appWorkDuration, 21'222'222ns);

    EXPECT_EQ(offsets.early.sfOffset, 1'000'000);
    EXPECT_EQ(offsets.early.appOffset, 2'000'000);

    EXPECT_EQ(offsets.early.sfWorkDuration, 10'111'111ns);
    EXPECT_EQ(offsets.early.appWorkDuration, 21'222'222ns);

    EXPECT_EQ(offsets.earlyGpu.sfOffset, 1'000'000);
    EXPECT_EQ(offsets.earlyGpu.appOffset, 2'000'000);

    EXPECT_EQ(offsets.earlyGpu.sfWorkDuration, 10'111'111ns);
    EXPECT_EQ(offsets.earlyGpu.appWorkDuration, 21'222'222ns);
}

TEST_F(PhaseOffsetsTest, minHwcWorkDuration) {
    TestablePhaseOffsets phaseOffsets{1'000'000, 1'000'000, {}, {}, {}, {},         2'000'000,
                                      1'000'000, {},        {}, {}, {}, 10'000'000, 1234};
    EXPECT_EQ(phaseOffsets.getCurrentConfigs().hwcMinWorkDuration, 1234ns);
}

} // namespace android::scheduler
