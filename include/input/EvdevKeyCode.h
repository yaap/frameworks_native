/*
 * Copyright 2026 The Android Open Source Project
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

#include <ftl/enum.h>
#include <linux/input-event-codes.h>
#include <sys/types.h>
#include <ostream>
#include <input/InputEventLabels.h>

namespace android {

// Type-safe wrapper for event codes of evdev event type EV_KEY, also known as KEY_*/BTN_* values.
enum class EvdevKeyCode : uint16_t {
    EAST = BTN_EAST,
    NORTH = BTN_NORTH,
    SOUTH = BTN_SOUTH,
    THUMBL = BTN_THUMBL,
    THUMBR = BTN_THUMBR,
    WEST = BTN_WEST,
};

inline std::ostream& operator<<(std::ostream& stream, const EvdevKeyCode& key) {
    return stream << InputEventLookup::getLinuxEvdevCodeLabel(EV_KEY, ftl::to_underlying(key));
}
} // namespace android
