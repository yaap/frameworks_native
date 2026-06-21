/*
 * Copyright (C) 2019 The Android Open Source Project
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

#pragma once

#include <android-base/thread_annotations.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "InputListener.h"

using std::chrono_literals::operator""ms;

namespace android {

// --- TestInputListener ---

class TestInputListener : public InputListenerInterface {
public:
    TestInputListener(std::chrono::milliseconds eventHappenedTimeout = 0ms,
                      std::chrono::milliseconds eventDidNotHappenTimeout = 0ms);
    virtual ~TestInputListener();

    using TimePoint = std::chrono::time_point<std::chrono::system_clock>;

    void assertNotifyInputDevicesChangedWasCalled();

    void clearNotifyDeviceResetCalls();

    void assertNotifyDeviceResetWasCalled(const ::testing::Matcher<NotifyDeviceResetArgs>& matcher);

    // Deprecated; use the version with a Matcher instead.
    void assertNotifyDeviceResetWasCalled(NotifyDeviceResetArgs* outEventArgs = nullptr);

    void assertNotifyDeviceResetWasNotCalled();

    // Deprecated; use the version with a Matcher instead.
    void assertNotifyKeyWasCalled(NotifyKeyArgs* outEventArgs = nullptr);

    void assertNotifyKeyWasCalled(const ::testing::Matcher<NotifyKeyArgs>& matcher);

    void assertNotifyKeyWasNotCalled();

    // Deprecated; use the version with a Matcher instead.
    void assertNotifyMotionWasCalled(NotifyMotionArgs* outEventArgs = nullptr,
                                     std::optional<TimePoint> waitUntil = {});

    void assertNotifyMotionWasCalled(const ::testing::Matcher<NotifyMotionArgs>& matcher,
                                     std::optional<TimePoint> waitUntil = {});

    void assertNotifyMotionWasNotCalled(std::optional<TimePoint> waitUntil = {});

    void assertNotifySwitchWasCalled(
            const ::testing::Matcher<NotifySwitchArgs>& matcher = ::testing::_);

    void assertNotifyCaptureWasCalled(
            const ::testing::Matcher<NotifyPointerCaptureChangedArgs>& matcher = ::testing::_);
    void assertNotifyCaptureWasNotCalled();
    void assertNotifySensorWasCalled();
    void assertNotifyVibratorStateWasCalled();

private:
    template <class NotifyArgsType>
    void assertCalled(NotifyArgsType* outEventArgs, std::string message,
                      std::optional<TimePoint> waitUntil = {});

    template <class NotifyArgsType>
    void assertNotCalled(std::string message, std::optional<TimePoint> timeout = {});

    template <class NotifyArgsType>
    void addToQueue(const NotifyArgsType& args);

    virtual void notifyInputDevicesChanged(const NotifyInputDevicesChangedArgs& args) override;

    virtual void notifyDeviceReset(const NotifyDeviceResetArgs& args) override;

    virtual void notifyKey(const NotifyKeyArgs& args) override;

    virtual void notifyMotion(const NotifyMotionArgs& args) override;

    virtual void notifySwitch(const NotifySwitchArgs& args) override;

    virtual void notifySensor(const NotifySensorArgs& args) override;

    virtual void notifyVibratorState(const NotifyVibratorStateArgs& args) override;

    virtual void notifyPointerCaptureChanged(const NotifyPointerCaptureChangedArgs& args) override;

    std::mutex mLock;
    std::condition_variable mCondition;
    const std::chrono::milliseconds mEventHappenedTimeout;
    const std::chrono::milliseconds mEventDidNotHappenTimeout;

    std::tuple<std::vector<NotifyInputDevicesChangedArgs>,   //
               std::vector<NotifyDeviceResetArgs>,           //
               std::vector<NotifyKeyArgs>,                   //
               std::vector<NotifyMotionArgs>,                //
               std::vector<NotifySwitchArgs>,                //
               std::vector<NotifySensorArgs>,                //
               std::vector<NotifyVibratorStateArgs>,         //
               std::vector<NotifyPointerCaptureChangedArgs>> //
            mQueues GUARDED_BY(mLock);
};

} // namespace android
