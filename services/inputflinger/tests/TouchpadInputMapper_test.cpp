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

#include "TouchpadInputMapper.h"

#include <android-base/logging.h>
#include <gtest/gtest.h>
#include <input/AccelerationCurve.h>
#include <input/Input.h>
#include <input/ScopedFlagOverride.h>

#include <log/log.h>
#include <optional>
#include <thread>
#include "InputMapperTest.h"
#include "TestConstants.h"
#include "TestEventMatchers.h"
#include "include/gestures.h"

#define TAG "TouchpadInputMapper_test"

namespace android {

using testing::AllOf;
using testing::Contains;
using testing::ElementsAre;
using testing::IsEmpty;
using testing::Return;
using testing::VariantWith;
constexpr auto ACTION_DOWN = AMOTION_EVENT_ACTION_DOWN;
constexpr auto ACTION_MOVE = AMOTION_EVENT_ACTION_MOVE;
constexpr auto ACTION_UP = AMOTION_EVENT_ACTION_UP;
constexpr auto BUTTON_PRESS = AMOTION_EVENT_ACTION_BUTTON_PRESS;
constexpr auto BUTTON_RELEASE = AMOTION_EVENT_ACTION_BUTTON_RELEASE;
constexpr auto HOVER_MOVE = AMOTION_EVENT_ACTION_HOVER_MOVE;
constexpr auto HOVER_ENTER = AMOTION_EVENT_ACTION_HOVER_ENTER;
constexpr auto HOVER_EXIT = AMOTION_EVENT_ACTION_HOVER_EXIT;
constexpr ui::LogicalDisplayId DISPLAY_ID = ui::LogicalDisplayId::DEFAULT;
constexpr int32_t DISPLAY_WIDTH = 480;
constexpr int32_t DISPLAY_HEIGHT = 800;
constexpr std::optional<uint8_t> NO_PORT = std::nullopt; // no physical port is specified

/**
 * Unit tests for TouchpadInputMapper.
 */
class TouchpadInputMapperTest : public VerifyingInputMapperUnitTest {
protected:
    void SetUp() override {
        VerifyingInputMapperUnitTest::SetUp();

        // Present scan codes: BTN_TOUCH and BTN_TOOL_FINGER
        expectScanCodes(/*present=*/true,
                        {BTN_LEFT, BTN_RIGHT, BTN_TOOL_FINGER, BTN_TOOL_QUINTTAP, BTN_TOUCH,
                         BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP, BTN_TOOL_QUADTAP});
        // Missing scan codes that the mapper checks for.
        expectScanCodes(/*present=*/false,
                        {BTN_TOOL_PEN, BTN_TOOL_RUBBER, BTN_TOOL_BRUSH, BTN_TOOL_PENCIL,
                         BTN_TOOL_AIRBRUSH});

        // Current scan code state - all keys are UP by default
        setScanCodeState(KeyState::UP, {BTN_TOUCH,          BTN_STYLUS,
                                        BTN_STYLUS2,        BTN_0,
                                        BTN_TOOL_FINGER,    BTN_TOOL_PEN,
                                        BTN_TOOL_RUBBER,    BTN_TOOL_BRUSH,
                                        BTN_TOOL_PENCIL,    BTN_TOOL_AIRBRUSH,
                                        BTN_TOOL_MOUSE,     BTN_TOOL_LENS,
                                        BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP,
                                        BTN_TOOL_QUADTAP,   BTN_TOOL_QUINTTAP,
                                        BTN_LEFT,           BTN_RIGHT,
                                        BTN_MIDDLE,         BTN_BACK,
                                        BTN_SIDE,           BTN_FORWARD,
                                        BTN_EXTRA,          BTN_TASK});

        setKeyCodeState(KeyState::UP,
                        {AKEYCODE_STYLUS_BUTTON_PRIMARY, AKEYCODE_STYLUS_BUTTON_SECONDARY});

        // Key mappings
        EXPECT_CALL(mMockEventHub, mapKey(EVENTHUB_ID, BTN_LEFT, /*usageCode=*/0, /*metaState=*/0))
                .WillRepeatedly(Return(std::nullopt));

        // Input properties - only INPUT_PROP_BUTTONPAD
        EXPECT_CALL(mMockEventHub, hasInputProperty(EVENTHUB_ID, INPUT_PROP_BUTTONPAD))
                .WillRepeatedly(Return(true));
        EXPECT_CALL(mMockEventHub, hasInputProperty(EVENTHUB_ID, INPUT_PROP_SEMI_MT))
                .WillRepeatedly(Return(false));

        // Axes that the device has
        setupAxis(ABS_MT_SLOT, /*valid=*/true, /*min=*/0, /*max=*/4, /*resolution=*/0);
        setupAxis(ABS_MT_POSITION_X, /*valid=*/true, /*min=*/0, /*max=*/2000, /*resolution=*/24);
        setupAxis(ABS_MT_POSITION_Y, /*valid=*/true, /*min=*/0, /*max=*/1000, /*resolution=*/24);
        setupAxis(ABS_MT_PRESSURE, /*valid=*/true, /*min*/ 0, /*max=*/255, /*resolution=*/0);
        // Axes that the device does not have
        setupAxis(ABS_MT_ORIENTATION, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_TOUCH_MAJOR, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_TOUCH_MINOR, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_WIDTH_MAJOR, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_WIDTH_MINOR, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_TRACKING_ID, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_DISTANCE, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_TOOL_TYPE, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);

        EXPECT_CALL(mMockEventHub, getAbsoluteAxisValue(EVENTHUB_ID, ABS_MT_SLOT))
                .WillRepeatedly(Return(0));
        EXPECT_CALL(mMockEventHub, getMtSlotValues(EVENTHUB_ID, testing::_, testing::_))
                .WillRepeatedly([]() -> base::Result<std::vector<int32_t>> {
                    return base::ResultError("Axis not supported", NAME_NOT_FOUND);
                });

        // User's settings - enable pointer acceleration
        mReaderConfiguration.touchpadAccelerationEnabled = true;

        mMapper = createInputMapper<TouchpadInputMapper>(*mDeviceContext, mReaderConfiguration);
    }

    void assertAccelCurvePropEquals(const std::string& propName,
                                    const std::vector<AccelerationCurveSegment>& expectedSegments) {
        SCOPED_TRACE(testing::Message() << "Gesture property \"" << propName << "\"");
        const auto prop = getGestureProperty(propName);
        ASSERT_TRUE(prop.has_value());

        std::vector<double> actualValues = prop.value().getRealValues();

        for (size_t i = 0; i < expectedSegments.size(); ++i) {
            SCOPED_TRACE(testing::Message() << "i = " << i);
            if (std::isinf(expectedSegments[i].maxPointerSpeedMmPerS)) {
                ASSERT_TRUE(std::isinf(actualValues[i * 4 + 0]));
            } else {
                ASSERT_NEAR(actualValues[i * 4 + 0], expectedSegments[i].maxPointerSpeedMmPerS,
                            EPSILON);
            }

            ASSERT_NEAR(actualValues[i * 4 + 1], 0, EPSILON);
            ASSERT_NEAR(actualValues[i * 4 + 2], expectedSegments[i].baseGain, EPSILON);
            ASSERT_NEAR(actualValues[i * 4 + 3], expectedSegments[i].reciprocal, EPSILON);
        }
    }

    void assertBothCurvesEqual(const std::vector<AccelerationCurveSegment>& expectedSegments) {
        assertAccelCurvePropEquals("Pointer Accel Curve", expectedSegments);
        assertAccelCurvePropEquals("Scroll Accel Curve", expectedSegments);
    }

    void assertRelativeModeCurvesInUse() {
        // The pointer curve should be flat (i.e. a single segment with infinite maximum speed).
        const auto pointerCurveProp = getGestureProperty("Pointer Accel Curve");
        ASSERT_TRUE(pointerCurveProp.has_value());

        std::vector<double> pointerCurveValues = pointerCurveProp.value().getRealValues();
        ASSERT_TRUE(std::isinf(pointerCurveValues[0]));
        ASSERT_NEAR(pointerCurveValues[1], 0, EPSILON);
        // What exact gain it has is an implementation detail subject to change, so we don't want to
        // assert an exact value.
        double pointerCurveGain = pointerCurveValues[2];
        ASSERT_GT(pointerCurveGain, 0);
        ASSERT_NEAR(pointerCurveValues[3], 0, EPSILON);

        // The scroll curve should also be flat.
        const auto scrollCurveProp = getGestureProperty("Scroll Accel Curve");
        ASSERT_TRUE(scrollCurveProp.has_value());

        std::vector<double> scrollCurveValues = scrollCurveProp.value().getRealValues();
        ASSERT_TRUE(std::isinf(scrollCurveValues[0]));
        ASSERT_NEAR(scrollCurveValues[1], 0, EPSILON);
        // Again, we don't want to specify the exact gain, but it should be positive and smaller
        // than the pointer curve's gain (since a scroll wheel tick is a much larger movement than a
        // single-pixel move on a mouse, so the scroll numbers reported in relative mode should be
        // much smaller than the cursor movement numbers for the same physical distance moved).
        double scrollCurveGain = scrollCurveValues[2];
        ASSERT_GT(scrollCurveGain, 0);
        ASSERT_LT(scrollCurveGain, pointerCurveGain);
        ASSERT_NEAR(scrollCurveValues[3], 0, EPSILON);
    }

    std::optional<GesturesProp> getGestureProperty(const std::string& propName) {
        return static_cast<TouchpadInputMapper*>(mMapper.get())
                ->getGesturePropertyForTesting(propName);
    }

    void sleepAfterTouchLift() const {
        // After a touch lift, the Gestures library will ignore further input for a short period of
        // time. This is related to the "Change Timeout" gesture property, but simply reducing that
        // value doesn't allow us to reduce this sleep time, so there's something else going on too
        // which prevents us from shortening this delay.
        std::this_thread::sleep_for(std::chrono::milliseconds(205));
    }

    void oneFingerSwipe() {
        int32_t moveX = 500;
        process(EV_ABS, ABS_MT_SLOT, 0);
        process(EV_ABS, ABS_MT_TRACKING_ID, 3);
        process(EV_KEY, BTN_TOUCH, 1);
        process(EV_KEY, BTN_TOOL_FINGER, 1);
        setScanCodeState(KeyState::DOWN, {BTN_TOUCH, BTN_TOOL_FINGER});
        process(EV_ABS, ABS_MT_POSITION_X, moveX);
        process(EV_ABS, ABS_MT_POSITION_Y, 500);
        process(EV_ABS, ABS_MT_PRESSURE, 25);
        process(EV_SYN, SYN_REPORT, 0);

        for (int32_t i = 0; i < 3; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            moveX += 100;

            process(EV_ABS, ABS_MT_POSITION_X, moveX);
            process(EV_SYN, SYN_REPORT, 0);
        }
        process(EV_ABS, ABS_MT_PRESSURE, 0);
        process(EV_ABS, ABS_MT_TRACKING_ID, -1);
        process(EV_KEY, BTN_TOUCH, 0);
        process(EV_KEY, BTN_TOOL_FINGER, 0);
        setScanCodeState(KeyState::UP, {BTN_TOUCH, BTN_TOOL_FINGER});
        process(EV_SYN, SYN_REPORT, 0);
    }
};

/**
 * Start moving the finger and then click the left touchpad button. Check whether HOVER_EXIT is
 * generated when hovering stops. Currently, it is not.
 * In the current implementation, HOVER_MOVE and ACTION_DOWN events are not sent out right away,
 * but only after the button is released.
 */
TEST_F(TouchpadInputMapperTest, HoverAndLeftButtonPress) {
    mFakePolicy->setDefaultPointerDisplayId(DISPLAY_ID);
    DisplayViewport viewport =
            createViewport(DISPLAY_ID, DISPLAY_WIDTH, DISPLAY_HEIGHT, ui::ROTATION_0,
                           /*isActive=*/true, "local:0", NO_PORT, ViewportType::INTERNAL);
    mFakePolicy->addDisplayViewport(viewport);

    reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                      InputReaderConfiguration::Change::DISPLAY_INFO);
    mFakeListener.assertNoEvents();

    process(EV_ABS, ABS_MT_TRACKING_ID, 1);
    process(EV_KEY, BTN_TOUCH, 1);
    setScanCodeState(KeyState::DOWN, {BTN_TOOL_FINGER});
    process(EV_KEY, BTN_TOOL_FINGER, 1);
    process(EV_ABS, ABS_MT_POSITION_X, 50);
    process(EV_ABS, ABS_MT_POSITION_Y, 50);
    process(EV_ABS, ABS_MT_PRESSURE, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.assertNoEvents();

    // Without this sleep, the test fails.
    // TODO(b/284133337): Figure out whether this can be removed
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    process(EV_KEY, BTN_LEFT, 1);
    setScanCodeState(KeyState::DOWN, {BTN_LEFT});
    process(EV_SYN, SYN_REPORT, 0);

    process(EV_KEY, BTN_LEFT, 0);
    setScanCodeState(KeyState::UP, {BTN_LEFT});
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(WithMotionAction(HOVER_ENTER));
    mFakeListener.expectMotion(WithMotionAction(HOVER_MOVE));
    mFakeListener.expectMotion(WithMotionAction(HOVER_EXIT));
    mFakeListener.expectMotion(WithMotionAction(ACTION_DOWN));
    mFakeListener.expectMotion(WithMotionAction(BUTTON_PRESS));
    mFakeListener.expectMotion(WithMotionAction(BUTTON_RELEASE));
    mFakeListener.expectMotion(WithMotionAction(ACTION_UP));
    mFakeListener.expectMotion(WithMotionAction(HOVER_ENTER));

    // Liftoff
    process(EV_ABS, ABS_MT_PRESSURE, 0);
    process(EV_ABS, ABS_MT_TRACKING_ID, -1);
    process(EV_KEY, BTN_TOUCH, 0);
    setScanCodeState(KeyState::UP, {BTN_TOOL_FINGER});
    process(EV_KEY, BTN_TOOL_FINGER, 0);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.assertNoEvents();
}

// Regression test for b/458469793, where a HOVER_EXIT event was incorrectly reported after
// resetting the touchpad when the last gesture was a scroll.
TEST_F(TouchpadInputMapperTest, ScrollThenResetThenHoverMoveIsConsistent) {
    mFakePolicy->setDefaultPointerDisplayId(DISPLAY_ID);
    DisplayViewport viewport =
            createViewport(DISPLAY_ID, DISPLAY_WIDTH, DISPLAY_HEIGHT, ui::ROTATION_0,
                           /*isActive=*/true, "local:0", NO_PORT, ViewportType::INTERNAL);
    mFakePolicy->addDisplayViewport(viewport);

    reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                      InputReaderConfiguration::Change::DISPLAY_INFO);

    // Scroll on the touchpad.
    int32_t scrollY = 300;
    process(EV_ABS, ABS_MT_SLOT, 0);
    process(EV_ABS, ABS_MT_TRACKING_ID, 1);
    process(EV_ABS, ABS_MT_POSITION_X, 500);
    process(EV_ABS, ABS_MT_POSITION_Y, scrollY);
    process(EV_ABS, ABS_MT_PRESSURE, 50);
    process(EV_ABS, ABS_MT_SLOT, 1);
    process(EV_ABS, ABS_MT_TRACKING_ID, 2);
    process(EV_ABS, ABS_MT_POSITION_X, 800);
    process(EV_ABS, ABS_MT_POSITION_Y, scrollY);
    process(EV_ABS, ABS_MT_PRESSURE, 50);
    process(EV_KEY, BTN_TOUCH, 1);
    process(EV_KEY, BTN_TOOL_DOUBLETAP, 1);
    setScanCodeState(KeyState::DOWN, {BTN_TOUCH, BTN_TOOL_DOUBLETAP});
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.assertNoEvents();

    for (int32_t i = 0; i < 5; i++) {
        // Without these sleeps, the test fails.
        // TODO(b/284133337): remove the dependency of these tests on "real time"
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        scrollY += 50;

        process(EV_ABS, ABS_MT_SLOT, 0);
        process(EV_ABS, ABS_MT_POSITION_Y, scrollY);
        process(EV_ABS, ABS_MT_SLOT, 1);
        process(EV_ABS, ABS_MT_POSITION_Y, scrollY);
        process(EV_SYN, SYN_REPORT, 0);
    }

    // Lift the fingers.
    process(EV_ABS, ABS_MT_SLOT, 0);
    process(EV_ABS, ABS_MT_TRACKING_ID, -1);
    process(EV_ABS, ABS_MT_SLOT, 1);
    process(EV_ABS, ABS_MT_TRACKING_ID, -1);
    process(EV_KEY, BTN_TOUCH, 0);
    process(EV_KEY, BTN_TOOL_DOUBLETAP, 0);
    setScanCodeState(KeyState::UP, {BTN_TOUCH, BTN_TOOL_DOUBLETAP});
    process(EV_SYN, SYN_REPORT, 0);
    // We don't care exactly what events are output here (as we're just concerned with consistency
    // enforced by the verifier), but we want to check that the events above did actually trigger a
    // complete two-finger swipe.
    mFakeListener.consumeUntilAndExpectMotion(
            AllOf(WithMotionAction(ACTION_UP),
                  WithMotionClassification(MotionClassification::TWO_FINGER_SWIPE)));

    sleepAfterTouchLift();

    // Reset the mapper.
    resetMapper(systemTime(SYSTEM_TIME_MONOTONIC));

    // Move a finger on the touchpad. Again, we don't care about the detailed events, but check a
    // cursor movement was detected.
    oneFingerSwipe();
    mFakeListener.consumeUntilAndExpectMotion(WithMotionAction(HOVER_MOVE));
}

TEST_F(TouchpadInputMapperTest, EnterRelativeCaptureDuringClickIsConsistent) {
    // By default, the Gestures library waits for a period of time after a button goes down before
    // it reports a button change gesture (to give extra fingers time to arrive in case of a
    // multi-finger click). This makes the click event arrive as the result of a timer callback,
    // which is hard and fragile to test using our current infrastructure, so we disable this wait.
    getGestureProperty("Button Evaluation Timeout")->setRealValues({0.f});

    mFakePolicy->setDefaultPointerDisplayId(DISPLAY_ID);
    DisplayViewport viewport =
            createViewport(DISPLAY_ID, DISPLAY_WIDTH, DISPLAY_HEIGHT, ui::ROTATION_0,
                           /*isActive=*/true, "local:0", NO_PORT, ViewportType::INTERNAL);
    mFakePolicy->addDisplayViewport(viewport);
    reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,

                      InputReaderConfiguration::Change::DISPLAY_INFO);

    // Press the touchpad's button.
    process(EV_ABS, ABS_MT_TRACKING_ID, 1);
    process(EV_KEY, BTN_TOUCH, 1);
    process(EV_KEY, BTN_TOOL_FINGER, 1);
    setScanCodeState(KeyState::DOWN, {BTN_TOUCH, BTN_TOOL_FINGER});
    process(EV_ABS, ABS_MT_POSITION_X, 50);
    process(EV_ABS, ABS_MT_POSITION_Y, 50);
    process(EV_ABS, ABS_MT_PRESSURE, 1);
    process(EV_SYN, SYN_REPORT, 0);

    // Without this sleep, the test fails.
    // TODO(b/284133337): Figure out whether this can be removed
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    process(EV_KEY, BTN_LEFT, 1);
    setScanCodeState(KeyState::DOWN, {BTN_LEFT});
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.consumeUntilAndExpectMotion(WithMotionAction(ACTION_DOWN));
    mFakeListener.expectMotion(WithMotionAction(BUTTON_PRESS));
    // Capture the touchpad.
    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::RELATIVE;
    reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_CAPTURE);

    // Release the button.
    process(EV_KEY, BTN_LEFT, 0);
    setScanCodeState(KeyState::UP, {BTN_LEFT});
    process(EV_ABS, ABS_MT_PRESSURE, 0);
    process(EV_ABS, ABS_MT_TRACKING_ID, -1);
    process(EV_KEY, BTN_TOUCH, 0);
    process(EV_KEY, BTN_TOOL_FINGER, 0);
    setScanCodeState(KeyState::UP, {BTN_TOUCH, BTN_TOOL_FINGER});
    process(EV_SYN, SYN_REPORT, 0);

    sleepAfterTouchLift();

    // Move a finger on the touchpad.
    oneFingerSwipe();
    mFakeListener.consumeUntilAndExpectMotion(WithMotionAction(ACTION_MOVE));

    // Release touchpad capture.
    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::UNCAPTURED;
    reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_CAPTURE);

    sleepAfterTouchLift();

    // Move a finger on the touchpad.
    oneFingerSwipe();
    mFakeListener.consumeUntilAndExpectMotion(WithMotionAction(HOVER_MOVE));
}

TEST_F(TouchpadInputMapperTest, ThreeFingerSwipeDisabledDuringRelativeCapture) {
    ASSERT_THAT(getGestureProperty("Three Finger Swipe Enable")->getBoolValues(),
                ElementsAre(true));

    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::RELATIVE;
    reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_CAPTURE);

    ASSERT_THAT(getGestureProperty("Three Finger Swipe Enable")->getBoolValues(),
                ElementsAre(false));

    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::UNCAPTURED;
    reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_CAPTURE);

    ASSERT_THAT(getGestureProperty("Three Finger Swipe Enable")->getBoolValues(),
                ElementsAre(true));
}

TEST_F(TouchpadInputMapperTest, ExitingAbsoluteCaptureCancelsFingers) {
    SCOPED_FLAG_OVERRIDE(cancel_touches_on_absolute_capture_release, true);
    // Enter absolute capture mode and put some fingers down.
    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::ABSOLUTE;
    reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_CAPTURE);

    process(EV_ABS, ABS_MT_SLOT, 0);
    process(EV_ABS, ABS_MT_TRACKING_ID, 1);
    process(EV_ABS, ABS_MT_POSITION_X, 500);
    process(EV_ABS, ABS_MT_POSITION_Y, 500);
    process(EV_ABS, ABS_MT_SLOT, 1);
    process(EV_ABS, ABS_MT_TRACKING_ID, 2);
    process(EV_ABS, ABS_MT_POSITION_X, 800);
    process(EV_ABS, ABS_MT_POSITION_Y, 500);
    process(EV_KEY, BTN_TOUCH, 1);
    process(EV_KEY, BTN_TOOL_DOUBLETAP, 1);
    setScanCodeState(KeyState::DOWN, {BTN_TOUCH, BTN_TOOL_DOUBLETAP});
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectDeviceReset(WithDeviceId(DEVICE_ID));
    mFakeListener.expectMotion(WithMotionAction(ACTION_DOWN));
    mFakeListener.expectMotion(WithMotionAction(AMOTION_EVENT_ACTION_POINTER_DOWN |
                                                (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT)));

    // Leave capture mode and check the state is reset.
    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::UNCAPTURED;
    reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_CAPTURE);

    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_CANCEL),
                                     WithPointerCount(2), WithPointerCoords(0, 500, 500),
                                     WithPointerCoords(1, 800, 500),
                                     WithPointerToolType(0, ToolType::FINGER),
                                     WithPointerToolType(1, ToolType::FINGER)));
    mFakeListener.expectDeviceReset(WithDeviceId(DEVICE_ID));
}

TEST_F(TouchpadInputMapperTest, TouchpadHardwareState) {
    mReaderConfiguration.shouldNotifyTouchpadHardwareState = true;
    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::TOUCHPAD_SETTINGS);

    process(EV_ABS, ABS_MT_TRACKING_ID, 1);
    process(EV_KEY, BTN_TOUCH, 1);
    setScanCodeState(KeyState::DOWN, {BTN_TOOL_FINGER});
    process(EV_KEY, BTN_TOOL_FINGER, 1);
    process(EV_ABS, ABS_MT_POSITION_X, 50);
    process(EV_ABS, ABS_MT_POSITION_Y, 50);
    process(EV_ABS, ABS_MT_PRESSURE, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.assertNoEvents();

    mFakePolicy->assertTouchpadHardwareStateNotified();
}

TEST_F(TouchpadInputMapperTest, TouchpadAccelerationDisabled) {
    mReaderConfiguration.touchpadAccelerationEnabled = false;
    mReaderConfiguration.touchpadPointerSpeed = 3;

    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::TOUCHPAD_SETTINGS);

    ASSERT_NO_FATAL_FAILURE(assertBothCurvesEqual(
            createFlatAccelerationCurve(mReaderConfiguration.touchpadPointerSpeed)));
}

TEST_F(TouchpadInputMapperTest, TouchpadAccelerationEnabled) {
    // Enable touchpad acceleration.
    mReaderConfiguration.touchpadAccelerationEnabled = true;
    mReaderConfiguration.touchpadPointerSpeed = 3;

    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::TOUCHPAD_SETTINGS);

    // Use createAccelerationCurveForPointerSensitivity to get expected curve segments.
    ASSERT_NO_FATAL_FAILURE(assertBothCurvesEqual(createAccelerationCurveForPointerSensitivity(
            mReaderConfiguration.touchpadPointerSpeed)));
}

TEST_F(TouchpadInputMapperTest, AccelerationDisabledDuringCapture) {
    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::RELATIVE;

    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_CAPTURE);
    ASSERT_NO_FATAL_FAILURE(assertRelativeModeCurvesInUse());

    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::UNCAPTURED;

    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_CAPTURE);

    ASSERT_NO_FATAL_FAILURE(assertBothCurvesEqual(createAccelerationCurveForPointerSensitivity(
            mReaderConfiguration.touchpadPointerSpeed)));
}

TEST_F(TouchpadInputMapperTest, ExitingCaptureWithAccelerationDisabledDoesntReenable) {
    mReaderConfiguration.touchpadAccelerationEnabled = false;
    mReaderConfiguration.touchpadPointerSpeed = 3;
    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::TOUCHPAD_SETTINGS);

    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::RELATIVE;
    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_CAPTURE);
    mFakeListener.expectDeviceReset(AllOf(WithDeviceId(DEVICE_ID), WithEventTime(ARBITRARY_TIME)));
    ASSERT_NO_FATAL_FAILURE(assertRelativeModeCurvesInUse());

    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::UNCAPTURED;
    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_CAPTURE);
    mFakeListener.expectDeviceReset(AllOf(WithDeviceId(DEVICE_ID), WithEventTime(ARBITRARY_TIME)));

    ASSERT_NO_FATAL_FAILURE(assertBothCurvesEqual(
            createFlatAccelerationCurve(mReaderConfiguration.touchpadPointerSpeed)));
}

TEST_F(TouchpadInputMapperTest, ChangingSettingsDuringCaptureDoesntReenableAcceleration) {
    constexpr int32_t NEW_POINTER_SPEED = 3;
    ASSERT_NE(mReaderConfiguration.touchpadPointerSpeed, NEW_POINTER_SPEED);

    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::RELATIVE;
    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_CAPTURE);
    mFakeListener.expectDeviceReset(AllOf(WithDeviceId(DEVICE_ID), WithEventTime(ARBITRARY_TIME)));
    ASSERT_NO_FATAL_FAILURE(assertRelativeModeCurvesInUse());

    // Changing the pointer speed settings shouldn't result in an immediate change to the curve,
    // since we're in pointer capture.
    mReaderConfiguration.touchpadAccelerationEnabled = true;
    mReaderConfiguration.touchpadPointerSpeed = NEW_POINTER_SPEED;
    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::TOUCHPAD_SETTINGS);
    ASSERT_NO_FATAL_FAILURE(assertRelativeModeCurvesInUse());

    // ...but once we leave capture, the new speed should take effect.
    mReaderConfiguration.pointerCaptureRequest.mode = PointerCaptureMode::UNCAPTURED;
    reconfigureMapper(ARBITRARY_TIME, mReaderConfiguration,
                      InputReaderConfiguration::Change::POINTER_CAPTURE);
    mFakeListener.expectDeviceReset(AllOf(WithDeviceId(DEVICE_ID), WithEventTime(ARBITRARY_TIME)));
    mFakeListener.assertNoEvents();

    ASSERT_NO_FATAL_FAILURE(
            assertBothCurvesEqual(createAccelerationCurveForPointerSensitivity(NEW_POINTER_SPEED)));
}

} // namespace android
