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

#include <optional>
#include <ostream>
#include <string_view>

#include <android/input.h>
#include <ftl/enum.h>
#include <input/InputEventLabels.h>
#include <sys/types.h>

namespace android {

// Type-safe wrapper for Android key codes, also known as AKEYCODE_* values.
enum class KeyCode : int32_t {
    UNKNOWN = AKEYCODE_UNKNOWN,
    BUTTON_A = AKEYCODE_BUTTON_A,
    BUTTON_B = AKEYCODE_BUTTON_B,
    BUTTON_L1 = AKEYCODE_BUTTON_L1,
};

inline std::ostream& operator<<(std::ostream& stream, const KeyCode& key) {
    auto rawKey = ftl::to_underlying(key);
    if (const std::optional<std::string_view> label = InputEventLookup::getLabelByKeyCode(rawKey);
        label.has_value()) {
        return stream << label.value();
    }
    return stream << rawKey;
}
} // namespace android
