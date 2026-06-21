/*
 * Copyright 2025 The Android Open Source Project
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

#include "TestInputQueue.h"
#include <source_location>

namespace android {

void TestInputQueue::addEvent(const NotifyArgs& event) {
    std::scoped_lock lock(mLock);
    mEventQueue.push(event);
    mCondition.notify_all();
}

std::optional<NotifyArgs> TestInputQueue::expectEvent() {
    std::unique_lock lock(mLock);
    if (!mCondition.wait_for(lock, std::chrono::seconds(3),
                             [this]() REQUIRES(mLock) { return !mEventQueue.empty(); })) {
        ADD_FAILURE() << "Timed out waiting for event.";
        return {};
    }
    base::ScopedLockAssertion assumeLock(mLock);
    NotifyArgs event = mEventQueue.front();
    mEventQueue.pop();
    return event;
}

template <typename T>
T TestInputQueue::expectAndVerify(const char* expectedType, testing::Matcher<const T&> matcher,
                                  const std::source_location location) {
    std::optional<NotifyArgs> args = expectEvent();
    if (!args) {
        ADD_FAILURE() << "Did not get an event, expected " << expectedType << " " << matcher;
        return {};
    }
    if (!std::holds_alternative<T>(*args)) {
        ADD_FAILURE() << "Expected " << expectedType << " " << matcher << ", but got " << *args;
        return {};
    }
    T eventArgs = std::get<T>(*args);
    EXPECT_THAT(eventArgs, matcher) << " at " << location.file_name() << ":" << location.line();
    return eventArgs;
}

NotifyMotionArgs TestInputQueue::expectMotion(testing::Matcher<const NotifyMotionArgs&> matcher,
                                              const std::source_location location) {
    return expectAndVerify<NotifyMotionArgs>("NotifyMotionArgs", matcher, location);
}

NotifyMotionArgs TestInputQueue::consumeUntilAndExpectMotion(
        testing::Matcher<const NotifyMotionArgs&> matcher) {
    while (true) {
        std::optional<NotifyArgs> args = expectEvent();
        if (!args) {
            ADD_FAILURE() << "Did not get an event, expected NotifyMotionArgs " << matcher;
            return {};
        }

        if (const auto* motionArgs = std::get_if<NotifyMotionArgs>(&*args)) {
            if (testing::Matches(matcher)(*motionArgs)) {
                return *motionArgs;
            }
        }
    }
}

NotifyKeyArgs TestInputQueue::expectKey(testing::Matcher<const NotifyKeyArgs&> matcher) {
    return expectAndVerify<NotifyKeyArgs>("NotifyKeyArgs", matcher);
}

NotifySensorArgs TestInputQueue::expectSensorEvent(
        testing::Matcher<const NotifySensorArgs&> matcher) {
    return expectAndVerify<NotifySensorArgs>("NotifySensorArgs", matcher);
}

NotifySwitchArgs TestInputQueue::expectSwitchEvent(
        testing::Matcher<const NotifySwitchArgs&> matcher) {
    return expectAndVerify<NotifySwitchArgs>("NotifySwitchArgs", matcher);
}

NotifyDeviceResetArgs TestInputQueue::expectDeviceReset(
        testing::Matcher<const NotifyDeviceResetArgs&> matcher) {
    return expectAndVerify<NotifyDeviceResetArgs>("NotifyDeviceResetArgs", matcher);
}

void TestInputQueue::assertNoEvents(const std::source_location location) {
    std::scoped_lock lock(mLock);
    EXPECT_THAT(mEventQueue, testing::IsEmpty())
            << "at " << location.file_name() << ":" << location.line() << "\n"
            << mEventQueue.front();
}

} // namespace android
