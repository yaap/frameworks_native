/*
 * Copyright 2024 The Android Open Source Project
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

#include "RotaryEncoderInputMapper.h"

#include <list>
#include <string>

#include <android-base/logging.h>
#include <com_android_input_flags.h>
#include <flag_macros.h>
#include <gtest/gtest.h>
#include <input/DisplayViewport.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <utils/Timers.h>

#include "InputMapperTest.h"
#include "InputReaderBase.h"
#include "InterfaceMocks.h"
#include "NotifyArgs.h"
#include "TestEventMatchers.h"
#include "ui/Rotation.h"

#define TAG "RotaryEncoderInputMapper_test"

namespace android {

using testing::AllOf;
using testing::Contains;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::Key;
using testing::Pair;
using testing::Return;
using testing::VariantWith;
constexpr ui::LogicalDisplayId DISPLAY_ID = ui::LogicalDisplayId::DEFAULT;
constexpr ui::LogicalDisplayId SECONDARY_DISPLAY_ID = ui::LogicalDisplayId{DISPLAY_ID.val() + 1};
constexpr int32_t DISPLAY_WIDTH = 480;
constexpr int32_t DISPLAY_HEIGHT = 800;

namespace {

DisplayViewport createViewport() {
    DisplayViewport v;
    v.orientation = ui::Rotation::Rotation0;
    v.logicalRight = DISPLAY_HEIGHT;
    v.logicalBottom = DISPLAY_WIDTH;
    v.physicalRight = DISPLAY_HEIGHT;
    v.physicalBottom = DISPLAY_WIDTH;
    v.deviceWidth = DISPLAY_HEIGHT;
    v.deviceHeight = DISPLAY_WIDTH;
    v.isActive = true;
    return v;
}

DisplayViewport createPrimaryViewport() {
    DisplayViewport v = createViewport();
    v.displayId = DISPLAY_ID;
    v.uniqueId = "local:1";
    return v;
}

DisplayViewport createSecondaryViewport() {
    DisplayViewport v = createViewport();
    v.displayId = SECONDARY_DISPLAY_ID;
    v.uniqueId = "local:2";
    v.type = ViewportType::EXTERNAL;
    return v;
}

} // namespace

/**
 * Unit tests for RotaryEncoderInputMapper.
 */
class RotaryEncoderInputMapperTest : public InputMapperUnitTest {
protected:
    void SetUp() override { SetUp(/*bus=*/0, /*isExternal=*/false); }
    void SetUp(int bus, bool isExternal) override {
        InputMapperUnitTest::SetUp(bus, isExternal);

        EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_WHEEL))
                .WillRepeatedly(Return(true));
        EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_HWHEEL))
                .WillRepeatedly(Return(false));
        EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_WHEEL_HI_RES))
                .WillRepeatedly(Return(false));
        EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_HWHEEL_HI_RES))
                .WillRepeatedly(Return(false));
    }

    std::map<std::string, int64_t> mTelemetryLogCounts;

    /**
     * A fake function for telemetry logging.
     * Records the log counts in the `mTelemetryLogCounts` map.
     */
    std::function<void(const char*, int64_t)> mTelemetryLogCounter =
            [this](const char* key, int64_t value) { mTelemetryLogCounts[key] += value; };
};

TEST_F(RotaryEncoderInputMapperTest, ConfigureDisplayIdWithAssociatedViewport) {
    DisplayViewport primaryViewport = createPrimaryViewport();
    DisplayViewport secondaryViewport = createSecondaryViewport();
    mReaderConfiguration.setDisplayViewports({primaryViewport, secondaryViewport});

    // Set up the secondary display as the associated viewport of the mapper.
    EXPECT_CALL((*mDevice), getAssociatedViewport).WillRepeatedly(Return(secondaryViewport));
    mMapper = createInputMapper<RotaryEncoderInputMapper>(*mDeviceContext, mReaderConfiguration);

    // Ensure input events are generated for the secondary display.
    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_SCROLL),
                                     WithSource(AINPUT_SOURCE_ROTARY_ENCODER),
                                     WithDisplayId(SECONDARY_DISPLAY_ID)));
}

TEST_F(RotaryEncoderInputMapperTest, ConfigureDisplayIdNoAssociatedViewport) {
    // Set up the default display.
    mFakePolicy->clearViewports();
    mFakePolicy->addDisplayViewport(createPrimaryViewport());

    // Set up the mapper with no associated viewport.
    mMapper = createInputMapper<RotaryEncoderInputMapper>(*mDeviceContext, mReaderConfiguration);

    // Ensure input events are generated without display ID
    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_SCROLL),
                                     WithSource(AINPUT_SOURCE_ROTARY_ENCODER),
                                     WithDisplayId(ui::LogicalDisplayId::INVALID)));
}

TEST_F(RotaryEncoderInputMapperTest, ProcessRegularScroll) {
    mMapper = createInputMapper<RotaryEncoderInputMapper>(*mDeviceContext, mReaderConfiguration);

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(AllOf(WithSource(AINPUT_SOURCE_ROTARY_ENCODER),
                                     WithMotionAction(AMOTION_EVENT_ACTION_SCROLL),
                                     WithScroll(1.0f)));
}

TEST_F(RotaryEncoderInputMapperTest, ProcessHighResScroll) {
    EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_WHEEL_HI_RES))
            .WillRepeatedly(Return(true));
    mMapper = createInputMapper<RotaryEncoderInputMapper>(*mDeviceContext, mReaderConfiguration);

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL_HI_RES, 60);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(AllOf(WithSource(AINPUT_SOURCE_ROTARY_ENCODER),
                                     WithMotionAction(AMOTION_EVENT_ACTION_SCROLL),
                                     WithScroll(0.5f)));
}

TEST_F(RotaryEncoderInputMapperTest, HighResScrollIgnoresRegularScroll) {
    EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_WHEEL_HI_RES))
            .WillRepeatedly(Return(true));
    mMapper = createInputMapper<RotaryEncoderInputMapper>(*mDeviceContext, mReaderConfiguration);

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL_HI_RES, 60);
    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(AllOf(WithSource(AINPUT_SOURCE_ROTARY_ENCODER),
                                     WithMotionAction(AMOTION_EVENT_ACTION_SCROLL),
                                     WithScroll(0.5f)));
}

TEST_F(RotaryEncoderInputMapperTest, ZeroResolution_NoRotationLogging) {
    mPropertyMap.addProperty("device.res", "-3");
    mPropertyMap.addProperty("rotary_encoder.min_rotations_to_log", "2.0");
    mMapper = createInputMapper<RotaryEncoderInputMapper>(*mDeviceContext, mReaderConfiguration,
                                                          mTelemetryLogCounter);
    InputDeviceInfo info;
    mMapper->populateDeviceInfo(info);

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 700);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);

    EXPECT_THAT(mTelemetryLogCounts, IsEmpty());
}

TEST_F(RotaryEncoderInputMapperTest, NegativeMinLogRotation_NoRotationLogging) {
    mPropertyMap.addProperty("device.res", "3");
    mPropertyMap.addProperty("rotary_encoder.min_rotations_to_log", "-2.0");
    mMapper = createInputMapper<RotaryEncoderInputMapper>(*mDeviceContext, mReaderConfiguration,
                                                          mTelemetryLogCounter);
    InputDeviceInfo info;
    mMapper->populateDeviceInfo(info);

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 700);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);

    EXPECT_THAT(mTelemetryLogCounts, IsEmpty());
}

TEST_F(RotaryEncoderInputMapperTest, ZeroMinLogRotation_NoRotationLogging) {
    mPropertyMap.addProperty("device.res", "3");
    mPropertyMap.addProperty("rotary_encoder.min_rotations_to_log", "0.0");
    mMapper = createInputMapper<RotaryEncoderInputMapper>(*mDeviceContext, mReaderConfiguration,
                                                          mTelemetryLogCounter);
    InputDeviceInfo info;
    mMapper->populateDeviceInfo(info);

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 700);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);

    EXPECT_THAT(mTelemetryLogCounts, IsEmpty());
}

TEST_F(RotaryEncoderInputMapperTest, NoMinLogRotation_NoRotationLogging) {
    // 3 units per radian, 2 * M_PI * 3 = ~18.85 units per rotation.
    mPropertyMap.addProperty("device.res", "3");
    mMapper = createInputMapper<RotaryEncoderInputMapper>(*mDeviceContext, mReaderConfiguration,
                                                          mTelemetryLogCounter);
    InputDeviceInfo info;
    mMapper->populateDeviceInfo(info);

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 700);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);

    EXPECT_THAT(mTelemetryLogCounts, IsEmpty());
}

TEST_F(RotaryEncoderInputMapperTest, RotationLogging) {
    // 3 units per radian, 2 * M_PI * 3 = ~18.85 units per rotation.
    // Multiples of `unitsPerRoation`, to easily follow the assertions below.
    // [18.85, 37.7, 56.55, 75.4, 94.25, 113.1, 131.95, 150.8]
    mPropertyMap.addProperty("device.res", "3");
    mPropertyMap.addProperty("rotary_encoder.min_rotations_to_log", "2.0");

    mMapper = createInputMapper<RotaryEncoderInputMapper>(*mDeviceContext, mReaderConfiguration,
                                                          mTelemetryLogCounter);
    InputDeviceInfo info;
    mMapper->populateDeviceInfo(info);

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 15); // total scroll = 15
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    EXPECT_THAT(mTelemetryLogCounts, IsEmpty());

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 13); // total scroll = 28
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    // Expect 0 since `min_rotations_to_log` = 2, and total scroll 28 only has 1 rotation.
    EXPECT_THAT(mTelemetryLogCounts, IsEmpty());

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 10); // total scroll = 38
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    // Total scroll includes >= `min_rotations_to_log` (2), expect log.
    EXPECT_THAT(mTelemetryLogCounts,
                ElementsAre(Pair("input.value_rotary_input_device_full_rotation_count", 2)));

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, -22); // total scroll = 60
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    // Expect no additional telemetry. Total rotation is 3, and total unlogged rotation is 1, which
    // is less than `min_rotations_to_log`.
    EXPECT_THAT(mTelemetryLogCounts,
                ElementsAre(Pair("input.value_rotary_input_device_full_rotation_count", 2)));

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, -16); // total scroll = 76
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    // Total unlogged rotation >= `min_rotations_to_log` (2), so expect 2 more logged rotation.
    EXPECT_THAT(mTelemetryLogCounts,
                ElementsAre(Pair("input.value_rotary_input_device_full_rotation_count", 4)));

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, -76); // total scroll = 152
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    // Total unlogged scroll >= 4*`min_rotations_to_log`. Expect *all* unlogged rotations to be
    // logged, even if that's more than multiple of `min_rotations_to_log`.
    EXPECT_THAT(mTelemetryLogCounts,
                ElementsAre(Pair("input.value_rotary_input_device_full_rotation_count", 8)));
}

TEST_F(RotaryEncoderInputMapperTest, RotationLogging_QuarterRotation) {
    // 3 units per radian, 2 * M_PI * 3 = ~18.85 units per rotation.
    // 0.25 rotation per log should log at ~4.71 units.
    // Multiples of 4.71, to easily follow the assertions below.
    // [4.71, 9.42, 14.13, 18.85]
    mPropertyMap.addProperty("device.res", "3");
    mPropertyMap.addProperty("rotary_encoder.min_rotations_to_log", "0.25");

    mMapper = createInputMapper<RotaryEncoderInputMapper>(*mDeviceContext, mReaderConfiguration,
                                                          mTelemetryLogCounter);
    InputDeviceInfo info;
    mMapper->populateDeviceInfo(info);

    std::list<NotifyArgs> args;

    // 1. Scroll 2. Total 2. Expect no log.
    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 2);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    EXPECT_THAT(mTelemetryLogCounts, IsEmpty());

    // 2. Scroll 3. Total 5. Expect log.
    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 3);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    EXPECT_THAT(mTelemetryLogCounts,
                Contains(Key("input.value_rotary_input_device_full_rotation_count")));
    mTelemetryLogCounts.clear();

    // 3. Scroll 2. Total 7. Expect no log.
    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 2);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    EXPECT_THAT(mTelemetryLogCounts, IsEmpty());

    // 4. Scroll 3. Total 10. Expect log.
    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, -3);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    EXPECT_THAT(mTelemetryLogCounts,
                Contains(Key("input.value_rotary_input_device_full_rotation_count")));
    mTelemetryLogCounts.clear();

    // 5. Scroll 2. Total 12. Expect no log.
    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, -2);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    EXPECT_THAT(mTelemetryLogCounts, IsEmpty());

    // 6. Scroll 3. Total 15. Expect log.
    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 3);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    EXPECT_THAT(mTelemetryLogCounts,
                Contains(Key("input.value_rotary_input_device_full_rotation_count")));
}

} // namespace android