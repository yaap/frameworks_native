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

// Type-safe wrapper for Android axis codes, also known as AMOTION_EVENT_AXIS_* values.
enum class MotionEventAxis : int32_t {
    UNKNOWN = -1,
    BRAKE = AMOTION_EVENT_AXIS_BRAKE,
    GAS = AMOTION_EVENT_AXIS_GAS,
    HAT_X = AMOTION_EVENT_AXIS_HAT_X,
    HAT_Y = AMOTION_EVENT_AXIS_HAT_Y,
    LTRIGGER = AMOTION_EVENT_AXIS_LTRIGGER,
    RTRIGGER = AMOTION_EVENT_AXIS_RTRIGGER,
    THROTTLE = AMOTION_EVENT_AXIS_THROTTLE,
    X = AMOTION_EVENT_AXIS_X,
    Y = AMOTION_EVENT_AXIS_Y,
    Z = AMOTION_EVENT_AXIS_Z,

    MAX = AMOTION_EVENT_MAXIMUM_VALID_AXIS_VALUE,
};

inline std::ostream& operator<<(std::ostream& stream, const MotionEventAxis& axis) {
    auto rawAxis = ftl::to_underlying(axis);
    if (const std::optional<std::string_view> label = InputEventLookup::getAxisLabel(rawAxis);
        label.has_value()) {
        return stream << label.value();
    }
    return stream << rawAxis;
}

} // namespace android
