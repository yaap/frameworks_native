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

#include <cstdlib>
#include <mutex>
#include <vector>

#include "test_framework/core/ScenarioEventRecorder.h"

namespace android::surfaceflinger::tests::end2end::test_framework::core {

void ScenarioEventRecorder::reset() {
    const std::lock_guard lock(mMutex);
    mEvents.clear();
}

[[nodiscard]] auto ScenarioEventRecorder::events() const -> std::vector<Event> {
    const std::lock_guard lock(mMutex);
    return mEvents;
}

void ScenarioEventRecorder::recordEvent(const Event& event) {
    const std::lock_guard lock(mMutex);
    mEvents.emplace_back(event);
}

}  // namespace android::surfaceflinger::tests::end2end::test_framework::core
