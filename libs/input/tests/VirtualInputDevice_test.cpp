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

#include <android-base/unique_fd.h>
#include <gtest/gtest.h>
#include <input/Input.h>
#include <input/VirtualInputDevice.h>

namespace android {

// A helper to create a dummy fd that can be used for virtual input devices.
// Since we don't want to actually interact with /dev/uinput in a unit test,
// we just use a pipe or a temporary file.
base::unique_fd createDummyFd() {
    int fds[2];
    if (pipe(fds) != 0) {
        return base::unique_fd(-1);
    }
    // Close write end.
    close(fds[1]);
    return base::unique_fd(fds[0]);
}

TEST(VirtualTouchscreenTest, WriteTouchEvent_InvalidPointerId_DoesNotCrash) {
    base::unique_fd fd = createDummyFd();
    ASSERT_TRUE(fd.ok());

    VirtualTouchscreen touchscreen(std::move(fd));

    // Pointer ID -1 was previously allowed by validation but caused a crash in bitset::test.
    int32_t pointerId = -1;
    int32_t toolType = AMOTION_EVENT_TOOL_TYPE_FINGER;
    int32_t action = AMOTION_EVENT_ACTION_DOWN;
    float x = 100.0f;
    float y = 100.0f;
    float pressure = 1.0f;
    float majorAxisSize = 10.0f;
    std::chrono::nanoseconds eventTime(1000);

    // This should return false and not crash.
    bool result = touchscreen.writeTouchEvent(pointerId, toolType, action, x, y, pressure,
                                              majorAxisSize, eventTime);
    EXPECT_FALSE(result);
}

TEST(VirtualTouchscreenTest, WriteTouchEvent_ValidPointerId_DoesNotCrash) {
    base::unique_fd fd = createDummyFd();
    ASSERT_TRUE(fd.ok());

    VirtualTouchscreen touchscreen(std::move(fd));

    int32_t pointerId = 0;
    int32_t toolType = AMOTION_EVENT_TOOL_TYPE_FINGER;
    int32_t action = AMOTION_EVENT_ACTION_DOWN;
    float x = 100.0f;
    float y = 100.0f;
    float pressure = 1.0f;
    float majorAxisSize = 10.0f;
    std::chrono::nanoseconds eventTime(1000);

    // This might fail to write to the dummy fd but it should pass the validation.
    // In our case, write() to the read-end of a pipe might return an error but not crash.
    bool result = touchscreen.writeTouchEvent(pointerId, toolType, action, x, y, pressure,
                                              majorAxisSize, eventTime);
    EXPECT_FALSE(result);
}

} // namespace android
