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

#include "JoystickInputMapper.h"

#include <list>
#include <optional>

#include <EventHub.h>
#include <NotifyArgs.h>
#include <ftl/enum.h>
#include <ftl/flags.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <input/DisplayViewport.h>
#include <input/EvdevAbsCode.h>
#include <input/EvdevKeyCode.h>
#include <input/KeyCode.h>
#include <input/MotionEventAxis.h>
#include <linux/input-event-codes.h>
#include <ui/LogicalDisplayId.h>

#include "InputMapperTest.h"
#include "TestConstants.h"
#include "TestEventMatchers.h"
#include "android/input.h"
#include "android/keycodes.h"

namespace android {

using namespace ftl::flag_operators;
using testing::AllOf;
using testing::ElementsAre;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::Return;
using testing::VariantWith;

class JoystickInputMapperTest : public InputMapperUnitTest {
protected:
    void SetUp() override {
        InputMapperUnitTest::SetUp();
        EXPECT_CALL(mMockEventHub, getDeviceClasses(EVENTHUB_ID))
                .WillRepeatedly(Return(InputDeviceClass::JOYSTICK | InputDeviceClass::EXTERNAL));

        // The mapper requests info on all ABS axis IDs, including ones which aren't actually used
        // (e.g. in the range from 0x0b (ABS_BRAKE) to 0x0f (ABS_HAT0X)), so just return nullopt for
        // all axes we don't explicitly set up below.
        EXPECT_CALL(mMockEventHub, getAbsoluteAxisInfo(EVENTHUB_ID, testing::_))
                .WillRepeatedly(Return(std::nullopt));

        EXPECT_CALL(mMockEventHub, hasScanCode(EVENTHUB_ID, testing::_))
                .WillRepeatedly(Return(false));

        EXPECT_CALL(mMockEventHub, mapAxis(EVENTHUB_ID, testing::_, testing::_))
                .WillRepeatedly(Return(NAME_NOT_FOUND));

        setupAxis(ABS_X, /*valid=*/true, /*min=*/-127, /*max=*/127, /*resolution=*/0);
        setupAxis(ABS_Y, /*valid=*/true, /*min=*/-127, /*max=*/127, /*resolution=*/0);
        setAxisMapping(EvdevAbsCode::X, MotionEventAxis::X);
        setAxisMapping(EvdevAbsCode::Y, MotionEventAxis::Y);
    }

    void setAxisMapping(EvdevAbsCode evdevAbs, MotionEventAxis axis) {
        EXPECT_CALL(mMockEventHub, mapAxis(EVENTHUB_ID, ftl::to_underlying(evdevAbs), testing::_))
                .WillRepeatedly([=](int32_t deviceId, int32_t axisId, AxisInfo* outAxisInfo) {
                    outAxisInfo->axis = ftl::to_underlying(axis);
                    return OK;
                });
    }

    void setAxisMapping(EvdevAbsCode evdevAbs, MotionEventAxis axis, AxisInfo::Mode mode) {
        EXPECT_CALL(mMockEventHub, mapAxis(EVENTHUB_ID, ftl::to_underlying(evdevAbs), testing::_))
                .WillRepeatedly([=](int32_t deviceId, int32_t axisId, AxisInfo* outAxisInfo) {
                    outAxisInfo->axis = ftl::to_underlying(axis);
                    outAxisInfo->mode = mode;
                    return OK;
                });
    }

    void setAxisMapping(EvdevAbsCode evdevAbs, AxisInfo axisInfo) {
        EXPECT_CALL(mMockEventHub, mapAxis(EVENTHUB_ID, ftl::to_underlying(evdevAbs), testing::_))
                .WillRepeatedly(testing::DoAll(testing::SetArgPointee<2>(axisInfo), Return(OK)));
    }

    void setKeyMapping(EvdevKeyCode evdevKey, KeyCode keyCode) {
        setKeyMapping(evdevKey, keyCode, keyCode);
    }

    void setKeyMapping(EvdevKeyCode evdevKey, KeyCode keyCode, KeyCode originalKeyCode) {
        auto rawEvdevKey = ftl::to_underlying(evdevKey);
        expectScanCodes(true, {rawEvdevKey});

        EXPECT_CALL(mMockEventHub, mapKey(EVENTHUB_ID, rawEvdevKey, 0, 0))
                .WillRepeatedly(Return(MappedKey{
                        .keyCode = ftl::to_underlying(keyCode),
                        .originalKeyCode = originalKeyCode,
                        .metaState = 0,
                        .flags = 0u,
                }));
    }
};

TEST_F(JoystickInputMapperTest, Configure_AssignsDisplayUniqueId) {
    DisplayViewport viewport;
    viewport.displayId = ui::LogicalDisplayId{1};
    EXPECT_CALL((*mDevice), getAssociatedViewport).WillRepeatedly(Return(viewport));
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext,
                                                     mFakePolicy->getReaderConfiguration());

    std::list<NotifyArgs> out;

    // Send an axis event
    process(EV_ABS, ABS_X, 100);
    mFakeListener.assertNoEvents();
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(WithDisplayId(viewport.displayId));

    // Send another axis event
    process(EV_ABS, ABS_Y, 100);
    mFakeListener.assertNoEvents();
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(WithDisplayId(viewport.displayId));
}

TEST_F(JoystickInputMapperTest, Configure_AssignsGenericAxisIdsToUnmappedAxes) {
    setupAxis(ABS_GAS, /*valid=*/true, /*min=*/0, /*max=*/100, /*resolution=*/0);
    setupAxis(ABS_BRAKE, /*valid=*/true, /*min=*/0, /*max=*/100, /*resolution=*/0);
    setupAxis(ABS_RZ, /*valid=*/true, /*min=*/0, /*max=*/100, /*resolution=*/0);

    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);

    process(EV_ABS, ABS_GAS, 25);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{AMOTION_EVENT_AXIS_GENERIC_1, 0.25f},
                                               {AMOTION_EVENT_AXIS_GENERIC_2, 0.f},
                                               {AMOTION_EVENT_AXIS_GENERIC_3, 0.f}})));

    process(EV_ABS, ABS_BRAKE, 50);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{AMOTION_EVENT_AXIS_GENERIC_1, 0.25f},
                                               {AMOTION_EVENT_AXIS_GENERIC_2, 0.5f},
                                               {AMOTION_EVENT_AXIS_GENERIC_3, 0.0f}})));

    process(EV_ABS, ABS_RZ, 75);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{AMOTION_EVENT_AXIS_GENERIC_1, 0.25f},
                                               {AMOTION_EVENT_AXIS_GENERIC_2, 0.5f},
                                               {AMOTION_EVENT_AXIS_GENERIC_3, 0.75f}})));
}

TEST_F(JoystickInputMapperTest, MappedAxes_PrioritizesMostRecentUpdate) {
    // Two raw axes, ABS_X and ABS_Y, will be mapped to the same logical axis,
    // AMOTION_EVENT_AXIS_X. We need to ensure that the most recent event is the one
    // that gets reported.
    AxisInfo axisInfo;
    axisInfo.axis = AMOTION_EVENT_AXIS_X;
    EXPECT_CALL(mMockEventHub, mapAxis(EVENTHUB_ID, ABS_X, testing::_))
            .WillRepeatedly(testing::DoAll(testing::SetArgPointee<2>(axisInfo), Return(false)));
    EXPECT_CALL(mMockEventHub, mapAxis(EVENTHUB_ID, ABS_Y, testing::_))
            .WillRepeatedly(testing::DoAll(testing::SetArgPointee<2>(axisInfo), Return(false)));

    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext,
                                                     mFakePolicy->getReaderConfiguration());
    nsecs_t eventTime = ARBITRARY_TIME;

    // Send an event for ABS_X with a value of 0 (maps to 0).
    process(eventTime, EV_ABS, ABS_X, 0);
    // Send an event for ABS_Y at a later time with a value of 127 (maps to 1.0).
    eventTime++;
    process(eventTime, EV_ABS, ABS_Y, 127);

    // Sync and verify: ABS_Y value should be taken
    process(eventTime, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{AMOTION_EVENT_AXIS_X, 1.0f}})));

    // Reset and test reverse order of events
    ASSERT_THAT(mMapper->reset(eventTime), IsEmpty());
    // Send an event for ABS_Y with a value of 127 (maps to 1.0).
    process(eventTime, EV_ABS, ABS_Y, 127);
    // Send an event for ABS_X at a later time with a value of 0 (maps to 0).
    eventTime++;
    process(eventTime, EV_ABS, ABS_X, 0);

    // Sync and verify: ABS_X value should be taken
    process(eventTime, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{AMOTION_EVENT_AXIS_X, 0}})));
}

TEST_F(JoystickInputMapperTest, AxisRemappedToUnknown_DoesNotGenerateMotionEvents) {
    setupAxis(ABS_GAS, /*valid=*/true, /*min=*/0, /*max=*/255, /*resolution=*/0);
    setAxisMapping(EvdevAbsCode::GAS, MotionEventAxis::UNKNOWN);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);

    process(EV_ABS, ABS_GAS, 100);
    process(EV_SYN, SYN_REPORT, 0);

    mFakeListener.assertNoEvents();
}

TEST_F(JoystickInputMapperTest,
       AxisRemappedToUnknown_PopulateDeviceInfo_DoesNotReturnDisabledMotionRange) {
    setupAxis(ABS_GAS, /*valid=*/true, /*min=*/0, /*max=*/255, /*resolution=*/4);
    setAxisMapping(EvdevAbsCode::GAS, MotionEventAxis::UNKNOWN);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    InputDeviceInfo info;

    mMapper->populateDeviceInfo(info);

    EXPECT_EQ(info.getMotionRange(ftl::to_underlying(MotionEventAxis::UNKNOWN),
                                  AINPUT_SOURCE_JOYSTICK),
              nullptr);
    EXPECT_EQ(info.getMotionRange(ftl::to_underlying(MotionEventAxis::GAS),
                                  AINPUT_SOURCE_JOYSTICK),
              nullptr);
}

TEST_F(JoystickInputMapperTest, AxisRemapping_SplitAxis_LowDisabled) {
    setupAxis(ABS_THROTTLE, /*valid=*/true, /*min=*/0, /*max=*/100, /*resolution=*/4);
    AxisInfo axisInfo;
    axisInfo.axis = ftl::to_underlying(MotionEventAxis::UNKNOWN);
    axisInfo.highAxis = ftl::to_underlying(MotionEventAxis::BRAKE);
    axisInfo.mode = AxisInfo::MODE_SPLIT;
    axisInfo.splitValue = 50;
    setAxisMapping(EvdevAbsCode::THROTTLE, axisInfo);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);

    // Low axis events are not generated.
    process(EV_ABS, ABS_THROTTLE, 48);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.assertNoEvents();

    // High axis events are generated.
    process(EV_ABS, ABS_THROTTLE, 90);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{MotionEventAxis::BRAKE, 0.8f}})));

    // Value below split value resets high axis value.
    process(EV_ABS, ABS_THROTTLE, 45);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{MotionEventAxis::BRAKE, 0.f}})));
}

TEST_F(JoystickInputMapperTest, AxisRemapping_SplitAxis_LowDisabled_ReturnsCorrectMotionRanges) {
    setupAxis(ABS_THROTTLE, /*valid=*/true, /*min=*/0, /*max=*/255, /*resolution=*/4);
    AxisInfo axisInfo;
    axisInfo.axis = ftl::to_underlying(MotionEventAxis::UNKNOWN);
    axisInfo.highAxis = ftl::to_underlying(MotionEventAxis::GAS);
    axisInfo.mode = AxisInfo::MODE_SPLIT;
    axisInfo.splitValue = 128;
    setAxisMapping(EvdevAbsCode::THROTTLE, axisInfo);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    InputDeviceInfo info;

    mMapper->populateDeviceInfo(info);

    auto unknownRange = info.getMotionRange(ftl::to_underlying(MotionEventAxis::UNKNOWN),
                                            AINPUT_SOURCE_JOYSTICK);
    auto gasRange =
            info.getMotionRange(ftl::to_underlying(MotionEventAxis::GAS), AINPUT_SOURCE_JOYSTICK);
    EXPECT_EQ(unknownRange, nullptr);
    ASSERT_NE(gasRange, nullptr);
    EXPECT_EQ(gasRange->min, 0);
    EXPECT_EQ(gasRange->max, 1);
}

TEST_F(JoystickInputMapperTest, AxisRemapping_SplitAxis_HighDisabled) {
    setupAxis(ABS_THROTTLE, /*valid=*/true, /*min=*/0, /*max=*/200, /*resolution=*/4);
    AxisInfo axisInfo;
    axisInfo.axis = ftl::to_underlying(MotionEventAxis::BRAKE);
    axisInfo.highAxis = ftl::to_underlying(MotionEventAxis::UNKNOWN);
    axisInfo.mode = AxisInfo::MODE_SPLIT;
    axisInfo.splitValue = 100;
    setAxisMapping(EvdevAbsCode::THROTTLE, axisInfo);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);

    // High axis events are not generated.
    process(EV_ABS, ABS_THROTTLE, 101);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.assertNoEvents();

    // Low axis events are generated.
    process(EV_ABS, ABS_THROTTLE, 70);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{MotionEventAxis::BRAKE, 0.3f}})));

    // Value above split value resets low axis value.
    process(EV_ABS, ABS_THROTTLE, 110);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{MotionEventAxis::BRAKE, 0.0f}})));
}

TEST_F(JoystickInputMapperTest, AxisRemapping_SplitAxis_HighDisabled_ReturnsCorrectMotionRanges) {
    setupAxis(ABS_THROTTLE, /*valid=*/true, /*min=*/0, /*max=*/255, /*resolution=*/4);
    AxisInfo axisInfo;
    axisInfo.axis = ftl::to_underlying(MotionEventAxis::BRAKE);
    axisInfo.highAxis = ftl::to_underlying(MotionEventAxis::UNKNOWN);
    axisInfo.mode = AxisInfo::MODE_SPLIT;
    axisInfo.splitValue = 128;
    setAxisMapping(EvdevAbsCode::THROTTLE, axisInfo);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    InputDeviceInfo info;

    mMapper->populateDeviceInfo(info);

    auto unknownRange = info.getMotionRange(ftl::to_underlying(MotionEventAxis::UNKNOWN),
                                            AINPUT_SOURCE_JOYSTICK);
    auto brakeRange =
            info.getMotionRange(ftl::to_underlying(MotionEventAxis::BRAKE), AINPUT_SOURCE_JOYSTICK);
    EXPECT_EQ(unknownRange, nullptr);
    ASSERT_NE(brakeRange, nullptr);
    EXPECT_EQ(brakeRange->min, 0);
    EXPECT_EQ(brakeRange->max, 1);
}

TEST_F(JoystickInputMapperTest,
       ButtonToAxisMapping_remappedToExistingAxis_keyGeneratesMotionEvents) {
    setupAxis(ABS_GAS, /*valid=*/true, /*min=*/0, /*max=*/255, /*resolution=*/0);
    setAxisMapping(EvdevAbsCode::GAS, MotionEventAxis::GAS);
    setKeyMapping(EvdevKeyCode::SOUTH, KeyCode::BUTTON_A);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::GAS}};
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    process(299, EV_KEY, BTN_SOUTH, 1);
    process(301, EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{AMOTION_EVENT_AXIS_GAS, 1.0f}}),
                                     WithEventTime(301)));

    process(305, EV_KEY, BTN_SOUTH, 0);
    process(311, EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{AMOTION_EVENT_AXIS_GAS, 0.0f}}),
                                     WithEventTime(311)));
}

TEST_F(JoystickInputMapperTest,
       ButtonToAxisMapping_remappedToNonExistingAxis_keyGeneratesMotionEvents) {
    setKeyMapping(EvdevKeyCode::SOUTH, KeyCode::BUTTON_A);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::GAS}};
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    process(EV_KEY, BTN_SOUTH, 1);
    process(EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{AMOTION_EVENT_AXIS_GAS, 1.0f}})));

    process(EV_KEY, BTN_SOUTH, 0);
    process(EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{AMOTION_EVENT_AXIS_GAS, 0.0f}})));
}

TEST_F(JoystickInputMapperTest,
       ButtonToAxisMapping_sameKeyRemappedToAnotherKey_generatesMotionEvents) {
    setupAxis(ABS_GAS, /*valid=*/true, /*min=*/0, /*max=*/255, /*resolution=*/0);
    setAxisMapping(EvdevAbsCode::GAS, MotionEventAxis::GAS);
    setKeyMapping(EvdevKeyCode::SOUTH, KeyCode::BUTTON_B, /*originalKeyCode=*/KeyCode::BUTTON_A);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::GAS}};
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    process(EV_KEY, BTN_SOUTH, 1);
    process(EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{AMOTION_EVENT_AXIS_GAS, 1.0f}})));

    process(EV_KEY, BTN_SOUTH, 0);
    process(EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{AMOTION_EVENT_AXIS_GAS, 0.0f}})));
}

TEST_F(JoystickInputMapperTest,
       ButtonToAxisMapping_otherKeyRemappedToSameKey_otherKeyDoesNotGenerateEvents) {
    setupAxis(ABS_GAS, /*valid=*/true, /*min=*/0, /*max=*/255, /*resolution=*/0);
    setAxisMapping(EvdevAbsCode::GAS, MotionEventAxis::GAS);
    setKeyMapping(EvdevKeyCode::SOUTH, KeyCode::BUTTON_A, /*originalKeyCode=*/KeyCode::BUTTON_B);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::GAS}};
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    process(EV_KEY, BTN_SOUTH, 1);
    process(EV_SYN, SYN_REPORT, 0);
    process(EV_KEY, BTN_SOUTH, 0);
    process(EV_SYN, SYN_REPORT, 0);

    mFakeListener.assertNoEvents();
}

TEST_F(JoystickInputMapperTest,
       ButtonToAxisMapping_multipleButtonsDown_motionEventsHaveMultipleAxes) {
    setKeyMapping(EvdevKeyCode::SOUTH, KeyCode::BUTTON_A);
    setKeyMapping(EvdevKeyCode::EAST, KeyCode::BUTTON_B);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::GAS},
            {KeyCode::BUTTON_B, MotionEventAxis::BRAKE},

    };
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    // Press the first button.
    process(EV_KEY, BTN_SOUTH, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                  WithAxes({{MotionEventAxis::GAS, 1.0f}, {MotionEventAxis::BRAKE, 0}})));

    // Press the second button.
    process(EV_KEY, BTN_EAST, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                  WithAxes({{MotionEventAxis::GAS, 1.0f}, {MotionEventAxis::BRAKE, 1.0f}})));

    // Release the first button.
    process(EV_KEY, BTN_SOUTH, 0);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                  WithAxes({{MotionEventAxis::GAS, 0}, {MotionEventAxis::BRAKE, 1.0f}})));

    // Release the second button.
    process(EV_KEY, BTN_EAST, 0);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                  WithAxes({{MotionEventAxis::GAS, 0}, {MotionEventAxis::BRAKE, 0}})));
}

TEST_F(JoystickInputMapperTest, ButtonToAxisMapping_buttonThenAxisPressed_axisValueGetsUpdated) {
    setupAxis(ABS_GAS, /*valid=*/true, /*min=*/0, /*max=*/100, /*resolution=*/0);
    setAxisMapping(EvdevAbsCode::GAS, MotionEventAxis::LTRIGGER);
    setKeyMapping(EvdevKeyCode::THUMBL, KeyCode::BUTTON_L1);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_L1, MotionEventAxis::LTRIGGER}};
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    // Press the button.
    process(EV_KEY, BTN_THUMBL, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{MotionEventAxis::LTRIGGER, 1.0f}})));

    // Now press the physical axis. The axis gets overridden.
    process(EV_ABS, ABS_GAS, 75);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{MotionEventAxis::LTRIGGER, 0.75f}})));

    // Release the button. The axis gets overridden again.
    process(EV_KEY, BTN_THUMBL, 0);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{MotionEventAxis::LTRIGGER, 0}})));

    // Release the axis. The value hasn't changed since the last event.
    process(EV_ABS, ABS_GAS, 0);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.assertNoEvents();
}

TEST_F(JoystickInputMapperTest, ButtonToAxisMapping_keyMappedToInvertedAxis) {
    setupAxis(ABS_THROTTLE, /*valid=*/true, /*min=*/0, /*max=*/255, /*resolution=*/0);
    AxisInfo axisInfo;
    axisInfo.axis = AMOTION_EVENT_AXIS_THROTTLE;
    axisInfo.mode = AxisInfo::MODE_INVERT;
    setAxisMapping(EvdevAbsCode::THROTTLE, axisInfo);
    setKeyMapping(EvdevKeyCode::SOUTH, KeyCode::BUTTON_A);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::THROTTLE}};
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    process(EV_KEY, BTN_SOUTH, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{MotionEventAxis::THROTTLE, 1.f}})));

    process(EV_KEY, BTN_SOUTH, 0);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{MotionEventAxis::THROTTLE, 0.f}})));
}

TEST_F(JoystickInputMapperTest, ButtonToAxisMapping_keyMappedToSplitAxis) {
    setupAxis(ABS_THROTTLE, /*valid=*/true, /*min=*/0, /*max=*/255, /*resolution=*/4);
    AxisInfo axisInfo;
    axisInfo.axis = AMOTION_EVENT_AXIS_BRAKE;
    axisInfo.highAxis = AMOTION_EVENT_AXIS_GAS;
    axisInfo.mode = AxisInfo::MODE_SPLIT;
    axisInfo.splitValue = 128;
    setAxisMapping(EvdevAbsCode::THROTTLE, axisInfo);
    setKeyMapping(EvdevKeyCode::SOUTH, KeyCode::BUTTON_A);
    setKeyMapping(EvdevKeyCode::EAST, KeyCode::BUTTON_B);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {{KeyCode::BUTTON_A,
                                                                    MotionEventAxis::BRAKE},
                                                                   {KeyCode::BUTTON_B,
                                                                    MotionEventAxis::GAS}};
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    process(EV_KEY, BTN_EAST, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                  WithAxes({{MotionEventAxis::BRAKE, 0}, {MotionEventAxis::GAS, 1}})));

    process(EV_KEY, BTN_SOUTH, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                  WithAxes({{MotionEventAxis::BRAKE, 1}, {MotionEventAxis::GAS, 1}})));

    process(EV_ABS, ABS_THROTTLE, 100);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                  WithAxes({{MotionEventAxis::BRAKE, 0.219}, {MotionEventAxis::GAS, 0}})));

    process(EV_ABS, ABS_THROTTLE, 200);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                  WithAxes({{MotionEventAxis::BRAKE, 0}, {MotionEventAxis::GAS, 0.567f}})));

    process(EV_KEY, BTN_EAST, 0);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                  WithAxes({{MotionEventAxis::BRAKE, 0}, {MotionEventAxis::GAS, 0}})));
}

TEST_F(JoystickInputMapperTest,
       ButtonToAxisMapping_keyMappedToSplitAxis_populateDeviceInfo_hasCorrectMotionRanges) {
    setupAxis(ABS_THROTTLE, /*valid=*/true, /*min=*/0, /*max=*/255, /*resolution=*/4);
    AxisInfo axisInfo;
    axisInfo.axis = AMOTION_EVENT_AXIS_BRAKE;
    axisInfo.highAxis = AMOTION_EVENT_AXIS_GAS;
    axisInfo.mode = AxisInfo::MODE_SPLIT;
    axisInfo.splitValue = 128;
    setAxisMapping(EvdevAbsCode::THROTTLE, axisInfo);
    setKeyMapping(EvdevKeyCode::SOUTH, KeyCode::BUTTON_A);
    setKeyMapping(EvdevKeyCode::EAST, KeyCode::BUTTON_B);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {{KeyCode::BUTTON_A,
                                                                    MotionEventAxis::BRAKE},
                                                                   {KeyCode::BUTTON_B,
                                                                    MotionEventAxis::GAS}};
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    InputDeviceInfo info;
    mMapper->populateDeviceInfo(info);

    auto brakeRange = info.getMotionRange(AMOTION_EVENT_AXIS_BRAKE, AINPUT_SOURCE_JOYSTICK);
    ASSERT_TRUE(brakeRange != nullptr);
    EXPECT_EQ(brakeRange->min, 0.0f);
    EXPECT_EQ(brakeRange->max, 1.0f);

    auto gasRange = info.getMotionRange(AMOTION_EVENT_AXIS_GAS, AINPUT_SOURCE_JOYSTICK);
    ASSERT_TRUE(gasRange != nullptr);
    EXPECT_EQ(gasRange->min, 0.0f);
    EXPECT_EQ(gasRange->max, 1.0f);
}

TEST_F(JoystickInputMapperTest,
       ButtonToAxisMapping_keyMappedToExistingCenteredAxis_populateDeviceInfo_returnsMotionRange) {
    setKeyMapping(EvdevKeyCode::SOUTH, KeyCode::BUTTON_A);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::X}};
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    InputDeviceInfo info;
    mMapper->populateDeviceInfo(info);

    auto range = info.getMotionRange(AMOTION_EVENT_AXIS_X, AINPUT_SOURCE_JOYSTICK);
    ASSERT_TRUE(range != nullptr);
    EXPECT_EQ(range->min, -1.0f);
    EXPECT_EQ(range->max, 1.0f);
}

TEST_F(JoystickInputMapperTest,
       ButtonToAxisMapping_keyMappedToNonExistingCenteredAxis_returnsCorrectMotionRange) {
    setKeyMapping(EvdevKeyCode::SOUTH, KeyCode::BUTTON_A);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::HAT_Y}};
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    InputDeviceInfo info;
    mMapper->populateDeviceInfo(info);

    auto range = info.getMotionRange(AMOTION_EVENT_AXIS_HAT_Y, AINPUT_SOURCE_JOYSTICK);
    ASSERT_TRUE(range != nullptr);
    EXPECT_EQ(range->min, -1.f);
    EXPECT_EQ(range->max, 1.0f);
}

TEST_F(JoystickInputMapperTest,
       ButtonToAxisMapping_reconfigureWithChangedMappings_eventsHaveCorrectAxes) {
    setKeyMapping(EvdevKeyCode::SOUTH, KeyCode::BUTTON_A);
    setKeyMapping(EvdevKeyCode::EAST, KeyCode::BUTTON_B);

    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);

    // Initial configuration.
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::GAS},
            {KeyCode::BUTTON_B, MotionEventAxis::BRAKE},
    };
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    // Press BTN_SOUTH, expect GAS motion event.
    process(EV_KEY, BTN_SOUTH, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                  WithAxes({{MotionEventAxis::GAS, 1.0f}, {MotionEventAxis::BRAKE, 0.0f}})));
    // Press BTN_EAST, expect BRAKE motion event. Both axes are now active.
    process(EV_KEY, BTN_EAST, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                  WithAxes({{MotionEventAxis::GAS, 1.0f}, {MotionEventAxis::BRAKE, 1.0f}})));
    mFakeListener.assertNoEvents();

    // Reconfigure: change one mapping, remove another.
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::THROTTLE},
    };
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    process(EV_KEY, BTN_SOUTH, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE),
                                     WithAxes({{MotionEventAxis::THROTTLE, 1.0f},
                                               {MotionEventAxis::BRAKE, 0.0f},
                                               {MotionEventAxis::GAS, 0.0f}})));

    process(EV_KEY, BTN_EAST, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.assertNoEvents();
}

TEST_F(JoystickInputMapperTest,
       ButtonToAxisMapping_reconfigureWithChangedMappings_returnsCorrectMotionRange) {
    setKeyMapping(EvdevKeyCode::SOUTH, KeyCode::BUTTON_A);
    setKeyMapping(EvdevKeyCode::EAST, KeyCode::BUTTON_B);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    // Initial configuration.
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::GAS},
            {KeyCode::BUTTON_B, MotionEventAxis::BRAKE},
    };
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    {
        InputDeviceInfo info;
        mMapper->populateDeviceInfo(info);
        auto gasRange = info.getMotionRange(AMOTION_EVENT_AXIS_GAS, AINPUT_SOURCE_JOYSTICK);
        ASSERT_NE(gasRange, nullptr);
        EXPECT_EQ(gasRange->min, 0.0f);
        EXPECT_EQ(gasRange->max, 1.0f);
        auto brakeRange = info.getMotionRange(AMOTION_EVENT_AXIS_BRAKE, AINPUT_SOURCE_JOYSTICK);
        ASSERT_NE(brakeRange, nullptr);
        EXPECT_EQ(brakeRange->min, 0.0f);
        EXPECT_EQ(brakeRange->max, 1.0f);
        auto throttleRange =
                info.getMotionRange(AMOTION_EVENT_AXIS_THROTTLE, AINPUT_SOURCE_JOYSTICK);
        ASSERT_EQ(throttleRange, nullptr);
    }

    // Reconfigure: change one mapping, remove another.
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::THROTTLE},
    };
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    {
        InputDeviceInfo info;
        mMapper->populateDeviceInfo(info);
        auto gasRange = info.getMotionRange(AMOTION_EVENT_AXIS_GAS, AINPUT_SOURCE_JOYSTICK);
        ASSERT_EQ(gasRange, nullptr);
        auto brakeRange = info.getMotionRange(AMOTION_EVENT_AXIS_BRAKE, AINPUT_SOURCE_JOYSTICK);
        ASSERT_EQ(brakeRange, nullptr);
        auto throttleRange =
                info.getMotionRange(AMOTION_EVENT_AXIS_THROTTLE, AINPUT_SOURCE_JOYSTICK);
        ASSERT_NE(throttleRange, nullptr);
        EXPECT_EQ(throttleRange->min, 0.0f);
    }
}

TEST_F(JoystickInputMapperTest,
       ButtonToAxisMapping_reconfigureWithChangedMappings_bumpsGeneration) {
    setKeyMapping(EvdevKeyCode::SOUTH, KeyCode::BUTTON_A);
    setKeyMapping(EvdevKeyCode::EAST, KeyCode::BUTTON_B);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    auto initialGeneration = mDevice->getGeneration();
    // Initial configuration.
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::GAS},
    };
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    EXPECT_EQ(mDevice->getGeneration(), initialGeneration + 1);

    // Reconfigure: change one mapping, remove another.
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::THROTTLE},
    };
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    EXPECT_EQ(mDevice->getGeneration(), initialGeneration + 2);
}

TEST_F(JoystickInputMapperTest, Dump) {
    setupAxis(ABS_GAS, /*valid=*/true, /*min=*/0, /*max=*/255, /*resolution=*/0);
    setupAxis(ABS_THROTTLE, /*valid=*/true, /*min=*/0, /*max=*/255, /*resolution=*/0);
    setAxisMapping(EvdevAbsCode::GAS, MotionEventAxis::GAS);
    setAxisMapping(EvdevAbsCode::THROTTLE, MotionEventAxis::THROTTLE);
    setKeyMapping(EvdevKeyCode::SOUTH, KeyCode::BUTTON_A);
    setKeyMapping(EvdevKeyCode::EAST, KeyCode::BUTTON_B);
    mMapper = createInputMapper<JoystickInputMapper>(*mDeviceContext, mReaderConfiguration);
    mReaderConfiguration.keyToAxisRemappingPerDevice[DEVICE_ID] = {
            {KeyCode::BUTTON_A, MotionEventAxis::GAS},
            {KeyCode::BUTTON_B, MotionEventAxis::THROTTLE},
    };
    processArgs(mMapper->reconfigure(ARBITRARY_TIME, mReaderConfiguration,
                                     InputReaderConfiguration::Change::AXIS_REMAPPING));

    std::string dump;
    mMapper->dump(dump);

    ASSERT_THAT(dump, HasSubstr("Joystick Input Mapper"));
    ASSERT_THAT(dump, HasSubstr("Axes:"));
    ASSERT_THAT(dump, HasSubstr("GAS:"));
    ASSERT_THAT(dump, HasSubstr("THROTTLE:"));

    ASSERT_THAT(dump, HasSubstr("Key to Axis Remappings:"));
    ASSERT_THAT(dump, HasSubstr("BTN_GAMEPAD -> ABS_GAS"));
    ASSERT_THAT(dump, HasSubstr("BTN_EAST -> ABS_THROTTLE"));
}

} // namespace android
