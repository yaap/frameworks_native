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

#include <memory>

#include <android/input.h>
#include <gestures/RelativeModeGestureConverter.h>
#include <gtest/gtest.h>
#include <utils/StrongPointer.h>

#include "FakeEventHub.h"
#include "FakeInputReaderPolicy.h"
#include "InstrumentedInputReader.h"
#include "NotifyArgs.h"
#include "TestConstants.h"
#include "TestEventMatchers.h"
#include "TestInputListener.h"
#include "include/gestures.h"

namespace android {

namespace {

constexpr stime_t GESTURE_TIME = 1.2;

} // namespace

using testing::AllOf;
using testing::ElementsAre;
using testing::VariantWith;

class RelativeModeGestureConverterTest : public testing::Test {
protected:
    static constexpr int32_t DEVICE_ID = END_RESERVED_ID + 1000;

    RelativeModeGestureConverterTest()
          : mFakeEventHub(std::make_unique<FakeEventHub>()),
            mFakePolicy(sp<FakeInputReaderPolicy>::make()),
            mReader(std::make_unique<InstrumentedInputReader>(mFakeEventHub, mFakePolicy,
                                                              mFakeListener)),
            mConverter(*mReader->getContext(), DEVICE_ID) {}

    std::shared_ptr<FakeEventHub> mFakeEventHub;
    sp<FakeInputReaderPolicy> mFakePolicy;
    TestInputListener mFakeListener;
    std::unique_ptr<InstrumentedInputReader> mReader;
    RelativeModeGestureConverter mConverter;
};

TEST_F(RelativeModeGestureConverterTest, Move) {
    Gesture moveGesture(kGestureMove, GESTURE_TIME, GESTURE_TIME, -5, 10);
    std::list<NotifyArgs> args =
            mConverter.handleGesture(ARBITRARY_TIME, READ_TIME, ARBITRARY_TIME, moveGesture);
    ASSERT_THAT(args,
                ElementsAre(VariantWith<NotifyMotionArgs>(
                        AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE), WithCoords(-5, 10),
                              WithRelativeMotion(-5, 10), WithButtonState(0),
                              WithSource(AINPUT_SOURCE_MOUSE_RELATIVE), WithPressure(0.0f)))));
}

TEST_F(RelativeModeGestureConverterTest, ButtonsChange) {
    // Press left and right buttons at once
    Gesture downGesture(kGestureButtonsChange, GESTURE_TIME, GESTURE_TIME,
                        /*down=*/GESTURES_BUTTON_LEFT | GESTURES_BUTTON_RIGHT,
                        /*up=*/GESTURES_BUTTON_NONE, /*is_tap=*/false);
    std::list<NotifyArgs> args =
            mConverter.handleGesture(ARBITRARY_TIME, READ_TIME, ARBITRARY_TIME, downGesture);
    ASSERT_THAT(args,
                ElementsAre(VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN),
                                          WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY |
                                                          AMOTION_EVENT_BUTTON_SECONDARY))),
                            VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS),
                                          WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY),
                                          WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY))),
                            VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS),
                                          WithActionButton(AMOTION_EVENT_BUTTON_SECONDARY),
                                          WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY |
                                                          AMOTION_EVENT_BUTTON_SECONDARY)))));
    ASSERT_THAT(args,
                Each(VariantWith<NotifyMotionArgs>(AllOf(WithCoords(0, 0),
                                                         WithToolType(ToolType::MOUSE),
                                                         WithPressure(1.0f)))));

    // Then release the left button
    Gesture leftUpGesture(kGestureButtonsChange, GESTURE_TIME, GESTURE_TIME,
                          /*down=*/GESTURES_BUTTON_NONE, /*up=*/GESTURES_BUTTON_LEFT,
                          /*is_tap=*/false);
    args = mConverter.handleGesture(ARBITRARY_TIME, READ_TIME, ARBITRARY_TIME, leftUpGesture);
    ASSERT_THAT(args,
                ElementsAre(VariantWith<NotifyMotionArgs>(
                        AllOf(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_RELEASE),
                              WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY),
                              WithButtonState(AMOTION_EVENT_BUTTON_SECONDARY), WithCoords(0, 0),
                              WithToolType(ToolType::MOUSE), WithPressure(1.0f)))));

    // Finally release the right button
    Gesture rightUpGesture(kGestureButtonsChange, GESTURE_TIME, GESTURE_TIME,
                           /*down=*/GESTURES_BUTTON_NONE, /*up=*/GESTURES_BUTTON_RIGHT,
                           /*is_tap=*/false);
    args = mConverter.handleGesture(ARBITRARY_TIME, READ_TIME, ARBITRARY_TIME, rightUpGesture);
    ASSERT_THAT(args,
                ElementsAre(VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_RELEASE),
                                          WithActionButton(AMOTION_EVENT_BUTTON_SECONDARY),
                                          WithButtonState(0), WithPressure(1.0f))),
                            VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_UP),
                                          WithButtonState(0), WithPressure(0.0f)))));
    ASSERT_THAT(args,
                Each(VariantWith<NotifyMotionArgs>(
                        AllOf(WithCoords(0, 0), WithToolType(ToolType::MOUSE)))));
}

TEST_F(RelativeModeGestureConverterTest, DragWithButton) {
    // Press the button
    Gesture downGesture(kGestureButtonsChange, GESTURE_TIME, GESTURE_TIME,
                        /*down=*/GESTURES_BUTTON_LEFT, /*up=*/GESTURES_BUTTON_NONE,
                        /*is_tap=*/false);
    std::list<NotifyArgs> args =
            mConverter.handleGesture(ARBITRARY_TIME, READ_TIME, ARBITRARY_TIME, downGesture);
    ASSERT_THAT(args,
                ElementsAre(VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN),
                                          WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY),
                                          WithPressure(1.0f))),
                            VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS),
                                          WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY),
                                          WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY),
                                          WithPressure(1.0f)))));
    ASSERT_THAT(args,
                Each(VariantWith<NotifyMotionArgs>(
                        AllOf(WithCoords(0, 0), WithToolType(ToolType::MOUSE)))));

    // Move
    Gesture moveGesture(kGestureMove, GESTURE_TIME, GESTURE_TIME, -5, 10);
    args = mConverter.handleGesture(ARBITRARY_TIME, READ_TIME, ARBITRARY_TIME, moveGesture);
    ASSERT_THAT(args,
                ElementsAre(VariantWith<NotifyMotionArgs>(
                        AllOf(WithMotionAction(AMOTION_EVENT_ACTION_MOVE), WithCoords(-5, 10),
                              WithRelativeMotion(-5, 10), WithToolType(ToolType::MOUSE),
                              WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY), WithPressure(1.0f)))));

    // Release the button
    Gesture upGesture(kGestureButtonsChange, GESTURE_TIME, GESTURE_TIME,
                      /*down=*/GESTURES_BUTTON_NONE, /*up=*/GESTURES_BUTTON_LEFT,
                      /*is_tap=*/false);
    args = mConverter.handleGesture(ARBITRARY_TIME, READ_TIME, ARBITRARY_TIME, upGesture);
    ASSERT_THAT(args,
                ElementsAre(VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_RELEASE),
                                          WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY),
                                          WithButtonState(0), WithPressure(1.0f))),
                            VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_UP),
                                          WithButtonState(0), WithPressure(0.0f)))));
    ASSERT_THAT(args,
                Each(VariantWith<NotifyMotionArgs>(
                        AllOf(WithCoords(0, 0), WithToolType(ToolType::MOUSE)))));
}

TEST_F(RelativeModeGestureConverterTest, Tap) {
    // Tap should produce button press/release events
    Gesture tapGesture(kGestureButtonsChange, GESTURE_TIME, GESTURE_TIME,
                       /*down=*/GESTURES_BUTTON_LEFT, /*up=*/GESTURES_BUTTON_LEFT, /*is_tap=*/true);
    std::list<NotifyArgs> args =
            mConverter.handleGesture(ARBITRARY_TIME, READ_TIME, ARBITRARY_TIME, tapGesture);

    ASSERT_THAT(args,
                ElementsAre(VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_DOWN),
                                          WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY),
                                          WithPressure(1.0f))),
                            VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_PRESS),
                                          WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY),
                                          WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY),
                                          WithPressure(1.0f))),
                            VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_RELEASE),
                                          WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY),
                                          WithButtonState(0), WithPressure(1.0f))),
                            VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_UP),
                                          WithButtonState(0), WithPressure(0.0f)))));
    ASSERT_THAT(args,
                Each(VariantWith<NotifyMotionArgs>(
                        AllOf(WithCoords(0, 0), WithToolType(ToolType::MOUSE)))));
}

TEST_F(RelativeModeGestureConverterTest, Scroll) {
    Gesture scrollGesture(kGestureScroll, GESTURE_TIME, GESTURE_TIME, /*dx=*/10, /*dy=*/-5);
    std::list<NotifyArgs> args =
            mConverter.handleGesture(ARBITRARY_TIME, READ_TIME, ARBITRARY_TIME, scrollGesture);
    ASSERT_THAT(args,
                ElementsAre(VariantWith<NotifyMotionArgs>(
                        AllOf(WithMotionAction(AMOTION_EVENT_ACTION_SCROLL), WithScroll(10, -5),
                              WithButtonState(0), WithSource(AINPUT_SOURCE_MOUSE_RELATIVE),
                              WithPressure(0.0f)))));
}

TEST_F(RelativeModeGestureConverterTest, ScrollWithButtonPressed) {
    Gesture downGesture(kGestureButtonsChange, GESTURE_TIME, GESTURE_TIME,
                        /*down=*/GESTURES_BUTTON_LEFT, /*up=*/GESTURES_BUTTON_NONE,
                        /*is_tap=*/false);
    (void)mConverter.handleGesture(ARBITRARY_TIME, READ_TIME, ARBITRARY_TIME, downGesture);

    Gesture scrollGesture(kGestureScroll, GESTURE_TIME, GESTURE_TIME, /*dx=*/10, /*dy=*/-5);
    std::list<NotifyArgs> args =
            mConverter.handleGesture(ARBITRARY_TIME, READ_TIME, ARBITRARY_TIME, scrollGesture);
    ASSERT_THAT(args,
                ElementsAre(VariantWith<NotifyMotionArgs>(
                        AllOf(WithMotionAction(AMOTION_EVENT_ACTION_SCROLL), WithScroll(10, -5),
                              WithButtonState(AMOTION_EVENT_BUTTON_PRIMARY),
                              WithSource(AINPUT_SOURCE_MOUSE_RELATIVE), WithPressure(1.0f)))));
}

TEST_F(RelativeModeGestureConverterTest, ResetWithButtonPressed) {
    Gesture downGesture(kGestureButtonsChange, GESTURE_TIME, GESTURE_TIME,
                        /*down=*/GESTURES_BUTTON_LEFT | GESTURES_BUTTON_RIGHT,
                        /*up=*/GESTURES_BUTTON_NONE, /*is_tap=*/false);
    (void)mConverter.handleGesture(ARBITRARY_TIME, READ_TIME, ARBITRARY_TIME, downGesture);

    std::list<NotifyArgs> args = mConverter.reset(ARBITRARY_TIME);
    ASSERT_THAT(args,
                ElementsAre(VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_RELEASE),
                                          WithActionButton(AMOTION_EVENT_BUTTON_PRIMARY),
                                          WithButtonState(AMOTION_EVENT_BUTTON_SECONDARY),
                                          WithPressure(1.0f))),
                            VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_BUTTON_RELEASE),
                                          WithActionButton(AMOTION_EVENT_BUTTON_SECONDARY),
                                          WithButtonState(0), WithPressure(1.0f))),
                            VariantWith<NotifyMotionArgs>(
                                    AllOf(WithMotionAction(AMOTION_EVENT_ACTION_UP),
                                          WithButtonState(0), WithPressure(0.0f)))));
    ASSERT_THAT(args,
                Each(VariantWith<NotifyMotionArgs>(
                        AllOf(WithCoords(0, 0), WithToolType(ToolType::MOUSE)))));
}

TEST_F(RelativeModeGestureConverterTest, DownTime) {
    // A move event before any buttons are pressed should have a down time of 0.
    Gesture moveGesture1(kGestureMove, GESTURE_TIME, GESTURE_TIME, -5, 10);
    std::list<NotifyArgs> args =
            mConverter.handleGesture(ARBITRARY_TIME, ARBITRARY_TIME, ARBITRARY_TIME, moveGesture1);
    ASSERT_THAT(args, Each(VariantWith<NotifyMotionArgs>(WithDownTime(0))));

    // Press the button. The down time should be updated to the event time.
    constexpr nsecs_t downEventTime = 5678;
    Gesture downGesture(kGestureButtonsChange, GESTURE_TIME, GESTURE_TIME,
                        /*down=*/GESTURES_BUTTON_LEFT, /*up=*/GESTURES_BUTTON_NONE,
                        /*is_tap=*/false);
    args = mConverter.handleGesture(downEventTime, downEventTime, ARBITRARY_TIME, downGesture);
    ASSERT_THAT(args, Each(VariantWith<NotifyMotionArgs>(WithDownTime(downEventTime))));

    // Events from subsequent movements and button releases should have the updated down time.
    constexpr nsecs_t move2Time = downEventTime + 200;
    Gesture moveGesture2(kGestureMove, GESTURE_TIME, GESTURE_TIME, -5, 10);
    args = mConverter.handleGesture(move2Time, move2Time, move2Time, moveGesture2);
    ASSERT_THAT(args, Each(VariantWith<NotifyMotionArgs>(WithDownTime(downEventTime))));

    // Release the button. The down time should still be the same.
    constexpr nsecs_t upTime = downEventTime + 400;
    Gesture upGesture(kGestureButtonsChange, GESTURE_TIME, GESTURE_TIME,
                      /*down=*/GESTURES_BUTTON_NONE, /*up=*/GESTURES_BUTTON_LEFT,
                      /*is_tap=*/false);
    args = mConverter.handleGesture(upTime, upTime, upTime, upGesture);
    ASSERT_THAT(args, Each(VariantWith<NotifyMotionArgs>(WithDownTime(downEventTime))));
}

} // namespace android
