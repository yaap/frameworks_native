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

#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <android-base/thread_annotations.h>  // NOLINT(misc-include-cleaner
#include <utils/Mutex.h>

#include "test_framework/hwc3/events/BufferPendingDisplay.h"
#include "test_framework/hwc3/events/BufferPendingRelease.h"
#include "test_framework/hwc3/events/DisplayPresented.h"
#include "test_framework/surfaceflinger/events/BufferReleased.h"
#include "test_framework/surfaceflinger/events/TransactionCommitted.h"
#include "test_framework/surfaceflinger/events/TransactionCompleted.h"
#include "test_framework/surfaceflinger/events/TransactionInitiated.h"

namespace android::surfaceflinger::tests::end2end::test_framework::core {

// Monitors all the SF and HWC related events observed by the test framework as each
// test scenario executes, recording the events for later analysis.
class ScenarioEventRecorder final {
  public:
    using Event =
            std::variant<hwc3::events::DisplayPresented, hwc3::events::BufferPendingDisplay,
                         hwc3::events::BufferPendingRelease, surfaceflinger::events::BufferReleased,
                         surfaceflinger::events::TransactionInitiated,
                         surfaceflinger::events::TransactionCommitted,
                         surfaceflinger::events::TransactionCompleted>;

    // Resets the recorder to a clean state.
    void reset();

    // Obtains a copy of all the recorded events.
    [[nodiscard]] auto events() const -> std::vector<Event>;

    // Adds a single event to the record
    void recordEvent(const Event& event);

  private:
    mutable std::mutex mMutex;
    std::vector<Event> mEvents GUARDED_BY(mMutex);
};

[[nodiscard]] constexpr auto toString(const ScenarioEventRecorder::Event& event) -> std::string {
    return std::visit([](const auto& event) { return toString(event); }, event);
}

[[nodiscard]] constexpr auto toString(const std::optional<ScenarioEventRecorder::Event>& event)
        -> std::string {
    return event ? toString(*event) : "(none)";
}

}  // namespace android::surfaceflinger::tests::end2end::test_framework::core
