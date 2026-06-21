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

#include "MultiTouchInputMapper.h"

#include <android-base/logging.h>
#include <gtest/gtest.h>
#include <list>
#include <optional>

#include "InputMapperTest.h"
#include "InterfaceMocks.h"
#include "TestEventMatchers.h"
#include "input/ScopedFlagOverride.h"

#define TAG "MultiTouchpadInputMapperUnit_test"

namespace android {

using testing::_;
using testing::AllOf;
using testing::IsEmpty;
using testing::Return;
using testing::SetArgPointee;
using testing::VariantWith;

namespace {

constexpr ui::LogicalDisplayId DISPLAY_ID = ui::LogicalDisplayId::DEFAULT;
constexpr ui::LogicalDisplayId SECOND_DISPLAY_ID = ui::LogicalDisplayId{DISPLAY_ID.val() + 1};
constexpr int32_t DISPLAY_WIDTH = 480;
constexpr int32_t DISPLAY_HEIGHT = 800;
constexpr std::optional<uint8_t> NO_PORT = std::nullopt; // no physical port is specified
constexpr int32_t SLOT_COUNT = 5;

constexpr int32_t ACTION_DOWN = AMOTION_EVENT_ACTION_DOWN;
constexpr int32_t ACTION_CANCEL = AMOTION_EVENT_ACTION_CANCEL;
constexpr int32_t ACTION_POINTER_0_UP =
        AMOTION_EVENT_ACTION_POINTER_UP | (0 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
constexpr int32_t ACTION_POINTER_1_DOWN =
        AMOTION_EVENT_ACTION_POINTER_DOWN | (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);

} // namespace

/**
 * Unit tests for MultiTouchInputMapper.
 */
class MultiTouchInputMapperUnitTest : public VerifyingInputMapperUnitTest {
protected:
    void SetUp() override { SetUp(/*bus=*/0, /*isExternal=*/false); }
    void SetUp(int bus, bool isExternal) override {
        VerifyingInputMapperUnitTest::SetUp(bus, isExternal);

        // Present scan codes
        expectScanCodes(/*present=*/true,
                        {BTN_TOUCH, BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP,
                         BTN_TOOL_QUADTAP, BTN_TOOL_QUINTTAP});

        // Missing scan codes that the mapper checks for.
        expectScanCodes(/*present=*/false,
                        {BTN_TOOL_PEN, BTN_TOOL_RUBBER, BTN_TOOL_BRUSH, BTN_TOOL_PENCIL,
                         BTN_TOOL_AIRBRUSH});

        // Current scan code state - all keys are UP by default
        setScanCodeState(KeyState::UP, {BTN_LEFT,           BTN_RIGHT,        BTN_MIDDLE,
                                        BTN_BACK,           BTN_SIDE,         BTN_FORWARD,
                                        BTN_EXTRA,          BTN_TASK,         BTN_TOUCH,
                                        BTN_STYLUS,         BTN_STYLUS2,      BTN_0,
                                        BTN_TOOL_FINGER,    BTN_TOOL_PEN,     BTN_TOOL_RUBBER,
                                        BTN_TOOL_BRUSH,     BTN_TOOL_PENCIL,  BTN_TOOL_AIRBRUSH,
                                        BTN_TOOL_MOUSE,     BTN_TOOL_LENS,    BTN_TOOL_DOUBLETAP,
                                        BTN_TOOL_TRIPLETAP, BTN_TOOL_QUADTAP, BTN_TOOL_QUINTTAP});

        setKeyCodeState(KeyState::UP,
                        {AKEYCODE_STYLUS_BUTTON_PRIMARY, AKEYCODE_STYLUS_BUTTON_SECONDARY});

        // Input properties - only INPUT_PROP_DIRECT for touchscreen
        EXPECT_CALL(mMockEventHub, hasInputProperty(EVENTHUB_ID, _)).WillRepeatedly(Return(false));
        EXPECT_CALL(mMockEventHub, hasInputProperty(EVENTHUB_ID, INPUT_PROP_DIRECT))
                .WillRepeatedly(Return(true));
        // The following EXPECT_CALL lines are not load-bearing, but without them gtest prints
        // warnings about "uninteresting mocked call", which are distracting when developing the
        // tests because this text is interleaved with logs of interest.
        EXPECT_CALL(mMockEventHub, getVirtualKeyDefinitions(EVENTHUB_ID, _))
                .WillRepeatedly(Return());
        EXPECT_CALL(mMockEventHub, hasRelativeAxis(EVENTHUB_ID, _))
                .WillRepeatedly(testing::Return(false));
        EXPECT_CALL(mMockEventHub, getVideoFrames(EVENTHUB_ID))
                .WillRepeatedly(testing::Return(std::vector<TouchVideoFrame>{}));
        EXPECT_CALL(mMockInputReaderContext, getExternalStylusDevices(_)).WillRepeatedly(Return());
        EXPECT_CALL(mMockInputReaderContext, getGlobalMetaState()).WillRepeatedly(Return(0));

        // Axes that the device has
        setupAxis(ABS_MT_SLOT, /*valid=*/true, /*min=*/0, /*max=*/SLOT_COUNT - 1, /*resolution=*/0);
        setupAxis(ABS_MT_TRACKING_ID, /*valid=*/true, /*min*/ 0, /*max=*/255, /*resolution=*/0);
        setupAxis(ABS_MT_POSITION_X, /*valid=*/true, /*min=*/0, /*max=*/2000, /*resolution=*/24);
        setupAxis(ABS_MT_POSITION_Y, /*valid=*/true, /*min=*/0, /*max=*/1000, /*resolution=*/24);

        // Axes that the device does not have
        setupAxis(ABS_MT_PRESSURE, /*valid=*/false, /*min*/ 0, /*max=*/255, /*resolution=*/0);
        setupAxis(ABS_MT_ORIENTATION, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_DISTANCE, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_TOUCH_MAJOR, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_TOUCH_MINOR, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_WIDTH_MAJOR, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_WIDTH_MINOR, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);
        setupAxis(ABS_MT_TOOL_TYPE, /*valid=*/false, /*min=*/0, /*max=*/0, /*resolution=*/0);

        // reset current slot at the beginning
        EXPECT_CALL(mMockEventHub, getAbsoluteAxisValue(EVENTHUB_ID, ABS_MT_SLOT))
                .WillRepeatedly(Return(0));

        // mark all slots not in use
        mockSlotValues({});

        mFakePolicy->setDefaultPointerDisplayId(DISPLAY_ID);
        DisplayViewport internalViewport =
                createViewport(DISPLAY_ID, DISPLAY_WIDTH, DISPLAY_HEIGHT, ui::ROTATION_0,
                               /*isActive=*/true, "local:0", NO_PORT, ViewportType::INTERNAL);
        mFakePolicy->addDisplayViewport(internalViewport);
        mMapper = createInputMapper<MultiTouchInputMapper>(*mDeviceContext,
                                                           mFakePolicy->getReaderConfiguration());
    }

    // Mocks position and tracking Ids for the provided slots. Remaining slots will be marked
    // unused.
    void mockSlotValues(
            const std::unordered_map<int32_t /*slotIndex*/,
                                     std::pair<Point /*position*/, int32_t /*trackingId*/>>&
                    slotValues) {
        EXPECT_CALL(mMockEventHub, getMtSlotValues(EVENTHUB_ID, _, SLOT_COUNT))
                .WillRepeatedly([=](int32_t, int32_t axis,
                                    size_t slotCount) -> base::Result<std::vector<int32_t>> {
                    // tracking Id for the unused slots must set to be < 0
                    std::vector<int32_t> outMtSlotValues(slotCount + 1, -1);
                    outMtSlotValues[0] = axis;
                    switch (axis) {
                        case ABS_MT_POSITION_X:
                            for (const auto& [slotIndex, valuePair] : slotValues) {
                                outMtSlotValues[slotIndex] = valuePair.first.x;
                            }
                            return outMtSlotValues;
                        case ABS_MT_POSITION_Y:
                            for (const auto& [slotIndex, valuePair] : slotValues) {
                                outMtSlotValues[slotIndex] = valuePair.first.y;
                            }
                            return outMtSlotValues;
                        case ABS_MT_TRACKING_ID:
                            for (const auto& [slotIndex, valuePair] : slotValues) {
                                outMtSlotValues[slotIndex] = valuePair.second;
                            }
                            return outMtSlotValues;
                        default:
                            return base::ResultError("Axis not supported", NAME_NOT_FOUND);
                    }
                });
    }
};

/**
 * While a gesture is active, change the display that the device is associated with. Make sure that
 * the CANCEL event that's generated has the display id of the original DOWN event, rather than the
 * new display id.
 */
TEST_F(MultiTouchInputMapperUnitTest, ChangeAssociatedDisplayIdWhenTouchIsActive) {
    // Add a second viewport that later will be associated with our device.
    DisplayViewport secondViewport =
            createViewport(SECOND_DISPLAY_ID, DISPLAY_WIDTH, DISPLAY_HEIGHT, ui::ROTATION_0,
                           /*isActive=*/true, "local:1", NO_PORT, ViewportType::EXTERNAL);
    mFakePolicy->addDisplayViewport(secondViewport);
    std::optional<DisplayViewport> firstViewport =
            mFakePolicy->getDisplayViewportByUniqueId("local:0");

    // InputReaderConfiguration contains information about how devices are associated with displays.
    // The mapper receives this information. However, it doesn't actually parse it - that's done by
    // InputDevice. The mapper asks InputDevice about the associated viewport, so that's what we
    // need to mock here to simulate association. This abstraction is confusing and should be
    // refactored.

    // Start with the first viewport
    ON_CALL((*mDevice), getAssociatedViewport).WillByDefault(Return(firstViewport));
    reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                      InputReaderConfiguration::Change::DISPLAY_INFO);

    int32_t x1 = 100, y1 = 125;
    process(EV_KEY, BTN_TOUCH, 1);
    process(EV_ABS, ABS_MT_POSITION_X, x1);
    process(EV_ABS, ABS_MT_POSITION_Y, y1);
    process(EV_ABS, ABS_MT_TRACKING_ID, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN), WithDisplayId(DISPLAY_ID)));

    // Now associate with the second viewport, and reconfigure.
    ON_CALL((*mDevice), getAssociatedViewport).WillByDefault(Return(secondViewport));
    reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                      InputReaderConfiguration::Change::DISPLAY_INFO);
    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_CANCEL), WithDisplayId(DISPLAY_ID)));
    mFakeListener.expectDeviceReset(WithDeviceId(DEVICE_ID));

    // The remainder of the gesture is ignored
    // Move.
    x1 += 10;
    y1 += 15;
    process(EV_ABS, ABS_MT_POSITION_X, x1);
    process(EV_ABS, ABS_MT_POSITION_Y, y1);
    process(EV_SYN, SYN_REPORT, 0);
    // Up
    process(EV_KEY, BTN_TOUCH, 0);
    process(EV_ABS, ABS_MT_TRACKING_ID, -1);
    process(EV_SYN, SYN_REPORT, 0);

    // New touch is delivered with the new display id.
    process(EV_ABS, ABS_MT_TRACKING_ID, 2);
    process(EV_KEY, BTN_TOUCH, 1);
    process(EV_ABS, ABS_MT_POSITION_X, x1 + 20);
    process(EV_ABS, ABS_MT_POSITION_Y, y1 + 40);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN), WithDisplayId(SECOND_DISPLAY_ID)));
}

// This test simulates a multi-finger gesture with unexpected reset in between. This might happen
// due to buffer overflow and device with report a SYN_DROPPED. In this case we expect mapper to be
// reset, MT slot state to be re-populated and the gesture should be cancelled and restarted.
TEST_F(MultiTouchInputMapperUnitTest, MultiFingerGestureWithUnexpectedReset) {
    // Two fingers down at once.
    constexpr int32_t FIRST_TRACKING_ID = 1, SECOND_TRACKING_ID = 2;
    int32_t x1 = 100, y1 = 125, x2 = 200, y2 = 225;
    process(EV_KEY, BTN_TOUCH, 1);
    process(EV_ABS, ABS_MT_POSITION_X, x1);
    process(EV_ABS, ABS_MT_POSITION_Y, y1);
    process(EV_ABS, ABS_MT_TRACKING_ID, FIRST_TRACKING_ID);
    process(EV_ABS, ABS_MT_SLOT, 1);
    process(EV_ABS, ABS_MT_POSITION_X, x2);
    process(EV_ABS, ABS_MT_POSITION_Y, y2);
    process(EV_ABS, ABS_MT_TRACKING_ID, SECOND_TRACKING_ID);

    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion((WithMotionAction(AMOTION_EVENT_ACTION_DOWN)));
    mFakeListener.expectMotion((WithMotionAction(ACTION_POINTER_1_DOWN)));

    // Move.
    x1 += 10;
    y1 += 15;
    x2 += 5;
    y2 -= 10;
    process(EV_ABS, ABS_MT_SLOT, 0);
    process(EV_ABS, ABS_MT_POSITION_X, x1);
    process(EV_ABS, ABS_MT_POSITION_Y, y1);
    process(EV_ABS, ABS_MT_SLOT, 1);
    process(EV_ABS, ABS_MT_POSITION_X, x2);
    process(EV_ABS, ABS_MT_POSITION_Y, y2);

    process(EV_SYN, SYN_REPORT, 0);
    NotifyMotionArgs args = mFakeListener.expectMotion(WithMotionAction(AMOTION_EVENT_ACTION_MOVE));
    const auto pointerCoordsBeforeReset = args.pointerCoords;

    // On buffer overflow mapper will be reset and MT slots data will be repopulated
    EXPECT_CALL(mMockEventHub, getAbsoluteAxisValue(EVENTHUB_ID, ABS_MT_SLOT))
            .WillRepeatedly(Return(1));

    mockSlotValues(
            {{1, {Point{x1, y1}, FIRST_TRACKING_ID}}, {2, {Point{x2, y2}, SECOND_TRACKING_ID}}});

    setScanCodeState(KeyState::DOWN, {BTN_TOUCH});

    resetMapper(systemTime(SYSTEM_TIME_MONOTONIC));
    mFakeListener.expectMotion(WithMotionAction(AMOTION_EVENT_ACTION_CANCEL));

    // SYN_REPORT should restart the gesture again
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(WithMotionAction(AMOTION_EVENT_ACTION_DOWN));
    NotifyMotionArgs motion = mFakeListener.expectMotion(WithMotionAction(ACTION_POINTER_1_DOWN));
    ASSERT_EQ(motion.pointerCoords, pointerCoordsBeforeReset);

    // Move.
    x1 += 10;
    y1 += 15;
    x2 += 5;
    y2 -= 10;
    process(EV_ABS, ABS_MT_SLOT, 0);
    process(EV_ABS, ABS_MT_POSITION_X, x1);
    process(EV_ABS, ABS_MT_POSITION_Y, y1);
    process(EV_ABS, ABS_MT_SLOT, 1);
    process(EV_ABS, ABS_MT_POSITION_X, x2);
    process(EV_ABS, ABS_MT_POSITION_Y, y2);

    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(WithMotionAction(AMOTION_EVENT_ACTION_MOVE));

    // First finger up.
    process(EV_ABS, ABS_MT_SLOT, 0);
    process(EV_ABS, ABS_MT_TRACKING_ID, -1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(WithMotionAction(ACTION_POINTER_0_UP));

    // Second finger up.
    process(EV_KEY, BTN_TOUCH, 0);
    process(EV_ABS, ABS_MT_SLOT, 1);
    process(EV_ABS, ABS_MT_TRACKING_ID, -1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(WithMotionAction(AMOTION_EVENT_ACTION_UP));
}

/**
 * Check what happens when two pointers are hovering (BTN_TOUCH is not pressed).
 */
TEST_F(MultiTouchInputMapperUnitTest, TwoPointersHoveringWithoutBtnTouch) {
    // Set up two pointers hovering (BTN_TOUCH is not pressed)
    process(EV_ABS, ABS_MT_SLOT, 0);
    process(EV_ABS, ABS_MT_TRACKING_ID, 0);
    process(EV_ABS, ABS_MT_POSITION_X, 100);
    process(EV_ABS, ABS_MT_POSITION_Y, 100);

    process(EV_ABS, ABS_MT_SLOT, 1);
    process(EV_ABS, ABS_MT_TRACKING_ID, 1);
    process(EV_ABS, ABS_MT_POSITION_X, 200);
    process(EV_ABS, ABS_MT_POSITION_Y, 200);
    process(EV_SYN, SYN_REPORT, 0);

    // In general, Android does not support two pointers hovering. No events should be produced,
    // since both pointers are being added in the same frame here.
    mFakeListener.assertNoEvents();

    // Now lift pointer 0. Pointer 1 remains.
    process(EV_ABS, ABS_MT_SLOT, 0);
    process(EV_ABS, ABS_MT_TRACKING_ID, -1);
    process(EV_SYN, SYN_REPORT, 0);

    // We expect HOVER_MOVE with 1 pointer (pointer 1)
    // Since we previously dropped 2 pointers, this looks like we are transitioning from 0 to 1
    // hovering pointer.
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER),
                                     WithPointerCount(1), WithPointerId(0, 1)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_MOVE),
                                     WithPointerCount(1), WithPointerId(0, 1)));
}

/**
 * Check what happens when two pointers are hovering (pressure is 0).
 */
TEST_F(MultiTouchInputMapperUnitTest, TwoPointersHoveringWithPressure) {
    // Reconfigure the device to support pressure
    setupAxis(ABS_MT_PRESSURE, /*valid=*/true, /*min=*/0, /*max=*/255, /*resolution=*/0);
    mMapper = createInputMapper<MultiTouchInputMapper>(*mDeviceContext,
                                                       mFakePolicy->getReaderConfiguration());

    // Press BTN_TOUCH, but pressure is 0. This should cause the pointers to hover.
    process(EV_KEY, BTN_TOUCH, 1);

    // Pointer 0: Hovering (pressure 0)
    process(EV_ABS, ABS_MT_SLOT, 0);
    process(EV_ABS, ABS_MT_TRACKING_ID, 0);
    process(EV_ABS, ABS_MT_POSITION_X, 100);
    process(EV_ABS, ABS_MT_POSITION_Y, 100);
    process(EV_ABS, ABS_MT_PRESSURE, 0);

    // Pointer 1: Hovering (pressure 0)
    process(EV_ABS, ABS_MT_SLOT, 1);
    process(EV_ABS, ABS_MT_TRACKING_ID, 1);
    process(EV_ABS, ABS_MT_POSITION_X, 200);
    process(EV_ABS, ABS_MT_POSITION_Y, 200);
    process(EV_ABS, ABS_MT_PRESSURE, 0);
    process(EV_SYN, SYN_REPORT, 0);

    // In general, Android does not support two pointers hovering. No events should be produced,
    // since both pointers are being added in the same frame here.
    mFakeListener.assertNoEvents();
}

/**
 * Start with one hovering pointer, and then add a second one. Ensure that the second hovering
 * pointer is not reported, because Android does not support multiple hovering pointers.
 */
TEST_F(MultiTouchInputMapperUnitTest, OneToTwoPointersHovering) {
    // Start with 1 pointer hovering (P0)
    process(EV_ABS, ABS_MT_SLOT, 0);
    process(EV_ABS, ABS_MT_TRACKING_ID, 0);
    process(EV_ABS, ABS_MT_POSITION_X, 100);
    process(EV_ABS, ABS_MT_POSITION_Y, 100);
    process(EV_SYN, SYN_REPORT, 0);

    // Expect HOVER_ENTER {0}
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER),
                                     WithPointerCount(1), WithPointerId(0, 0)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_MOVE),
                                     WithPointerCount(1), WithPointerId(0, 0)));

    // Add second pointer hovering (P1). Now have P0, P1.
    process(EV_ABS, ABS_MT_SLOT, 1);
    process(EV_ABS, ABS_MT_TRACKING_ID, 1);
    process(EV_ABS, ABS_MT_POSITION_X, 200);
    process(EV_ABS, ABS_MT_POSITION_Y, 200);
    process(EV_SYN, SYN_REPORT, 0);

    // Expect HOVER_EXIT {0} because multi-pointer hover is suppressed.
    // The mapper detects >1 pointers, clears them, so cooked state becomes empty.
    // Transition from {0} to {} triggers HOVER_EXIT {0}.
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_EXIT),
                                     WithPointerCount(1), WithPointerId(0, 0)));

    // Move pointers (P0, P1).
    process(EV_ABS, ABS_MT_SLOT, 0);
    process(EV_ABS, ABS_MT_POSITION_X, 110);
    process(EV_ABS, ABS_MT_POSITION_Y, 110);
    process(EV_SYN, SYN_REPORT, 0);
    // Still suppressed.
    mFakeListener.assertNoEvents();

    // Lift P0. P1 remains.
    process(EV_ABS, ABS_MT_SLOT, 0);
    process(EV_ABS, ABS_MT_TRACKING_ID, -1);
    process(EV_SYN, SYN_REPORT, 0);

    // Transition from {} to {1}. Expect HOVER_ENTER {1}.
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER),
                                     WithPointerCount(1), WithPointerId(0, 1)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_MOVE),
                                     WithPointerCount(1), WithPointerId(0, 1)));
}

class ExternalMultiTouchInputMapperTest : public MultiTouchInputMapperUnitTest {
protected:
    void SetUp() override { MultiTouchInputMapperUnitTest::SetUp(/*bus=*/0, /*isExternal=*/true); }
};

/**
 * Expect fallback to internal viewport if device is external and external viewport is not present.
 */
TEST_F(ExternalMultiTouchInputMapperTest, Viewports_Fallback) {
    // Expect the event to be sent to the internal viewport,
    // because an external viewport is not present.
    process(EV_KEY, BTN_TOUCH, 1);
    process(EV_ABS, ABS_MT_TRACKING_ID, 1);
    process(EV_ABS, ABS_MT_POSITION_X, 100);
    process(EV_ABS, ABS_MT_POSITION_Y, 200);
    process(EV_SYN, SYN_REPORT, 0);

    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_DOWN), WithDisplayId(DISPLAY_ID)));

    // Expect the event to be sent to the external viewport if it is present.
    DisplayViewport externalViewport =
            createViewport(SECOND_DISPLAY_ID, DISPLAY_WIDTH, DISPLAY_HEIGHT, ui::ROTATION_0,
                           /*isActive=*/true, "local:1", NO_PORT, ViewportType::EXTERNAL);
    mFakePolicy->addDisplayViewport(externalViewport);
    std::optional<DisplayViewport> internalViewport =
            mFakePolicy->getDisplayViewportByUniqueId("local:0");
    mReaderConfiguration.setDisplayViewports({*internalViewport, externalViewport});
    reconfigureMapper(systemTime(SYSTEM_TIME_MONOTONIC), mReaderConfiguration,
                      InputReaderConfiguration::Change::DISPLAY_INFO);

    mFakeListener.expectMotion(AllOf(WithMotionAction(ACTION_CANCEL), WithDisplayId(DISPLAY_ID)));
    mFakeListener.expectDeviceReset(WithDeviceId(DEVICE_ID));
    // Lift up the old pointer.
    process(EV_KEY, BTN_TOUCH, 0);
    process(EV_ABS, ABS_MT_TRACKING_ID, -1);
    process(EV_SYN, SYN_REPORT, 0);

    // Send new pointer
    process(EV_KEY, BTN_TOUCH, 1);
    process(EV_ABS, ABS_MT_TRACKING_ID, 2);
    process(EV_ABS, ABS_MT_POSITION_X, 111);
    process(EV_ABS, ABS_MT_POSITION_Y, 211);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(ACTION_DOWN), WithDisplayId(SECOND_DISPLAY_ID)));
}

class MultiTouchInputMapperPointerModeUnitTest : public MultiTouchInputMapperUnitTest {
protected:
    void SetUp() override {
        MultiTouchInputMapperUnitTest::SetUp();

        // TouchInputMapper goes into POINTER mode whenever INPUT_PROP_DIRECT is not set.
        EXPECT_CALL(mMockEventHub, hasInputProperty(EVENTHUB_ID, INPUT_PROP_DIRECT))
                .WillRepeatedly(Return(false));

        mMapper = createInputMapper<MultiTouchInputMapper>(*mDeviceContext,
                                                           mFakePolicy->getReaderConfiguration());
    }
};

TEST_F(MultiTouchInputMapperPointerModeUnitTest, MouseToolOnlyDownWhenMouseButtonsAreDown) {
    // Set the tool type to mouse.
    process(EV_KEY, BTN_TOOL_MOUSE, 1);
    process(EV_ABS, ABS_MT_POSITION_X, 100);
    process(EV_ABS, ABS_MT_POSITION_Y, 100);
    process(EV_ABS, ABS_MT_TRACKING_ID, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER),
                                     WithToolType(ToolType::MOUSE)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_MOVE),
                                     WithToolType(ToolType::MOUSE)));

    // Setting BTN_TOUCH does not make a mouse pointer go down.
    process(EV_KEY, BTN_TOUCH, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_MOVE));

    // The mouse button is pressed, so the mouse goes down.
    process(EV_KEY, BTN_MOUSE, 1);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_EXIT),
                                     WithToolType(ToolType::MOUSE)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN),
                                     WithToolType(ToolType::MOUSE),
                                     WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS),
                                     WithToolType(ToolType::MOUSE),
                                     WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY),
                                     WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY)));

    // The mouse button is released, so the mouse starts hovering.
    process(EV_KEY, BTN_MOUSE, 0);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_RELEASE),
                                     WithButtonState(0), WithToolType(ToolType::MOUSE),
                                     WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_UP),
                                     WithToolType(ToolType::MOUSE), WithButtonState(0)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_ENTER),
                                     WithToolType(ToolType::MOUSE)));
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_MOVE),
                                     WithToolType(ToolType::MOUSE)));

    // Change the tool type so that it is no longer a mouse.
    // The default tool type is finger, and the finger is already down.
    process(EV_KEY, BTN_TOOL_MOUSE, 0);
    process(EV_SYN, SYN_REPORT, 0);
    mFakeListener.expectMotion(AllOf(WithMotionAction(AMOTION_EVENT_ACTION_HOVER_EXIT),
                                     WithToolType(ToolType::MOUSE)));
    mFakeListener.expectMotion(
            AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN), WithToolType(ToolType::FINGER)));
}

} // namespace android
