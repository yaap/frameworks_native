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

#ifndef ANDROID_INTERNAL_PROTOLOG_TYPES_H
#define ANDROID_INTERNAL_PROTOLOG_TYPES_H

#include <cstdint>

namespace android {
namespace protolog {

enum class ProtoLogLevel : uint32_t {
    UNDEFINED = 0,
    VERBOSE = 1,
    DEBUG = 2,
    INFO = 3,
    WARN = 4,
    ERROR = 5,
    WTF = 6,
};

enum class TracingMode : uint32_t {
    DEFAULT = 0,
    ENABLE_ALL = 1,
};

} // namespace protolog
} // namespace android

#endif // ANDROID_INTERNAL_PROTOLOG_TYPES_H
