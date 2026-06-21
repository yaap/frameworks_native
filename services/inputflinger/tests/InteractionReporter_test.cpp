/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "../InteractionReporter.h"

#include <NotifyArgsBuilders.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <input/InputEventBuilders.h>
#include <chrono>

#include <attention/AttentionTypes.h>
#include <attention/InteractionProvider.h>
#include <attention/NativeInteractionManager.h>

#include "TestInputListener.h"

namespace android {

using namespace std::chrono_literals;
using testing::_;
using testing::Return;

class MockNativeInteractionManager : public attention::NativeInteractionManager {
public:
    MOCK_METHOD(bool, registerInteractionProvider,
                (std::shared_ptr<attention::InteractionProvider> provider), (override));
    MOCK_METHOD(bool, unregisterInteractionProvider,
                (std::shared_ptr<attention::InteractionProvider> provider), (override));
};

class TestInteractionReporter : public InteractionReporter {
public:
    explicit TestInteractionReporter(InputListenerInterface& nextListener)
          : InteractionReporter(nextListener) {}

    std::shared_ptr<attention::InteractionProvider> getInteractionProvider() {
        return mAttentionInteractionProvider;
    }
};

class InteractionReporterTest : public testing::Test {
protected:
    TestInputListener mTestListener;
    TestInteractionReporter mInteractionReporter{mTestListener};
};

TEST_F(InteractionReporterTest, NotifyInputDevicesChanged_ForwardsNotification) {
    NotifyInputDevicesChangedArgs args;

    mInteractionReporter.notifyInputDevicesChanged(args);

    mTestListener.assertNotifyInputDevicesChangedWasCalled();
}

TEST_F(InteractionReporterTest, NotifyKey_ReportsInteractionAndForwards) {
    const nsecs_t eventTime = 12345;

    mInteractionReporter.notifyKey(KeyArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_KEYBOARD)
                                           .eventTime(eventTime)
                                           .build());

    mTestListener.assertNotifyKeyWasCalled();
    std::vector<attention::InteractionState> interactions =
            mInteractionReporter.getInteractionProvider()->getSourceInteractions();
    ASSERT_EQ(1u, interactions.size());
    EXPECT_EQ(static_cast<int32_t>(attention::InteractionType::KEY),
              interactions[0].interactionTypes);
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::nanoseconds(eventTime))
                      .count(),
              interactions[0].interactionTimeMillis);
}

struct MotionInteractionTestParam {
    std::string_view name;
    int32_t action;
    attention::InteractionType expectedType;
};

class MotionInteractionTest : public InteractionReporterTest,
                              public testing::WithParamInterface<MotionInteractionTestParam> {};

TEST_P(MotionInteractionTest, NotifyMotion_ReportsCorrectInteraction) {
    const auto& [name, action, expectedType] = GetParam();
    const nsecs_t eventTime = 12345;

    mInteractionReporter.notifyMotion(
            MotionArgsBuilder(action, AINPUT_SOURCE_TOUCHSCREEN).eventTime(eventTime).build());

    mTestListener.assertNotifyMotionWasCalled();
    std::vector<attention::InteractionState> interactions =
            mInteractionReporter.getInteractionProvider()->getSourceInteractions();
    ASSERT_EQ(1u, interactions.size());
    EXPECT_EQ(static_cast<int32_t>(expectedType), interactions[0].interactionTypes);
    EXPECT_EQ(std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::nanoseconds(eventTime))
                      .count(),
              interactions[0].interactionTimeMillis);
}

INSTANTIATE_TEST_SUITE_P(
        InteractionReporterMotionTests, MotionInteractionTest,
        testing::Values(
                MotionInteractionTestParam{.name = "HoverEnterGeneratesHoverInteraction",
                                           .action = AMOTION_EVENT_ACTION_HOVER_ENTER,
                                           .expectedType = attention::InteractionType::HOVER},
                MotionInteractionTestParam{.name = "HoverExitGeneratesHoverInteraction",
                                           .action = AMOTION_EVENT_ACTION_HOVER_EXIT,
                                           .expectedType = attention::InteractionType::HOVER},
                MotionInteractionTestParam{.name = "HoverMoveGeneratesHoverInteraction",
                                           .action = AMOTION_EVENT_ACTION_HOVER_MOVE,
                                           .expectedType = attention::InteractionType::HOVER},
                MotionInteractionTestParam{.name = "ActionDownGeneratesGestureInteraction",
                                           .action = AMOTION_EVENT_ACTION_DOWN,
                                           .expectedType = attention::InteractionType::GESTURE},
                MotionInteractionTestParam{.name = "ActionUpGeneratesGestureInteraction",
                                           .action = AMOTION_EVENT_ACTION_UP,
                                           .expectedType = attention::InteractionType::GESTURE},
                MotionInteractionTestParam{.name = "ActionMoveGeneratesGestureInteraction",
                                           .action = AMOTION_EVENT_ACTION_MOVE,
                                           .expectedType = attention::InteractionType::GESTURE},
                MotionInteractionTestParam{.name = "ActionScrollGeneratesGestureInteraction",
                                           .action = AMOTION_EVENT_ACTION_SCROLL,
                                           .expectedType = attention::InteractionType::GESTURE}),
        [](const testing::TestParamInfo<MotionInteractionTest::ParamType>& p) {
            return std::string(p.param.name);
        });

TEST_F(InteractionReporterTest, SetInteractionProviderService_RegistersProvider) {
    auto mockManager = std::make_unique<MockNativeInteractionManager>();
    auto provider = mInteractionReporter.getInteractionProvider();
    EXPECT_CALL(*mockManager, registerInteractionProvider(provider)).WillOnce(Return(true));

    mInteractionReporter.setInteractionProviderService(std::move(mockManager));
}

TEST_F(InteractionReporterTest, NotifyMotion_TriggersCallback) {
    bool callbackCalled = false;
    mInteractionReporter.getInteractionProvider()->requestWakeupCallback(
            [&callbackCalled]() { callbackCalled = true; });

    mInteractionReporter.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN).build());

    EXPECT_TRUE(callbackCalled);
}

TEST_F(InteractionReporterTest, NotifyKey_TriggersCallback) {
    bool callbackCalled = false;
    mInteractionReporter.getInteractionProvider()->requestWakeupCallback(
            [&callbackCalled]() { callbackCalled = true; });

    mInteractionReporter.notifyKey(
            KeyArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).build());

    EXPECT_TRUE(callbackCalled);
}

TEST_F(InteractionReporterTest, NotifyMotion_TriggersCallbackExactlyOnce) {
    int callbackCallCount = 0;
    mInteractionReporter.getInteractionProvider()->requestWakeupCallback(
            [&callbackCallCount]() { callbackCallCount++; });

    mInteractionReporter.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN).build());

    mInteractionReporter.notifyMotion(
            MotionArgsBuilder(AMOTION_EVENT_ACTION_MOVE, AINPUT_SOURCE_TOUCHSCREEN).build());

    EXPECT_EQ(1, callbackCallCount);
}

TEST_F(InteractionReporterTest, NotifyKey_TriggersCallbackExactlyOnce) {
    int callbackCallCount = 0;
    mInteractionReporter.getInteractionProvider()->requestWakeupCallback(
            [&callbackCallCount]() { callbackCallCount++; });

    mInteractionReporter.notifyKey(
            KeyArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).build());

    mInteractionReporter.notifyKey(
            KeyArgsBuilder(AMOTION_EVENT_ACTION_DOWN, AINPUT_SOURCE_KEYBOARD).build());

    EXPECT_EQ(1, callbackCallCount);
}

TEST_F(InteractionReporterTest, NotifySwitch_ForwardsNotification) {
    NotifySwitchArgs args;

    mInteractionReporter.notifySwitch(args);

    mTestListener.assertNotifySwitchWasCalled();
}

TEST_F(InteractionReporterTest, NotifySensor_ForwardsNotification) {
    NotifySensorArgs args;

    mInteractionReporter.notifySensor(args);

    mTestListener.assertNotifySensorWasCalled();
}

TEST_F(InteractionReporterTest, NotifyVibratorState_ForwardsNotification) {
    NotifyVibratorStateArgs args;

    mInteractionReporter.notifyVibratorState(args);

    mTestListener.assertNotifyVibratorStateWasCalled();
}

TEST_F(InteractionReporterTest, NotifyDeviceReset_ForwardsNotification) {
    NotifyDeviceResetArgs args;

    mInteractionReporter.notifyDeviceReset(args);

    mTestListener.assertNotifyDeviceResetWasCalled();
}

TEST_F(InteractionReporterTest, NotifyPointerCaptureChanged_ForwardsNotification) {
    NotifyPointerCaptureChangedArgs args;

    mInteractionReporter.notifyPointerCaptureChanged(args);

    mTestListener.assertNotifyCaptureWasCalled();
}

} // namespace android
