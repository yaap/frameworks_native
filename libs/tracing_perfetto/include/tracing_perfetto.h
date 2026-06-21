/*
 * Copyright 2024 The Android Open Source Project
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

#include <stdint.h>
#include <stdio.h>

namespace tracing_perfetto {

void registerWithPerfetto(bool test = false);

bool isTagEnabled(uint64_t category);

void traceBegin(uint64_t category, const char* name);

void traceEnd(uint64_t category);

void traceAsyncBegin(uint64_t category, const char* name, int32_t cookie);

template <typename... Args>
inline void traceFormatBegin(uint64_t category, const char* fmt, Args... args) {
  if (isTagEnabled(category)) {
    if constexpr (sizeof...(Args) == 0) {
      traceBegin(category, fmt);
    } else {
      char buf[256];
      snprintf(buf, sizeof(buf), fmt, args...);
      traceBegin(category, buf);
    }
  }
}

void traceAsyncEnd(uint64_t category, const char* name, int32_t cookie);

void traceAsyncBeginForTrack(uint64_t category, const char* name,
                               const char* trackName, int32_t cookie);

void traceAsyncEndForTrack(uint64_t category, const char* trackName,
                             int32_t cookie);

void traceInstant(uint64_t category, const char* name);

template <typename... Args>
inline void traceFormatInstant(uint64_t category, const char* fmt,
                               Args... args) {
  if (isTagEnabled(category)) {
    if constexpr (sizeof...(Args) == 0) {
      traceInstant(category, fmt);
    } else {
      char buf[256];
      snprintf(buf, sizeof(buf), fmt, args...);
      traceInstant(category, buf);
    }
  }
}

void traceInstantForTrack(uint64_t category, const char* trackName,
                            const char* name);

void traceCounter(uint64_t category, const char* name, int64_t value);

void traceCounter32(uint64_t category, const char* name, int32_t value);

}  // namespace tracing_perfetto
