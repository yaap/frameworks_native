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

#ifndef TRACE_CATEGORIES_H
#define TRACE_CATEGORIES_H

#include "perfetto/public/te_category_macros.h"

/**
 * Keep these in sync with frameworks/base/core/java/android/os/Trace.java.
 */
#define TRACE_CATEGORY_ALWAYS (1 << 0)
#define TRACE_CATEGORY_GRAPHICS (1 << 1)
#define TRACE_CATEGORY_INPUT (1 << 2)
#define TRACE_CATEGORY_VIEW (1 << 3)
#define TRACE_CATEGORY_WEBVIEW (1 << 4)
#define TRACE_CATEGORY_WINDOW_MANAGER (1 << 5)
#define TRACE_CATEGORY_ACTIVITY_MANAGER (1 << 6)
#define TRACE_CATEGORY_SYNC_MANAGER (1 << 7)
#define TRACE_CATEGORY_AUDIO (1 << 8)
#define TRACE_CATEGORY_VIDEO (1 << 9)
#define TRACE_CATEGORY_CAMERA (1 << 10)
#define TRACE_CATEGORY_HAL (1 << 11)
#define TRACE_CATEGORY_APP (1 << 12)
#define TRACE_CATEGORY_RESOURCES (1 << 13)
#define TRACE_CATEGORY_DALVIK (1 << 14)
#define TRACE_CATEGORY_RS (1 << 15)
#define TRACE_CATEGORY_BIONIC (1 << 16)
#define TRACE_CATEGORY_POWER (1 << 17)
#define TRACE_CATEGORY_PACKAGE_MANAGER (1 << 18)
#define TRACE_CATEGORY_SYSTEM_SERVER (1 << 19)
#define TRACE_CATEGORY_DATABASE (1 << 20)
#define TRACE_CATEGORY_NETWORK (1 << 21)
#define TRACE_CATEGORY_ADB (1 << 22)
#define TRACE_CATEGORY_VIBRATOR (1 << 23)
#define TRACE_CATEGORY_AIDL (1 << 24)
#define TRACE_CATEGORY_NNAPI (1 << 25)
#define TRACE_CATEGORY_RRO (1 << 26)
#define TRACE_CATEGORY_THERMAL (1 << 27)

// Should match the definitions in: frameworks/native/cmds/atrace/atrace.cpp
#define TRACK_EVENT_CATEGORIES(C)                             \
  C(bitmap, "bitmap", "Enables bitmap tracing in the system") \
  C(rendering, "rendering", "Enables rendering workload tracing in the system")

namespace tracing_perfetto {
namespace track_event_categories {

PERFETTO_TE_CATEGORIES_DECLARE(TRACK_EVENT_CATEGORIES);

}  // namespace track_event_categories
}  // namespace tracing_perfetto

#endif  // TRACE_CATEGORIES_H
