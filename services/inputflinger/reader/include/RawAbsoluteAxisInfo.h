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

#pragma once

#include <cstdint>
#include <optional>
#include <ostream>

namespace android {

/* Describes an absolute axis. */
struct RawAbsoluteAxisInfo {
    int32_t minValue{};   // minimum value
    int32_t maxValue{};   // maximum value
    int32_t flat{};       // center flat position, eg. flat == 8 means center is between -8 and 8
    int32_t fuzz{};       // error tolerance, eg. fuzz == 4 means value is +/- 4 due to noise
    int32_t resolution{}; // resolution in units per mm or radians per mm
};

inline std::ostream& operator<<(std::ostream& out, const std::optional<RawAbsoluteAxisInfo>& info) {
    if (info) {
        out << "min=" << info->minValue << ", max=" << info->maxValue << ", flat=" << info->flat
            << ", fuzz=" << info->fuzz << ", resolution=" << info->resolution;
    } else {
        out << "unknown range";
    }
    return out;
}

} // namespace android
