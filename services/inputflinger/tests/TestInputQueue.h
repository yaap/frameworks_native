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

#pragma once

#include <android-base/thread_annotations.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <source_location>

#include "NotifyArgs.h"
#include "TestEventMatchers.h"

namespace android {

// A thread-safe, blocking queue for holding events of type NotifyArgs.
class TestInputQueue {
public:
    // Add an event to the queue.
    void addEvent(const NotifyArgs& event);

    NotifyMotionArgs expectMotion(
            testing::Matcher<const NotifyMotionArgs&> matcher,
            const std::source_location location = std::source_location::current());
    /**
     * Consume events until the received event matches the provided matcher.
     * Note: this will consume all event types (and discard them), not just motions, until the
     * motion of interest is received.
     */
    NotifyMotionArgs consumeUntilAndExpectMotion(testing::Matcher<const NotifyMotionArgs&> matcher);
    NotifyKeyArgs expectKey(testing::Matcher<const NotifyKeyArgs&> matcher);
    NotifySensorArgs expectSensorEvent(testing::Matcher<const NotifySensorArgs&> matcher);
    NotifySwitchArgs expectSwitchEvent(testing::Matcher<const NotifySwitchArgs&> matcher);
    NotifyDeviceResetArgs expectDeviceReset(testing::Matcher<const NotifyDeviceResetArgs&> matcher);

    // Check that the queue is empty. This will fail if the queue is not empty.
    void assertNoEvents(const std::source_location location = std::source_location::current());

private:
    // Get the next event from the queue. This will block until an event is available.
    std::optional<NotifyArgs> expectEvent();

    template <typename T>
    T expectAndVerify(const char* expectedType, testing::Matcher<const T&> matcher,
                      const std::source_location location = std::source_location::current());

    std::queue<NotifyArgs> mEventQueue GUARDED_BY(mLock);
    std::mutex mLock;
    std::condition_variable mCondition;
};

} // namespace android
