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

#define LOG_TAG "BufferQueueHelpers"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <gui/BufferQueueHelpers.h>

#include <string_view>

namespace android {
namespace BufferQueueHelpers {

std::string_view presentModeToString(int32_t mode) {
    switch (mode) {
        case ANATIVEWINDOW_PRESENT_UNKNOWN:
            return "ANATIVEWINDOW_PRESENT_UNKNOWN";
        case ANATIVEWINDOW_PRESENT_DEFAULT:
            return "ANATIVEWINDOW_PRESENT_DEFAULT";
        case ANATIVEWINDOW_PRESENT_FIFO_LATEST_READY:
            return "ANATIVEWINDOW_PRESENT_FIFO_LATEST_READY";
        default:
            return "UNKNOWN_PRESENT_MODE";
    }
}

} // namespace BufferQueueHelpers
} // namespace android