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

#include "tracing_perfetto.h"

#include <cutils/trace.h>

#include "tracing_perfetto_internal.h"

namespace tracing_perfetto {

void registerWithPerfetto(bool test) {
  internal::registerWithPerfetto(test);
}

void traceBegin(uint64_t category, const char* name) {
  struct PerfettoTeCategory* perfettoTeCategory =
      internal::toPerfettoCategory(category);

  if (internal::shouldPreferAtrace(perfettoTeCategory, category)) {
    atrace_begin(category, name);
  } else if (internal::isPerfettoCategoryEnabled(perfettoTeCategory)) {
    internal::perfettoTraceBegin(*perfettoTeCategory, name);
  }
}

void traceEnd(uint64_t category) {
  struct PerfettoTeCategory* perfettoTeCategory =
      internal::toPerfettoCategory(category);

  if (internal::shouldPreferAtrace(perfettoTeCategory, category)) {
    atrace_end(category);
  } else if (internal::isPerfettoCategoryEnabled(perfettoTeCategory)) {
    internal::perfettoTraceEnd(*perfettoTeCategory);
  }
}

void traceAsyncBegin(uint64_t category, const char* name, int32_t cookie) {
  struct PerfettoTeCategory* perfettoTeCategory =
      internal::toPerfettoCategory(category);

  if (internal::shouldPreferAtrace(perfettoTeCategory, category)) {
    atrace_async_begin(category, name, cookie);
  } else if (internal::isPerfettoCategoryEnabled(perfettoTeCategory)) {
    internal::perfettoTraceAsyncBegin(*perfettoTeCategory, name, cookie);
  }
}

void traceAsyncEnd(uint64_t category, const char* name, int32_t cookie) {
  struct PerfettoTeCategory* perfettoTeCategory =
      internal::toPerfettoCategory(category);

  if (internal::shouldPreferAtrace(perfettoTeCategory, category)) {
    atrace_async_end(category, name, cookie);
  } else if (internal::isPerfettoCategoryEnabled(perfettoTeCategory)) {
    internal::perfettoTraceAsyncEnd(*perfettoTeCategory, name, cookie);
  }
}

void traceAsyncBeginForTrack(uint64_t category, const char* name,
                             const char* trackName, int32_t cookie) {
  struct PerfettoTeCategory* perfettoTeCategory =
      internal::toPerfettoCategory(category);

  if (internal::shouldPreferAtrace(perfettoTeCategory, category)) {
    atrace_async_for_track_begin(category, trackName, name, cookie);
  } else if (internal::isPerfettoCategoryEnabled(perfettoTeCategory)) {
    internal::perfettoTraceAsyncBeginForTrack(*perfettoTeCategory, name,
                                              trackName, cookie);
  }
}

void traceAsyncEndForTrack(uint64_t category, const char* trackName,
                           int32_t cookie) {
  struct PerfettoTeCategory* perfettoTeCategory =
      internal::toPerfettoCategory(category);

  if (internal::shouldPreferAtrace(perfettoTeCategory, category)) {
    atrace_async_for_track_end(category, trackName, cookie);
  } else if (internal::isPerfettoCategoryEnabled(perfettoTeCategory)) {
    internal::perfettoTraceAsyncEndForTrack(*perfettoTeCategory, trackName,
                                            cookie);
  }
}

void traceInstant(uint64_t category, const char* name) {
  struct PerfettoTeCategory* perfettoTeCategory =
      internal::toPerfettoCategory(category);

  if (internal::shouldPreferAtrace(perfettoTeCategory, category)) {
    atrace_instant(category, name);
  } else if (internal::isPerfettoCategoryEnabled(perfettoTeCategory)) {
    internal::perfettoTraceInstant(*perfettoTeCategory, name);
  }
}

void traceInstantForTrack(uint64_t category, const char* trackName,
                          const char* name) {
  struct PerfettoTeCategory* perfettoTeCategory =
      internal::toPerfettoCategory(category);

  if (internal::shouldPreferAtrace(perfettoTeCategory, category)) {
    atrace_instant_for_track(category, trackName, name);
  } else if (internal::isPerfettoCategoryEnabled(perfettoTeCategory)) {
    internal::perfettoTraceInstantForTrack(*perfettoTeCategory, trackName, name);
  }
}

void traceCounter(uint64_t category, const char* name, int64_t value) {
  struct PerfettoTeCategory* perfettoTeCategory =
      internal::toPerfettoCategory(category);

  if (internal::shouldPreferAtrace(perfettoTeCategory, category)) {
    atrace_int64(category, name, value);
  } else if (internal::isPerfettoCategoryEnabled(perfettoTeCategory)) {
    internal::perfettoTraceCounter(*perfettoTeCategory, name, value);
  }
}

void traceCounter32(uint64_t category, const char* name, int32_t value) {
  struct PerfettoTeCategory* perfettoTeCategory =
      internal::toPerfettoCategory(category);
  if (internal::shouldPreferAtrace(perfettoTeCategory, category)) {
    atrace_int(category, name, value);
  } else if (internal::isPerfettoCategoryEnabled(perfettoTeCategory)) {
    internal::perfettoTraceCounter(*perfettoTeCategory, name,
                                   static_cast<int64_t>(value));
  }
}

bool isTagEnabled(uint64_t category) {
  struct PerfettoTeCategory* perfettoTeCategory =
      internal::toPerfettoCategory(category);
  return internal::isPerfettoCategoryEnabled(perfettoTeCategory) ||
         atrace_is_tag_enabled(category);
}

}  // namespace tracing_perfetto
