/*
 * Copyright 2023 The Android Open Source Project
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

#include "CursorInputMapper.h"

#include <optional>
#include <string>
#include <tuple>

#include <android-base/logging.h>
#include <android/configuration.h>
#include <com_android_input_flags.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <input/AccelerationCurve.h>
#include <input/DisplayViewport.h>
#include <input/Input.h>
#include <input/ScopedFlagOverride.h>
#include <linux/input-event-codes.h>
#include <linux/input.h>
#include <utils/Timers.h>

#include "InputMapperTest.h"
#include "InputReaderBase.h"
#include "InterfaceMocks.h"
#include "NotifyArgs.h"
#include "TestEventMatchers.h"
#include "ui/Rotation.h"

#define TAG "CursorInputMapper_test"

namespace input_flags = com::android::input::flags;

namespace android {

using testing::AllOf;
using testing::Return;
using testing::VariantWith;
constexpr auto ACTION_DOWN = AMOTION_EVENT_ACTION_DOWN;
constexpr auto ACTION_MOVE = AMOTION_EVENT_ACTION_MOVE;
constexpr auto ACTION_UP = AMOTION_EVENT_ACTION_UP;
constexpr auto BUTTON_PRESS = AMOTION_EVENT_ACTION_BUTTON_PRESS;
constexpr auto BUTTON_RELEASE = AMOTION_EVENT_ACTION_BUTTON_RELEASE;
constexpr auto HOVER_MOVE = AMOTION_EVENT_ACTION_HOVER_MOVE;
constexpr auto INVALID_CURSOR_POSITION = AMOTION_EVENT_INVALID_CURSOR_POSITION;
constexpr auto AXIS_X = AMOTION_EVENT_AXIS_X;
constexpr auto AXIS_Y = AMOTION_EVENT_AXIS_Y;
constexpr auto AXIS_RELATIVE_X = AMOTION_EVENT_AXIS_RELATIVE_X;
constexpr auto AXIS_RELATIVE_Y = AMOTION_EVENT_AXIS_RELATIVE_Y;
constexpr ui::LogicalDisplayId DISPLAY_ID = ui::LogicalDisplayId::DEFAULT;
constexpr ui::LogicalDisplayId SECONDARY_DISPLAY_ID = ui::LogicalDisplayId{DISPLAY_ID.val() + 1};
constexpr int32_t DISPLAY_WIDTH = 480;
constexpr int32_t DISPLAY_HEIGHT = 800;

constexpr int32_t TRACKBALL_MOVEMENT_THRESHOLD = 6;

namespace {

DisplayViewport createPrimaryViewport(ui::Rotation orientation,
                                      int32_t densityDpi = ACONFIGURATION_DENSITY_XHIGH,
                                      float xDpi = ACONFIGURATION_DENSITY_NONE,
                                      float yDpi = ACONFIGURATION_DENSITY_NONE) {
    const bool isRotated =
            orientation == ui::Rotation::Rotation90 || orientation == ui::Rotation::Rotation270;
    DisplayViewport v;
    v.displayId = DISPLAY_ID;
    v.orientation = orientation;
    v.logicalRight = isRotated ? DISPLAY_HEIGHT : DISPLAY_WIDTH;
    v.logicalBottom = isRotated ? DISPLAY_WIDTH : DISPLAY_HEIGHT;
    v.physicalRight = isRotated ? DISPLAY_HEIGHT : DISPLAY_WIDTH;
    v.physicalBottom = isRotated ? DISPLAY_WIDTH : DISPLAY_HEIGHT;
    v.deviceWidth = isRotated ? DISPLAY_HEIGHT : DISPLAY_WIDTH;
    v.deviceHeight = isRotated ? DISPLAY_WIDTH : DISPLAY_HEIGHT;
    v.isActive = true;
    v.uniqueId = "local:1";
    v.densityDpi = densityDpi;
    v.xDpi = xDpi;
    v.yDpi = yDpi;
    return v;
}

DisplayViewport createSecondaryViewport() {
    DisplayViewport v;
    v.displayId = SECONDARY_DISPLAY_ID;
    v.orientation = ui::Rotation::Rotation0;
    v.logicalRight = DISPLAY_HEIGHT;
    v.logicalBottom = DISPLAY_WIDTH;
    v.physicalRight = DISPLAY_HEIGHT;
    v.physicalBottom = DISPLAY_WIDTH;
    v.deviceWidth = DISPLAY_HEIGHT;
    v.deviceHeight = DISPLAY_WIDTH;
    v.isActive = true;
    v.uniqueId = "local:2";
    v.type = ViewportType::EXTERNAL;
    return v;
}

// In a number of these tests, we want to check that some pointer motion is reported without
// specifying an exact value, as that would require updating the tests every time the pointer
// ballistics was changed. To do this, we make some matchers that only check the sign of a
// particular axis.
MATCHER_P(WithPositiveAxis, axis, "MotionEvent with a positive axis value") {
    *result_listener << "expected 1 pointer with a positive " << MotionEvent::getLabelOrCode(axis)
                     << " axis but got " << arg.pointerCoords.size()
                     << " pointers, with axis value " << arg.pointerCoords[0].getAxisValue(axis);
    return arg.pointerCoords.size() == 1 && arg.pointerCoords[0].getAxisValue(axis) > 0;
}

MATCHER_P(WithZeroAxis, axis, "MotionEvent with a zero axis value") {
    *result_listener << "expected 1 pointer with a zero " << MotionEvent::getLabelOrCode(axis)
                     << " axis but got " << arg.pointerCoords.size()
                     << " pointers, with axis value " << arg.pointerCoords[0].getAxisValue(axis);
    return arg.pointerCoords.size() == 1 && arg.pointerCoords[0].getAxisValue(axis) == 0;
}

MATCHER_P(WithNegativeAxis, axis, "MotionEvent with a negative axis value") {
    *result_listener << "expected 1 pointer with a negative " << MotionEvent::getLabelOrCode(axis)
                     << " axis but got " << arg.pointerCoords.size()
                     << " pointers, with axis value " << arg.pointerCoords[0].getAxisValue(axis);
    return arg.pointerCoords.size() == 1 && arg.pointerCoords[0].getAxisValue(axis) < 0;
}

} // namespace

/**
 * Unit tests for CursorInputMapper.
 * These classes are named 'CursorInputMapperUnitTest...' to avoid name collision with the existing
 * 'CursorInputMapperTest...' classes. If all of the CursorInputMapper tests are migrated here, the
 * name can be simplified to 'CursorInputMapperTest'.
 *
 * TODO(b/283812079): move the remaining CursorInputMapper tests here. The ones that are left all
 *   depend on viewport association, for which we'll need to fake InputDeviceContext.
 */
class CursorInputMapperUnitTestBase : public VerifyingInputMapperUnitTest {
protected:
    void SetUp() override { SetUp(BUS_USB, /*isExternal=*/false); }
    void SetUp(int bus, bool isExternal) override {
        VerifyingInputMapperUnitTest::SetUp(bus, isExternal);

        // Current scan code state - all keys are UP by default
        setScanCodeState(KeyState::UP,
                         {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_BACK, BTN_SIDE, BTN_FORWARD,
                          BTN_EXTRA, BTN_TASK});
        EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_WHEEL))
                .WillRepeatedly(Return(false));
        EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_HWHEEL))
                .WillRepeatedly(Return(false));
        EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_WHEEL_HI_RES))
                .WillRepeatedly(Return(false));
        EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_HWHEEL_HI_RES))
                .WillRepeatedly(Return(false));

        mFakePolicy->setDefaultPointerDisplayId(DISPLAY_ID);
        mFakePolicy->addDisplayViewport(createPrimaryViewport(ui::Rotation::Rotation0));
    }

    void createMapper() {
        mMapper = createInputMapper<CursorInputMapper>(*mDeviceContext, mReaderConfiguration);
    }

    void setPointerCapture(bool enabled) {
        mReaderConfiguration.pointerCaptureRequest.window = enabled ? sp<BBinder>::make() : nullptr;
        mReaderConfiguration.pointerCaptureRequest.mode =
                enabled ? PointerCaptureMode::ABSOLUTE : PointerCaptureMode::UNCAPTURED;
        mReaderConfiguration.pointerCaptureRequest.seq = 1;
        int32_t generation = mDevice->getGeneration();
        reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                          InputReaderConfiguration::Change::POINTER_CAPTURE);
        mFakeListener.expectDeviceReset(
                AllOf(WithDeviceId(DEVICE_ID), WithEventTime(ARBITRARY_TIME)));

        // Check that generation also got bumped
        ASSERT_GT(mDevice->getGeneration(), generation);
    }

    void testRotation(int32_t originalX, int32_t originalY,
                      const testing::Matcher<NotifyMotionArgs>& coordsMatcher) {
        process(ARBITRARY_TIME, EV_REL, REL_X, originalX);
        process(ARBITRARY_TIME, EV_REL, REL_Y, originalY);
        process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
        mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_MOVE), coordsMatcher));
    }
};

class CursorInputMapperUnitTest : public CursorInputMapperUnitTestBase {
protected:
    void SetUp() override {
        CursorInputMapperUnitTestBase::SetUp();
    }
};

TEST_F(CursorInputMapperUnitTest, GetSourcesReturnsMouseInPointerMode) {
    mPropertyMap.addProperty("cursor.mode", "pointer");
    createMapper();

    ASSERT_EQ(AINPUT_SOURCE_MOUSE, mMapper->getSources());
}

TEST_F(CursorInputMapperUnitTest, GetSourcesReturnsTrackballInNavigationMode) {
    mPropertyMap.addProperty("cursor.mode", "navigation");
    createMapper();

    ASSERT_EQ(AINPUT_SOURCE_TRACKBALL, mMapper->getSources());
}

/**
 * Move the mouse and then click the button. Check whether HOVER_EXIT is generated when hovering
 * ends. Currently, it is not.
 */
TEST_F(CursorInputMapperUnitTest, HoverAndLeftButtonPress) {
    createMapper();
    // Move the cursor a little
    process(EV_REL, REL_X, 10);
    process(EV_REL, REL_Y, 20);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(WithMotionAction(HOVER_MOVE));

    // Now click the mouse button
    process(EV_KEY, BTN_LEFT, 1);
    process(EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(
            AllOf(WithMotionAction(ACTION_DOWN), WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_PRESS),
                                     WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY),
                                     WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY)));

    // Move some more.
    process(EV_REL, REL_X, 10);
    process(EV_REL, REL_Y, 20);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(WithMotionAction(ACTION_MOVE));

    // Release the button
    process(EV_KEY, BTN_LEFT, 0);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_RELEASE),
                                     WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY)));
    mFakeListener.expectMotion(WithMotionAction(ACTION_UP));
    mFakeListener.expectMotion(WithMotionAction(HOVER_MOVE));
}

TEST_F(CursorInputMapperUnitTest, MoveAndButtonChangeInSameFrame) {
    createMapper();

    // Move the cursor and press the button
    process(EV_REL, REL_X, -10);
    process(EV_REL, REL_Y, 20);
    process(EV_KEY, BTN_LEFT, 1);
    process(EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(AllOf(WithMotionAction(HOVER_MOVE), WithButtonState(0),
                                     WithNegativeAxis(AXIS_RELATIVE_X),
                                     WithPositiveAxis(AXIS_RELATIVE_Y), WithPressure(0.0f)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_DOWN),
                                     WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY),
                                     WithZeroAxis(AXIS_RELATIVE_X), WithZeroAxis(AXIS_RELATIVE_Y),
                                     WithPressure(1.0f)));
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(BUTTON_PRESS), WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY),
                  WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY), WithZeroAxis(AXIS_RELATIVE_X),
                  WithZeroAxis(AXIS_RELATIVE_Y), WithPressure(1.0f)));

    // Move some more and release the button
    process(EV_REL, REL_X, 10);
    process(EV_REL, REL_Y, -5);
    process(EV_KEY, BTN_LEFT, 0);
    process(EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_MOVE),
                                     WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY),
                                     WithPositiveAxis(AXIS_RELATIVE_X),
                                     WithNegativeAxis(AXIS_RELATIVE_Y), WithPressure(1.0f)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_RELEASE),
                                     WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY),
                                     WithButtonState(0), WithZeroAxis(AXIS_RELATIVE_X),
                                     WithZeroAxis(AXIS_RELATIVE_Y), WithPressure(0.0f)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_UP), WithButtonState(0),
                                     WithZeroAxis(AXIS_RELATIVE_X), WithZeroAxis(AXIS_RELATIVE_Y),
                                     WithPressure(0.0f)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(HOVER_MOVE), WithButtonState(0),
                                     WithZeroAxis(AXIS_RELATIVE_X), WithZeroAxis(AXIS_RELATIVE_Y),
                                     WithPressure(0.0f)));
}

/**
 * Test that enabling mouse swap primary button will have the left click result in a
 * `SECONDARY_BUTTON` event and a right click will result in a `PRIMARY_BUTTON` event.
 */
TEST_F(CursorInputMapperUnitTest, SwappedPrimaryButtonPress) {
    mReaderConfiguration.mouseSwapPrimaryButtonEnabled = true;
    createMapper();

    // Now click the left mouse button , expect a `SECONDARY_BUTTON` button state.
    process(EV_KEY, BTN_LEFT, 1);
    process(EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(
            AllOf(WithMotionAction(ACTION_DOWN), WithButtonState(AMOTION_EVENT_BUTTON_SECONDARY)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_PRESS),
                                     WithActionButton(AMOTION_EVENT_BUTTON_SECONDARY),
                                     WithButtonState(AMOTION_EVENT_BUTTON_SECONDARY)));

    // Release the left button.
    process(EV_KEY, BTN_LEFT, 0);
    process(EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_RELEASE),
                                     WithActionButton(AMOTION_EVENT_BUTTON_SECONDARY)));
    mFakeListener.expectMotion(WithMotionAction(ACTION_UP));
    mFakeListener.expectMotion(WithMotionAction(HOVER_MOVE));

    // Now click the right mouse button , expect a `PRIMARY_BUTTON` button state.
    process(EV_KEY, BTN_RIGHT, 1);
    process(EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(
            AllOf(WithMotionAction(ACTION_DOWN), WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_PRESS),
                                     WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY),
                                     WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY)));

    // Release the right button.
    process(EV_KEY, BTN_RIGHT, 0);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_RELEASE),
                                     WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY)));
    mFakeListener.expectMotion(WithMotionAction(ACTION_UP));
    mFakeListener.expectMotion(WithMotionAction(HOVER_MOVE));
}

/**
 * Set pointer capture and check that ACTION_MOVE events are emitted from CursorInputMapper.
 * During pointer capture, source should be set to MOUSE_RELATIVE. When the capture is disabled,
 * the events should be generated normally:
 *   1) The source should return to SOURCE_MOUSE
 *   2) Cursor position should be incremented by the relative device movements
 *   3) Cursor position of NotifyMotionArgs should now be getting populated.
 * When it's not SOURCE_MOUSE, CursorInputMapper doesn't populate cursor position values.
 */
TEST_F(CursorInputMapperUnitTest, ProcessPointerCapture) {
    createMapper();
    setPointerCapture(true);

    // Move.
    process(EV_REL, REL_X, 10);
    process(EV_REL, REL_Y, 20);
    process(EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(
            AllOf(WithMotionAction(ACTION_MOVE), WithSource(AINPUT_SOURCE_MOUSE_RELATIVE),
                  WithCoords(10.0f, 20.0f), WithRelativeMotion(10.0f, 20.0f),
                  WithCursorPosition(INVALID_CURSOR_POSITION, INVALID_CURSOR_POSITION)));

    // Button press.
    process(EV_KEY, BTN_MOUSE, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_DOWN),
                                     WithSource(AINPUT_SOURCE_MOUSE_RELATIVE),
                                     WithCoords(0.0f, 0.0f), WithPressure(1.0f)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_PRESS),
                                     WithSource(AINPUT_SOURCE_MOUSE_RELATIVE),
                                     WithCoords(0.0f, 0.0f), WithPressure(1.0f)));

    // Button release.
    process(EV_KEY, BTN_MOUSE, 0);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_RELEASE),
                                     WithSource(AINPUT_SOURCE_MOUSE_RELATIVE),
                                     WithCoords(0.0f, 0.0f), WithPressure(0.0f)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_UP),
                                     WithSource(AINPUT_SOURCE_MOUSE_RELATIVE),
                                     WithCoords(0.0f, 0.0f), WithPressure(0.0f)));

    // Another move.
    process(EV_REL, REL_X, 30);
    process(EV_REL, REL_Y, 40);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_MOVE),
                                     WithSource(AINPUT_SOURCE_MOUSE_RELATIVE),
                                     WithCoords(30.0f, 40.0f), WithRelativeMotion(30.0f, 40.0f)));

    // Disable pointer capture. Afterwards, events should be generated the usual way.
    setPointerCapture(false);
    const auto expectedCoords = WithCoords(0, 0);
    const auto expectedCursorPosition =
            WithCursorPosition(INVALID_CURSOR_POSITION, INVALID_CURSOR_POSITION);
    process(EV_REL, REL_X, 10);
    process(EV_REL, REL_Y, 20);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(HOVER_MOVE), WithSource(AINPUT_SOURCE_MOUSE),
                                     expectedCoords, expectedCursorPosition,
                                     WithPositiveAxis(AMOTION_EVENT_AXIS_RELATIVE_X),
                                     WithPositiveAxis(AMOTION_EVENT_AXIS_RELATIVE_Y)));
}

TEST_F(CursorInputMapperUnitTest, PopulateDeviceInfoReturnsScaledRangeInNavigationMode) {
    mPropertyMap.addProperty("cursor.mode", "navigation");
    createMapper();

    InputDeviceInfo info;
    mMapper->populateDeviceInfo(info);

    ASSERT_NO_FATAL_FAILURE(assertMotionRange(info, AINPUT_MOTION_RANGE_X, AINPUT_SOURCE_TRACKBALL,
                                              -1.0f, 1.0f, 0.0f,
                                              1.0f / TRACKBALL_MOVEMENT_THRESHOLD));
    ASSERT_NO_FATAL_FAILURE(assertMotionRange(info, AINPUT_MOTION_RANGE_Y, AINPUT_SOURCE_TRACKBALL,
                                              -1.0f, 1.0f, 0.0f,
                                              1.0f / TRACKBALL_MOVEMENT_THRESHOLD));
    ASSERT_NO_FATAL_FAILURE(assertMotionRange(info, AINPUT_MOTION_RANGE_PRESSURE,
                                              AINPUT_SOURCE_TRACKBALL, 0.0f, 1.0f, 0.0f, 0.0f));
}

TEST_F(CursorInputMapperUnitTest, ProcessShouldSetAllFieldsAndIncludeGlobalMetaState) {
    mPropertyMap.addProperty("cursor.mode", "navigation");
    createMapper();

    EXPECT_CALL(mMockInputReaderContext, getGlobalMetaState())
            .WillRepeatedly(Return(AMETA_SHIFT_LEFT_ON | AMETA_SHIFT_ON));

    // Button press.
    // Mostly testing non x/y behavior here so we don't need to check again elsewhere.
    process(ARBITRARY_TIME, EV_KEY, BTN_MOUSE, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    auto commonMatcher =
            AllOf(WithDeviceId(DEVICE_ID), WithSource(AINPUT_SOURCE_TRACKBALL),
                  WithFlags(ftl::Flags<MotionFlag>()), WithPolicyFlags(0),
                  WithMetaState(AMETA_SHIFT_LEFT_ON | AMETA_SHIFT_ON), WithPointerCount(1),
                  WithPointerId(0, 0), WithToolType(ToolType::MOUSE), WithCoords(0.0f, 0.0f),
                  WithPrecision(TRACKBALL_MOVEMENT_THRESHOLD, TRACKBALL_MOVEMENT_THRESHOLD),
                  WithDownTime(ARBITRARY_TIME));

    mFakeListener.expectMotion(
            AllOf(WithMotionAction(ACTION_DOWN), WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY),
                  WithPressure(1.0f), WithEventTime(ARBITRARY_TIME), commonMatcher));
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(BUTTON_PRESS), WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY),
                  WithPressure(1.0f), WithEventTime(ARBITRARY_TIME), commonMatcher));

    // Button release. Should have same down time.
    process(ARBITRARY_TIME + 1, EV_KEY, BTN_MOUSE, 0);
    process(ARBITRARY_TIME + 1, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_RELEASE), WithButtonState(0),
                                     WithPressure(0.0f), WithEventTime(ARBITRARY_TIME + 1),
                                     commonMatcher));
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_UP), WithButtonState(0),
                                     WithPressure(0.0f), WithEventTime(ARBITRARY_TIME + 1),
                                     commonMatcher));
}

TEST_F(CursorInputMapperUnitTest, ProcessShouldHandleIndependentXYUpdates) {
    mPropertyMap.addProperty("cursor.mode", "navigation");
    createMapper();

    // Motion in X but not Y.
    process(ARBITRARY_TIME, EV_REL, REL_X, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_MOVE), WithPressure(0.0f),
                                     WithPositiveAxis(AXIS_X), WithZeroAxis(AXIS_Y)));

    // Motion in Y but not X.
    process(ARBITRARY_TIME, EV_REL, REL_Y, -2);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_MOVE), WithPressure(0.0f),
                                     WithZeroAxis(AXIS_X), WithNegativeAxis(AXIS_Y)));
}

TEST_F(CursorInputMapperUnitTest, ProcessShouldHandleIndependentButtonUpdates) {
    mPropertyMap.addProperty("cursor.mode", "navigation");
    createMapper();

    // Button press.
    process(ARBITRARY_TIME, EV_KEY, BTN_MOUSE, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(ACTION_DOWN), WithCoords(0.0f, 0.0f), WithPressure(1.0f)));
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(BUTTON_PRESS), WithCoords(0.0f, 0.0f), WithPressure(1.0f)));

    // Button release.
    process(ARBITRARY_TIME, EV_KEY, BTN_MOUSE, 0);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(BUTTON_RELEASE), WithCoords(0.0f, 0.0f), WithPressure(0.0f)));
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(ACTION_UP), WithCoords(0.0f, 0.0f), WithPressure(0.0f)));
}

TEST_F(CursorInputMapperUnitTest, ProcessShouldHandleCombinedXYAndButtonUpdates) {
    mPropertyMap.addProperty("cursor.mode", "navigation");
    createMapper();

    // Combined X, Y and Button.
    process(ARBITRARY_TIME, EV_REL, REL_X, 1);
    process(ARBITRARY_TIME, EV_REL, REL_Y, -2);
    process(ARBITRARY_TIME, EV_KEY, BTN_MOUSE, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_MOVE), WithPressure(0.0f),
                                     WithPositiveAxis(AXIS_X), WithNegativeAxis(AXIS_Y)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_DOWN), WithPressure(1.0f),
                                     WithZeroAxis(AXIS_X), WithZeroAxis(AXIS_Y)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_PRESS), WithPressure(1.0f),
                                     WithZeroAxis(AXIS_X), WithZeroAxis(AXIS_Y)));

    // Move X, Y a bit while pressed.
    process(ARBITRARY_TIME, EV_REL, REL_X, 2);
    process(ARBITRARY_TIME, EV_REL, REL_Y, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_MOVE), WithPressure(1.0f),
                                     WithPositiveAxis(AXIS_X), WithPositiveAxis(AXIS_Y)));

    // Release Button.
    process(ARBITRARY_TIME, EV_KEY, BTN_MOUSE, 0);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(BUTTON_RELEASE), WithCoords(0.0f, 0.0f), WithPressure(0.0f)));
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(ACTION_UP), WithCoords(0.0f, 0.0f), WithPressure(0.0f)));
}

TEST_F(CursorInputMapperUnitTest, ProcessShouldNotRotateMotionsWhenOrientationAware) {
    // InputReader works in the un-rotated coordinate space, so orientation-aware devices do not
    // need to be rotated.
    mPropertyMap.addProperty("cursor.mode", "navigation");
    mPropertyMap.addProperty("cursor.orientationAware", "1");
    EXPECT_CALL((*mDevice), getAssociatedViewport)
            .WillRepeatedly(Return(createPrimaryViewport(ui::Rotation::Rotation90)));
    mMapper = createInputMapper<CursorInputMapper>(*mDeviceContext, mReaderConfiguration);

    constexpr auto X = AXIS_X;
    constexpr auto Y = AXIS_Y;
    ASSERT_NO_FATAL_FAILURE(testRotation( 0,  1, AllOf(WithZeroAxis(X),     WithPositiveAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1,  1, AllOf(WithPositiveAxis(X), WithPositiveAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1,  0, AllOf(WithPositiveAxis(X), WithZeroAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1, -1, AllOf(WithPositiveAxis(X), WithNegativeAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 0, -1, AllOf(WithZeroAxis(X),     WithNegativeAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1, -1, AllOf(WithNegativeAxis(X), WithNegativeAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1,  0, AllOf(WithNegativeAxis(X), WithZeroAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1,  1, AllOf(WithNegativeAxis(X), WithPositiveAxis(Y))));
}

TEST_F(CursorInputMapperUnitTest, ProcessShouldRotateMotionsWhenNotOrientationAware) {
    // Since InputReader works in the un-rotated coordinate space, only devices that are not
    // orientation-aware are affected by display rotation.
    mPropertyMap.addProperty("cursor.mode", "navigation");
    EXPECT_CALL((*mDevice), getAssociatedViewport)
            .WillRepeatedly(Return(createPrimaryViewport(ui::Rotation::Rotation0)));
    mMapper = createInputMapper<CursorInputMapper>(*mDeviceContext, mReaderConfiguration);

    constexpr auto X = AXIS_X;
    constexpr auto Y = AXIS_Y;
    ASSERT_NO_FATAL_FAILURE(testRotation( 0,  1, AllOf(WithZeroAxis(X),     WithPositiveAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1,  1, AllOf(WithPositiveAxis(X), WithPositiveAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1,  0, AllOf(WithPositiveAxis(X), WithZeroAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1, -1, AllOf(WithPositiveAxis(X), WithNegativeAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 0, -1, AllOf(WithZeroAxis(X),     WithNegativeAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1, -1, AllOf(WithNegativeAxis(X), WithNegativeAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1,  0, AllOf(WithNegativeAxis(X), WithZeroAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1,  1, AllOf(WithNegativeAxis(X), WithPositiveAxis(Y))));

    EXPECT_CALL((*mDevice), getAssociatedViewport)
            .WillRepeatedly(Return(createPrimaryViewport(ui::Rotation::Rotation90)));
    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::DISPLAY_INFO);
    ASSERT_NO_FATAL_FAILURE(testRotation( 0,  1, AllOf(WithNegativeAxis(X), WithZeroAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1,  1, AllOf(WithNegativeAxis(X), WithPositiveAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1,  0, AllOf(WithZeroAxis(X),     WithPositiveAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1, -1, AllOf(WithPositiveAxis(X), WithPositiveAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 0, -1, AllOf(WithPositiveAxis(X), WithZeroAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1, -1, AllOf(WithPositiveAxis(X), WithNegativeAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1,  0, AllOf(WithZeroAxis(X),     WithNegativeAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1,  1, AllOf(WithNegativeAxis(X), WithNegativeAxis(Y))));

    EXPECT_CALL((*mDevice), getAssociatedViewport)
            .WillRepeatedly(Return(createPrimaryViewport(ui::Rotation::Rotation180)));
    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::DISPLAY_INFO);
    ASSERT_NO_FATAL_FAILURE(testRotation( 0,  1, AllOf(WithZeroAxis(X),     WithNegativeAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1,  1, AllOf(WithNegativeAxis(X), WithNegativeAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1,  0, AllOf(WithNegativeAxis(X), WithZeroAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1, -1, AllOf(WithNegativeAxis(X), WithPositiveAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 0, -1, AllOf(WithZeroAxis(X),     WithPositiveAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1, -1, AllOf(WithPositiveAxis(X), WithPositiveAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1,  0, AllOf(WithPositiveAxis(X), WithZeroAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1,  1, AllOf(WithPositiveAxis(X), WithNegativeAxis(Y))));

    EXPECT_CALL((*mDevice), getAssociatedViewport)
            .WillRepeatedly(Return(createPrimaryViewport(ui::Rotation::Rotation270)));
    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::DISPLAY_INFO);
    ASSERT_NO_FATAL_FAILURE(testRotation( 0,  1, AllOf(WithPositiveAxis(X), WithZeroAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1,  1, AllOf(WithPositiveAxis(X), WithNegativeAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1,  0, AllOf(WithZeroAxis(X),     WithNegativeAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 1, -1, AllOf(WithNegativeAxis(X), WithNegativeAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation( 0, -1, AllOf(WithNegativeAxis(X), WithZeroAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1, -1, AllOf(WithNegativeAxis(X), WithPositiveAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1,  0, AllOf(WithZeroAxis(X),     WithPositiveAxis(Y))));
    ASSERT_NO_FATAL_FAILURE(testRotation(-1,  1, AllOf(WithPositiveAxis(X), WithPositiveAxis(Y))));
}

TEST_F(CursorInputMapperUnitTest, PopulateDeviceInfoReturnsRangeFromPolicy) {
    mPropertyMap.addProperty("cursor.mode", "pointer");
    mFakePolicy->clearViewports();
    createMapper();

    InputDeviceInfo info;
    mMapper->populateDeviceInfo(info);

    // Initially there should not be a valid motion range because there's no viewport or pointer
    // bounds.
    ASSERT_EQ(nullptr, info.getMotionRange(AINPUT_MOTION_RANGE_X, AINPUT_SOURCE_MOUSE));
    ASSERT_EQ(nullptr, info.getMotionRange(AINPUT_MOTION_RANGE_Y, AINPUT_SOURCE_MOUSE));
    ASSERT_NO_FATAL_FAILURE(assertMotionRange(info, AINPUT_MOTION_RANGE_PRESSURE,
                                              AINPUT_SOURCE_MOUSE, 0.0f, 1.0f, 0.0f, 0.0f));

    // When the viewport and the default pointer display ID is set, then there should be a valid
    // motion range.
    mFakePolicy->setDefaultPointerDisplayId(DISPLAY_ID);
    mFakePolicy->addDisplayViewport(createPrimaryViewport(ui::Rotation::Rotation0));
    reconfigureMapper(systemTime(), mReaderConfiguration,
                      InputReaderConfiguration::Change::DISPLAY_INFO);

    InputDeviceInfo info2;
    mMapper->populateDeviceInfo(info2);

    ASSERT_NO_FATAL_FAILURE(assertMotionRange(info2, AINPUT_MOTION_RANGE_X, AINPUT_SOURCE_MOUSE, 0,
                                              DISPLAY_WIDTH - 1, 0.0f, 0.0f));
    ASSERT_NO_FATAL_FAILURE(assertMotionRange(info2, AINPUT_MOTION_RANGE_Y, AINPUT_SOURCE_MOUSE, 0,
                                              DISPLAY_HEIGHT - 1, 0.0f, 0.0f));
    ASSERT_NO_FATAL_FAILURE(assertMotionRange(info2, AINPUT_MOTION_RANGE_PRESSURE,
                                              AINPUT_SOURCE_MOUSE, 0.0f, 1.0f, 0.0f, 0.0f));
}

TEST_F(CursorInputMapperUnitTest, ConfigureDisplayIdWithAssociatedViewport) {
    DisplayViewport primaryViewport = createPrimaryViewport(ui::Rotation::Rotation90);
    DisplayViewport secondaryViewport = createSecondaryViewport();
    mReaderConfiguration.setDisplayViewports({primaryViewport, secondaryViewport});
    // Set up the secondary display as the display on which the pointer should be shown.
    // The InputDevice is not associated with any display.
    EXPECT_CALL((*mDevice), getAssociatedViewport).WillRepeatedly(Return(secondaryViewport));
    mMapper = createInputMapper<CursorInputMapper>(*mDeviceContext, mReaderConfiguration);

    // Ensure input events are generated for the secondary display.
    process(ARBITRARY_TIME, EV_REL, REL_X, 10);
    process(ARBITRARY_TIME, EV_REL, REL_Y, 20);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(HOVER_MOVE), WithSource(AINPUT_SOURCE_MOUSE),
                                     WithDisplayId(SECONDARY_DISPLAY_ID), WithCoords(0.0f, 0.0f)));
}

TEST_F(CursorInputMapperUnitTest,
       ConfigureDisplayIdShouldGenerateEventForMismatchedPointerDisplay) {
    DisplayViewport primaryViewport = createPrimaryViewport(ui::Rotation::Rotation90);
    DisplayViewport secondaryViewport = createSecondaryViewport();
    mReaderConfiguration.setDisplayViewports({primaryViewport, secondaryViewport});
    // Set up the primary display as the display on which the pointer should be shown.
    // Associate the InputDevice with the secondary display.
    EXPECT_CALL((*mDevice), getAssociatedViewport).WillRepeatedly(Return(secondaryViewport));
    mMapper = createInputMapper<CursorInputMapper>(*mDeviceContext, mReaderConfiguration);

    // With PointerChoreographer enabled, there could be a PointerController for the associated
    // display even if it is different from the pointer display. So the mapper should generate an
    // event.
    process(ARBITRARY_TIME, EV_REL, REL_X, 10);
    process(ARBITRARY_TIME, EV_REL, REL_Y, 20);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(HOVER_MOVE), WithSource(AINPUT_SOURCE_MOUSE),
                                     WithDisplayId(SECONDARY_DISPLAY_ID), WithCoords(0.0f, 0.0f)));
}

TEST_F(CursorInputMapperUnitTest, ProcessShouldHandleAllButtonsWithZeroCoords) {
    mPropertyMap.addProperty("cursor.mode", "pointer");
    createMapper();

    // press BTN_LEFT, release BTN_LEFT
    process(ARBITRARY_TIME, EV_KEY, BTN_LEFT, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_DOWN),
                                     WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY),
                                     WithCoords(0.0f, 0.0f), WithPressure(1.0f)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_PRESS),
                                     WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY),
                                     WithCoords(0.0f, 0.0f), WithPressure(1.0f)));
    process(ARBITRARY_TIME, EV_KEY, BTN_LEFT, 0);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_RELEASE), WithButtonState(0),
                                     WithCoords(0.0f, 0.0f), WithPressure(0.0f)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_UP), WithButtonState(0),
                                     WithCoords(0.0f, 0.0f), WithPressure(0.0f)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(HOVER_MOVE), WithButtonState(0),
                                     WithCoords(0.0f, 0.0f), WithPressure(0.0f)));

    // press BTN_RIGHT + BTN_MIDDLE, release BTN_RIGHT, release BTN_MIDDLE
    process(ARBITRARY_TIME, EV_KEY, BTN_RIGHT, 1);
    process(ARBITRARY_TIME, EV_KEY, BTN_MIDDLE, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(ACTION_DOWN),
                  WithButtonState(AMOTION_EVENT_BUTTON_SECONDARY | AMOTION_EVENT_BUTTON_TERTIARY),
                  WithCoords(0.0f, 0.0f), WithPressure(1.0f)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_PRESS),
                                     WithButtonState(AMOTION_EVENT_BUTTON_TERTIARY),
                                     WithCoords(0.0f, 0.0f), WithPressure(1.0f)));
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(BUTTON_PRESS),
                  WithButtonState(AMOTION_EVENT_BUTTON_SECONDARY | AMOTION_EVENT_BUTTON_TERTIARY),
                  WithCoords(0.0f, 0.0f), WithPressure(1.0f)));
    process(ARBITRARY_TIME, EV_KEY, BTN_RIGHT, 0);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_RELEASE),
                                     WithButtonState(AMOTION_EVENT_BUTTON_TERTIARY),
                                     WithCoords(0.0f, 0.0f), WithPressure(1.0f)));
    process(ARBITRARY_TIME, EV_KEY, BTN_MIDDLE, 0);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_RELEASE), WithButtonState(0),
                                     WithCoords(0.0f, 0.0f), WithPressure(0.0f)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_UP), WithButtonState(0),
                                     WithCoords(0.0f, 0.0f), WithPressure(0.0f)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(HOVER_MOVE), WithButtonState(0),
                                     WithCoords(0.0f, 0.0f), WithPressure(0.0f)));
}

class CursorInputMapperButtonKeyTest
      : public CursorInputMapperUnitTest,
        public testing::WithParamInterface<
                std::tuple<int32_t /*evdevCode*/, int32_t /*expectedButtonState*/,
                           int32_t /*expectedKeyCode*/>> {};

TEST_P(CursorInputMapperButtonKeyTest, ProcessShouldHandleButtonKeyWithZeroCoords) {
    auto [evdevCode, expectedButtonState, expectedKeyCode] = GetParam();
    mPropertyMap.addProperty("cursor.mode", "pointer");
    createMapper();

    process(ARBITRARY_TIME, EV_KEY, evdevCode, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectKey(
            AllOf(WithKeyAction(AKEY_EVENT_ACTION_DOWN), WithKeyCode(expectedKeyCode)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_PRESS),
                                     WithButtonState(expectedButtonState), WithCoords(0.0f, 0.0f),
                                     WithPressure(0.0f)));
    process(ARBITRARY_TIME, EV_KEY, evdevCode, 0);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(BUTTON_RELEASE), WithButtonState(0),
                                     WithCoords(0.0f, 0.0f), WithPressure(0.0f)));
    mFakeListener.expectKey(
            AllOf(WithKeyAction(AKEY_EVENT_ACTION_UP), WithKeyCode(expectedKeyCode)));
}

INSTANTIATE_TEST_SUITE_P(
        SideExtraBackAndForward, CursorInputMapperButtonKeyTest,
        testing::Values(std::make_tuple(BTN_SIDE, AMOTION_EVENT_BUTTON_BACK, AKEYCODE_BACK),
                        std::make_tuple(BTN_EXTRA, AMOTION_EVENT_BUTTON_FORWARD, AKEYCODE_FORWARD),
                        std::make_tuple(BTN_BACK, AMOTION_EVENT_BUTTON_BACK, AKEYCODE_BACK),
                        std::make_tuple(BTN_FORWARD, AMOTION_EVENT_BUTTON_FORWARD,
                                        AKEYCODE_FORWARD)));

TEST_F(CursorInputMapperUnitTest, ProcessWhenModeIsPointerShouldKeepZeroCoords) {
    mPropertyMap.addProperty("cursor.mode", "pointer");
    createMapper();

    process(ARBITRARY_TIME, EV_REL, REL_X, 10);
    process(ARBITRARY_TIME, EV_REL, REL_Y, 20);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithSource(AINPUT_SOURCE_MOUSE), WithMotionAction(HOVER_MOVE),
                                     WithCoords(0.0f, 0.0f), WithPressure(0.0f), WithSize(0.0f),
                                     WithTouchDimensions(0.0f, 0.0f),
                                     WithToolDimensions(0.0f, 0.0f), WithOrientation(0.0f),
                                     WithDistance(0.0f)));
}

TEST_F(CursorInputMapperUnitTest, ProcessRegularScroll) {
    createMapper();

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 1);
    process(ARBITRARY_TIME, EV_REL, REL_HWHEEL, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(
            AllOf(WithMotionAction(HOVER_MOVE), WithSource(AINPUT_SOURCE_MOUSE)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_SCROLL),
                                     WithSource(AINPUT_SOURCE_MOUSE), WithScroll(1.0f, 1.0f)));
}

TEST_F(CursorInputMapperUnitTest, ProcessHighResScroll) {
    EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_WHEEL_HI_RES))
            .WillRepeatedly(Return(true));
    EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_HWHEEL_HI_RES))
            .WillRepeatedly(Return(true));
    createMapper();

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL_HI_RES, 60);
    process(ARBITRARY_TIME, EV_REL, REL_HWHEEL_HI_RES, 60);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(
            AllOf(WithMotionAction(HOVER_MOVE), WithSource(AINPUT_SOURCE_MOUSE)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_SCROLL),
                                     WithSource(AINPUT_SOURCE_MOUSE), WithScroll(0.5f, 0.5f)));
}

TEST_F(CursorInputMapperUnitTest, HighResScrollIgnoresRegularScroll) {
    EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_WHEEL_HI_RES))
            .WillRepeatedly(Return(true));
    EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_HWHEEL_HI_RES))
            .WillRepeatedly(Return(true));
    createMapper();

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL_HI_RES, 60);
    process(ARBITRARY_TIME, EV_REL, REL_HWHEEL_HI_RES, 60);
    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 1);
    process(ARBITRARY_TIME, EV_REL, REL_HWHEEL, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(
            AllOf(WithMotionAction(HOVER_MOVE), WithSource(AINPUT_SOURCE_MOUSE)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_SCROLL),
                                     WithSource(AINPUT_SOURCE_MOUSE), WithScroll(0.5f, 0.5f)));
}

TEST_F(CursorInputMapperUnitTest, ProcessReversedVerticalScroll) {
    mReaderConfiguration.mouseReverseVerticalScrollingEnabled = true;
    createMapper();

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL, 1);
    process(ARBITRARY_TIME, EV_REL, REL_HWHEEL, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);

    // Reversed vertical scrolling only affects the y-axis, expect it to be -1.0f to indicate the
    // inverted scroll direction.
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(HOVER_MOVE), WithSource(AINPUT_SOURCE_MOUSE)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_SCROLL),
                                     WithSource(AINPUT_SOURCE_MOUSE), WithScroll(1.0f, -1.0f)));
}

TEST_F(CursorInputMapperUnitTest, ProcessHighResReversedVerticalScroll) {
    mReaderConfiguration.mouseReverseVerticalScrollingEnabled = true;
    EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_WHEEL_HI_RES))
            .WillRepeatedly(Return(true));
    EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, REL_HWHEEL_HI_RES))
            .WillRepeatedly(Return(true));
    createMapper();

    process(ARBITRARY_TIME, EV_REL, REL_WHEEL_HI_RES, 60);
    process(ARBITRARY_TIME, EV_REL, REL_HWHEEL_HI_RES, 60);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(
            AllOf(WithMotionAction(HOVER_MOVE), WithSource(AINPUT_SOURCE_MOUSE)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_SCROLL),
                                     WithSource(AINPUT_SOURCE_MOUSE), WithScroll(0.5f, -0.5f)));
}

/**
 * When Pointer Capture is enabled, we expect to report unprocessed relative movements, so any
 * pointer acceleration or speed processing should not be applied.
 */
TEST_F(CursorInputMapperUnitTest, PointerCaptureDisablesVelocityProcessing) {
    mPropertyMap.addProperty("cursor.mode", "pointer");
    createMapper();

    // Move and verify scale is applied.
    process(ARBITRARY_TIME, EV_REL, REL_X, 10);
    process(ARBITRARY_TIME, EV_REL, REL_Y, 20);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    NotifyMotionArgs args = mFakeListener.expectMotion(
            AllOf(WithSource(AINPUT_SOURCE_MOUSE), WithMotionAction(HOVER_MOVE)));
    ASSERT_GT(args.pointerCoords[0].getAxisValue(AMOTION_EVENT_AXIS_RELATIVE_X), 10.0f);
    ASSERT_GT(args.pointerCoords[0].getAxisValue(AMOTION_EVENT_AXIS_RELATIVE_Y), 20.0f);

    // Enable Pointer Capture
    setPointerCapture(true);

    // Move and verify scale is not applied.
    process(ARBITRARY_TIME, EV_REL, REL_X, 10);
    process(ARBITRARY_TIME, EV_REL, REL_Y, 20);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithSource(AINPUT_SOURCE_MOUSE_RELATIVE),
                                     WithMotionAction(ACTION_MOVE), WithRelativeMotion(10, 20)));
}

TEST_F(CursorInputMapperUnitTest, ConfigureDisplayIdNoAssociatedViewport) {
    // Set up the default display.
    mFakePolicy->clearViewports();
    mFakePolicy->addDisplayViewport(createPrimaryViewport(ui::Rotation::Rotation0));

    // Set up the secondary display as the display on which the pointer should be shown.
    // The InputDevice is not associated with any display.
    mFakePolicy->addDisplayViewport(createSecondaryViewport());
    mFakePolicy->setDefaultPointerDisplayId(SECONDARY_DISPLAY_ID);

    createMapper();

    // Ensure input events are generated without display ID or coords, because they will be decided
    // later by PointerChoreographer.
    process(ARBITRARY_TIME, EV_REL, REL_X, 10);
    process(ARBITRARY_TIME, EV_REL, REL_Y, 20);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(HOVER_MOVE), WithSource(AINPUT_SOURCE_MOUSE),
                                     WithDisplayId(ui::LogicalDisplayId::INVALID),
                                     WithCoords(0.0f, 0.0f)));
}

TEST_F(CursorInputMapperUnitTest, PointerAccelerationDisabled) {
    mReaderConfiguration.mousePointerAccelerationEnabled = false;
    mReaderConfiguration.mousePointerSpeed = 3;
    mPropertyMap.addProperty("cursor.mode", "pointer");
    createMapper();

    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_SPEED);

    std::vector<AccelerationCurveSegment> curve =
            createFlatAccelerationCurve(mReaderConfiguration.mousePointerSpeed);
    double baseGain = curve[0].baseGain;

    process(ARBITRARY_TIME, EV_REL, REL_X, 10);
    process(ARBITRARY_TIME, EV_REL, REL_Y, 20);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);

    const float expectedRelX = 10 * baseGain;
    const float expectedRelY = 20 * baseGain;
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(HOVER_MOVE), WithRelativeMotion(expectedRelX, expectedRelY)));
}

TEST_F(CursorInputMapperUnitTest, ConfigureAccelerationWithAssociatedViewport) {
    mPropertyMap.addProperty("cursor.mode", "pointer");
    DisplayViewport primaryViewport = createPrimaryViewport(ui::Rotation::Rotation0);
    mReaderConfiguration.setDisplayViewports({primaryViewport});
    EXPECT_CALL((*mDevice), getAssociatedViewport).WillRepeatedly(Return(primaryViewport));
    mMapper = createInputMapper<CursorInputMapper>(*mDeviceContext, mReaderConfiguration);

    // Verify that acceleration is being applied by default by checking that the movement is scaled.
    process(ARBITRARY_TIME, EV_REL, REL_X, 10);
    process(ARBITRARY_TIME, EV_REL, REL_Y, 20);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    NotifyMotionArgs args = mFakeListener.expectMotion(
            AllOf(WithMotionAction(HOVER_MOVE), WithDisplayId(DISPLAY_ID)));
    ASSERT_GT(args.pointerCoords[0].getAxisValue(AMOTION_EVENT_AXIS_RELATIVE_X), 10.0f);
    ASSERT_GT(args.pointerCoords[0].getAxisValue(AMOTION_EVENT_AXIS_RELATIVE_Y), 20.0f);

    // Disable acceleration for the display, and verify that acceleration is no longer applied.
    mReaderConfiguration.displaysWithMouseScalingDisabled.emplace(DISPLAY_ID);
    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_SPEED);

    process(ARBITRARY_TIME, EV_REL, REL_X, 10);
    process(ARBITRARY_TIME, EV_REL, REL_Y, 20);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(HOVER_MOVE), WithDisplayId(DISPLAY_ID),
                                     WithRelativeMotion(10, 20)));
}

TEST_F(CursorInputMapperUnitTest, ConfigureAccelerationOnDisplayChange) {
    mPropertyMap.addProperty("cursor.mode", "pointer");
    DisplayViewport primaryViewport = createPrimaryViewport(ui::Rotation::Rotation0);
    mReaderConfiguration.setDisplayViewports({primaryViewport});
    // Disable acceleration for the display.
    mReaderConfiguration.displaysWithMouseScalingDisabled.emplace(DISPLAY_ID);

    // Don't associate the device with the display yet.
    EXPECT_CALL((*mDevice), getAssociatedViewport).WillRepeatedly(Return(std::nullopt));
    mMapper = createInputMapper<CursorInputMapper>(*mDeviceContext, mReaderConfiguration);

    // Verify that acceleration is being applied by default by checking that the movement is scaled.
    process(ARBITRARY_TIME, EV_REL, REL_X, 10);
    process(ARBITRARY_TIME, EV_REL, REL_Y, 20);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    NotifyMotionArgs args = mFakeListener.expectMotion(AllOf(WithMotionAction(HOVER_MOVE)));
    ASSERT_GT(args.pointerCoords[0].getAxisValue(AMOTION_EVENT_AXIS_RELATIVE_X), 10.0f);
    ASSERT_GT(args.pointerCoords[0].getAxisValue(AMOTION_EVENT_AXIS_RELATIVE_Y), 20.0f);

    // Now associate the device with the display, and verify that acceleration is disabled.
    EXPECT_CALL((*mDevice), getAssociatedViewport).WillRepeatedly(Return(primaryViewport));
    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::DISPLAY_INFO);

    process(ARBITRARY_TIME, EV_REL, REL_X, 10);
    process(ARBITRARY_TIME, EV_REL, REL_Y, 20);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(HOVER_MOVE), WithDisplayId(DISPLAY_ID),
                                     WithRelativeMotion(10, 20)));
}

class DensityDependentCursorUnitTest : public CursorInputMapperUnitTest {
protected:

    void createViewport(int32_t densityDpi, float xDpi = ACONFIGURATION_DENSITY_NONE,
                        float yDpi = ACONFIGURATION_DENSITY_NONE) {
        DisplayViewport viewport =
                createPrimaryViewport(ui::Rotation::Rotation0, densityDpi, xDpi, yDpi);
        mReaderConfiguration.setDisplayViewports({viewport});
        EXPECT_CALL((*mDevice), getAssociatedViewport).WillRepeatedly(Return(viewport));
    }

    void processRelativeMove(int32_t rawRelativeX, int32_t rawRelativeY) {
        process(ARBITRARY_TIME, EV_REL, REL_X, rawRelativeX);
        process(ARBITRARY_TIME, EV_REL, REL_Y, rawRelativeY);
        process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    }

    std::tuple<float, float> getBaselineCursorMoves(int32_t rawRelativeX, int32_t rawRelativeY) {
        // Cursor moves are not scaled for display density ACONFIGURATION_DENSITY_XHIGH, which is
        // considered baseline. Acceleration will still apply.
        createViewport(ACONFIGURATION_DENSITY_XHIGH);
        mMapper = createInputMapper<CursorInputMapper>(*mDeviceContext, mReaderConfiguration);

        processRelativeMove(rawRelativeX, rawRelativeY);
        NotifyMotionArgs args =
                mFakeListener.expectMotion(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_MOVE));
        auto coords = args.pointerCoords[0];
        return {coords.getAxisValue(AMOTION_EVENT_AXIS_RELATIVE_X),
                coords.getAxisValue(AMOTION_EVENT_AXIS_RELATIVE_Y)};
    }
};

TEST_F(DensityDependentCursorUnitTest, ScalesCursorMoveWithDisplayDensity) {
    // Use same move values on different density displays, generated events should be scaled
    // according to the display density.
    const int32_t rawRelativeX = 10;
    const int32_t rawRelativeY = 20;
    const auto [baselineRelativeX, baselineRelativeY] =
            getBaselineCursorMoves(rawRelativeX, rawRelativeY);

    createViewport(ACONFIGURATION_DENSITY_XXHIGH);

    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::DISPLAY_INFO);

    processRelativeMove(rawRelativeX, rawRelativeY);
    float scalingFactor = static_cast<float>(ACONFIGURATION_DENSITY_XXHIGH) /
            static_cast<float>(ACONFIGURATION_DENSITY_XHIGH);
    mFakeListener.expectMotion(AllOf(WithMotionAction(HOVER_MOVE),
                                     WithRelativeMotion(baselineRelativeX * scalingFactor,
                                                        baselineRelativeY * scalingFactor)));
}

TEST_F(DensityDependentCursorUnitTest, FallbackToNoScalingWhenDensityUnavailable) {
    const int32_t rawRelativeX = 10;
    const int32_t rawRelativeY = 20;
    const auto [baselineRelativeX, baselineRelativeY] =
            getBaselineCursorMoves(rawRelativeX, rawRelativeY);

    // Viewport without density information should be equivalent to viewport with baseline density.
    createViewport(ACONFIGURATION_DENSITY_NONE);
    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::DISPLAY_INFO);

    processRelativeMove(rawRelativeX, rawRelativeY);
    mFakeListener.expectMotion(AllOf(WithMotionAction(HOVER_MOVE),
                                     WithRelativeMotion(baselineRelativeX, baselineRelativeY)));
}

TEST_F(DensityDependentCursorUnitTest,
       DoesNotScaleCursorMoveWithDisplayDensityWhenMouseScalingDisabled) {
    // Create a medium density viewport and disable all scaling.
    mReaderConfiguration.displaysWithMouseScalingDisabled.emplace(DISPLAY_ID);
    createViewport(ACONFIGURATION_DENSITY_MEDIUM);

    mMapper = createInputMapper<CursorInputMapper>(*mDeviceContext, mReaderConfiguration);

    const int32_t rawRelativeX = 10;
    const int32_t rawRelativeY = 20;
    processRelativeMove(rawRelativeX, rawRelativeY);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(HOVER_MOVE), WithRelativeMotion(rawRelativeX, rawRelativeY)));
}

TEST_F(DensityDependentCursorUnitTest,
       ResetScaleCursorMoveWithDisplayDensityWhenMouseScalingDisabled) {
    // Create a medium density viewport.
    createViewport(ACONFIGURATION_DENSITY_MEDIUM);
    mMapper = createInputMapper<CursorInputMapper>(*mDeviceContext, mReaderConfiguration);

    // Disable scaling
    mReaderConfiguration.displaysWithMouseScalingDisabled.emplace(DISPLAY_ID);
    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_SPEED);

    const int32_t rawRelativeX = 10;
    const int32_t rawRelativeY = 20;
    processRelativeMove(rawRelativeX, rawRelativeY);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(HOVER_MOVE), WithRelativeMotion(rawRelativeX, rawRelativeY)));
}

TEST_F(DensityDependentCursorUnitTest, DoesNotScaleCursorMoveWithPointerCaptureEnabled) {
    // Create a medium density viewport, that should have scaling enabled by default.
    createViewport(ACONFIGURATION_DENSITY_MEDIUM);
    mMapper = createInputMapper<CursorInputMapper>(*mDeviceContext, mReaderConfiguration);

    // Request pointer capture after the mapper has been configured.
    setPointerCapture(true);

    // Verify pointer capture has been enabled.
    const int32_t rawRelativeX = 10;
    const int32_t rawRelativeY = 20;
    processRelativeMove(rawRelativeX, rawRelativeY);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(ACTION_MOVE), WithSource(AINPUT_SOURCE_MOUSE_RELATIVE),
                  WithCoords(rawRelativeX, rawRelativeY),
                  WithRelativeMotion(rawRelativeX, rawRelativeY),
                  WithCursorPosition(INVALID_CURSOR_POSITION, INVALID_CURSOR_POSITION)));
}

namespace {

// Minimum timestamp separation between subsequent input events from a Bluetooth device.
constexpr nsecs_t MIN_BLUETOOTH_TIMESTAMP_DELTA = ms2ns(4);
// Maximum smoothing time delta so that we don't generate events too far into the future.
constexpr nsecs_t MAX_BLUETOOTH_SMOOTHING_DELTA = ms2ns(32);

} // namespace

// --- BluetoothCursorInputMapperUnitTest ---

class BluetoothCursorInputMapperUnitTest : public CursorInputMapperUnitTestBase {
protected:
    void SetUp() override {
        CursorInputMapperUnitTestBase::SetUp(BUS_BLUETOOTH, /*isExternal=*/true);
    }
};

TEST_F(BluetoothCursorInputMapperUnitTest, TimestampSmoothening) {
    mPropertyMap.addProperty("cursor.mode", "pointer");
    createMapper();

    nsecs_t kernelEventTime = ARBITRARY_TIME;
    nsecs_t expectedEventTime = ARBITRARY_TIME;
    process(kernelEventTime, EV_REL, REL_X, 1);
    process(kernelEventTime, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(HOVER_MOVE), WithEventTime(expectedEventTime)));

    // Process several events that come in quick succession, according to their timestamps.
    for (int i = 0; i < 3; i++) {
        constexpr static nsecs_t delta = ms2ns(1);
        static_assert(delta < MIN_BLUETOOTH_TIMESTAMP_DELTA);
        kernelEventTime += delta;
        expectedEventTime += MIN_BLUETOOTH_TIMESTAMP_DELTA;

        process(kernelEventTime, EV_REL, REL_X, 1);
        process(kernelEventTime, EV_SYN, SYN_REPORT, 0);
        mFakeListener.expectMotion(
                AllOf(WithMotionAction(HOVER_MOVE), WithEventTime(expectedEventTime)));
    }
}

TEST_F(BluetoothCursorInputMapperUnitTest, TimestampSmootheningIsCapped) {
    mPropertyMap.addProperty("cursor.mode", "pointer");
    createMapper();

    nsecs_t expectedEventTime = ARBITRARY_TIME;
    process(ARBITRARY_TIME, EV_REL, REL_X, 1);
    process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(HOVER_MOVE), WithEventTime(expectedEventTime)));

    // Process several events with the same timestamp from the kernel.
    // Ensure that we do not generate events too far into the future.
    constexpr static int32_t numEvents =
            MAX_BLUETOOTH_SMOOTHING_DELTA / MIN_BLUETOOTH_TIMESTAMP_DELTA;
    for (int i = 0; i < numEvents; i++) {
        expectedEventTime += MIN_BLUETOOTH_TIMESTAMP_DELTA;

        process(ARBITRARY_TIME, EV_REL, REL_X, 1);
        process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
        mFakeListener.expectMotion(
                AllOf(WithMotionAction(HOVER_MOVE), WithEventTime(expectedEventTime)));
    }

    // By processing more events with the same timestamp, we should not generate events with a
    // timestamp that is more than the specified max time delta from the timestamp at its injection.
    const nsecs_t cappedEventTime = ARBITRARY_TIME + MAX_BLUETOOTH_SMOOTHING_DELTA;
    for (int i = 0; i < 3; i++) {
        process(ARBITRARY_TIME, EV_REL, REL_X, 1);
        process(ARBITRARY_TIME, EV_SYN, SYN_REPORT, 0);
        mFakeListener.expectMotion(
                AllOf(WithMotionAction(HOVER_MOVE), WithEventTime(cappedEventTime)));
    }
}

TEST_F(BluetoothCursorInputMapperUnitTest, TimestampSmootheningNotUsed) {
    mPropertyMap.addProperty("cursor.mode", "pointer");
    createMapper();

    nsecs_t kernelEventTime = ARBITRARY_TIME;
    nsecs_t expectedEventTime = ARBITRARY_TIME;
    process(kernelEventTime, EV_REL, REL_X, 1);
    process(kernelEventTime, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(HOVER_MOVE), WithEventTime(expectedEventTime)));

    // If the next event has a timestamp that is sufficiently spaced out so that Bluetooth timestamp
    // smoothening is not needed, its timestamp is not affected.
    kernelEventTime += MAX_BLUETOOTH_SMOOTHING_DELTA + ms2ns(1);
    expectedEventTime = kernelEventTime;

    process(kernelEventTime, EV_REL, REL_X, 1);
    process(kernelEventTime, EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(HOVER_MOVE), WithEventTime(expectedEventTime)));
}

} // namespace android
