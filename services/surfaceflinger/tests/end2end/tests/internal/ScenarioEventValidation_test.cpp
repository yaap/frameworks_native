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

#include <bit>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <variant>
#include <vector>

// #include <android-base/logging.h>
#include <gtest/gtest.h>

#include "test_framework/core/BufferId.h"
#include "test_framework/core/ScenarioEventRecorder.h"
#include "test_framework/core/ScenarioEventValidation.h"
#include "test_framework/hwc3/events/BufferPendingDisplay.h"
#include "test_framework/hwc3/events/BufferPendingRelease.h"
#include "test_framework/hwc3/events/DisplayPresented.h"
#include "test_framework/surfaceflinger/events/BufferReleased.h"
#include "test_framework/surfaceflinger/events/TransactionCommitted.h"
#include "test_framework/surfaceflinger/events/TransactionCompleted.h"
#include "test_framework/surfaceflinger/events/TransactionInitiated.h"

namespace android::surfaceflinger::tests::end2end {
namespace {

using namespace std::string_literals;

using TimePoint = std::chrono::steady_clock::time_point;

using Event =
        android::surfaceflinger::tests::end2end::test_framework::core::ScenarioEventRecorder::Event;

using HwcBufferPendingDisplay = test_framework::hwc3::events::BufferPendingDisplay;
using HwcBufferPendingRelease = test_framework::hwc3::events::BufferPendingRelease;
using HwcDisplayPresented = test_framework::hwc3::events::DisplayPresented;

using SfTransactionInitiated = test_framework::surfaceflinger::events::TransactionInitiated;
using SfTransactionCommitted = test_framework::surfaceflinger::events::TransactionCommitted;
using SfTransactionCompleted = test_framework::surfaceflinger::events::TransactionCompleted;
using SfBufferReleased = test_framework::surfaceflinger::events::BufferReleased;

using SfSurface = test_framework::surfaceflinger::Surface;
using BufferId = test_framework::core::BufferId;

// Helper to compute a monotonic clock time point from a integer value for the count of
// milliseconds since the epoch.
constexpr auto operator""_at_ms(unsigned long long value) -> TimePoint {
    return TimePoint{std::chrono::milliseconds(std::bit_cast<int64_t>(value))};
}

[[maybe_unused]] auto toString(const Event& event) -> std::string {
    return std::visit([](const auto& event) { return toString(event); }, event);
}

TEST(BasicValidationCheck, ChecksEvents) {
    using Events = std::initializer_list<Event>;

    constexpr auto kDisplay1 = uintptr_t{101L};
    constexpr auto kDisplay2 = uintptr_t{102L};
    constexpr auto kLayer1 = uintptr_t{201L};
    constexpr auto kLayer2 = uintptr_t{202L};
    constexpr auto kSurface1 = uintptr_t{301L};
    constexpr auto kSurface2 = uintptr_t{302L};
    constexpr auto kBuffer1 = BufferId{.inode = 1, .device = 99};
    constexpr auto kBuffer2 = BufferId{.inode = 2, .device = 99};
    constexpr auto kBuffer3 = BufferId{.inode = 3, .device = 99};
    constexpr auto kBuffer4 = BufferId{.inode = 4, .device = 99};

    struct TestCase {
        std::string testName;
        std::vector<Event> events;
        std::string expected;
    };

    // The implementation assumes that it is called with increasing input clock times.
    const auto testCases = std::initializer_list<TestCase>{
            {
                    .testName = "empty is valid",
                    .events = {},
            },
            {
                    // Simulates a pair of surfaces displayed on two 100Hz displays.
                    //
                    // New transactions are initiated soon after the last transaction commit
                    // notification for each surface.
                    .testName = "multi-surface valid interleaved",
                    .events =
                            Events{
                                    SfTransactionInitiated{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer1,
                                            .receivedAt = 0_at_ms,
                                    },
                                    SfTransactionInitiated{
                                            .surfaceId = kSurface2,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer2,
                                            .receivedAt = 1_at_ms,
                                    },
                                    HwcBufferPendingDisplay{
                                            .displayId = kDisplay1,
                                            .layerId = kLayer1,
                                            .bufferId = kBuffer1,
                                            .expectedPresentTime = 15_at_ms,
                                            .receivedAt = 12_at_ms,
                                    },
                                    HwcDisplayPresented{
                                            .displayId = kDisplay1,
                                            .expectedPresentTime = 15_at_ms,
                                            .receivedAt = 12_at_ms,
                                    },
                                    HwcBufferPendingDisplay{
                                            .displayId = kDisplay2,
                                            .layerId = kLayer2,
                                            .bufferId = kBuffer2,
                                            .expectedPresentTime = 15_at_ms,
                                            .receivedAt = 12_at_ms,
                                    },
                                    HwcDisplayPresented{
                                            .displayId = kDisplay2,
                                            .expectedPresentTime = 15_at_ms,
                                            .receivedAt = 12_at_ms,
                                    },
                                    SfTransactionCommitted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer1,
                                            .latchTime = 10_at_ms,
                                            .receivedAt = 12_at_ms,
                                    },
                                    SfTransactionCommitted{
                                            .surfaceId = kSurface2,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer2,
                                            .latchTime = 10_at_ms,
                                            .receivedAt = 12_at_ms,
                                    },
                                    SfTransactionInitiated{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer3,
                                            .receivedAt = 13_at_ms,
                                    },
                                    SfTransactionInitiated{
                                            .surfaceId = kSurface2,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer4,
                                            .receivedAt = 13_at_ms,
                                    },
                                    SfTransactionCompleted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer1,
                                            .latchTime = 10_at_ms,
                                            .receivedAt = 15_at_ms,
                                    },
                                    SfTransactionCompleted{
                                            .surfaceId = kSurface2,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer2,
                                            .latchTime = 10_at_ms,
                                            .receivedAt = 15_at_ms,
                                    },
                                    SfTransactionCommitted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer3,
                                            .latchTime = 20_at_ms,
                                            .receivedAt = 22_at_ms,
                                    },
                                    SfTransactionCommitted{
                                            .surfaceId = kSurface2,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer4,
                                            .latchTime = 20_at_ms,
                                            .receivedAt = 22_at_ms,
                                    },
                                    HwcBufferPendingDisplay{
                                            .displayId = kDisplay1,
                                            .layerId = kLayer1,
                                            .bufferId = kBuffer3,
                                            .expectedPresentTime = 25_at_ms,
                                            .receivedAt = 22_at_ms,
                                    },
                                    HwcBufferPendingRelease{
                                            .displayId = kDisplay1,
                                            .layerId = kLayer1,
                                            .bufferId = kBuffer1,
                                            .expectedPresentTime = 25_at_ms,
                                            .receivedAt = 22_at_ms,
                                    },
                                    HwcDisplayPresented{
                                            .displayId = kDisplay1,
                                            .expectedPresentTime = 25_at_ms,
                                            .receivedAt = 22_at_ms,
                                    },
                                    HwcBufferPendingDisplay{
                                            .displayId = kDisplay2,
                                            .layerId = kLayer2,
                                            .bufferId = kBuffer4,
                                            .expectedPresentTime = 25_at_ms,
                                            .receivedAt = 22_at_ms,
                                    },
                                    HwcBufferPendingRelease{
                                            .displayId = kDisplay2,
                                            .layerId = kLayer2,
                                            .bufferId = kBuffer2,
                                            .expectedPresentTime = 25_at_ms,
                                            .receivedAt = 22_at_ms,
                                    },
                                    HwcDisplayPresented{
                                            .displayId = kDisplay2,
                                            .expectedPresentTime = 25_at_ms,
                                            .receivedAt = 22_at_ms,
                                    },
                                    SfTransactionCompleted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer3,
                                            .latchTime = 20_at_ms,
                                            .receivedAt = 27_at_ms,
                                    },
                                    SfTransactionCompleted{
                                            .surfaceId = kSurface2,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer4,
                                            .latchTime = 20_at_ms,
                                            .receivedAt = 27_at_ms,
                                    },
                                    SfBufferReleased{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer1,
                                            .receivedAt = 27_at_ms,
                                    },
                                    SfBufferReleased{
                                            .surfaceId = kSurface2,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer2,
                                            .receivedAt = 27_at_ms,
                                    },

                                    // Simulate layer destruction
                                    HwcBufferPendingRelease{
                                            .displayId = kDisplay1,
                                            .layerId = kLayer1,
                                            .bufferId = kBuffer3,
                                            .expectedPresentTime = 35_at_ms,
                                            .receivedAt = 30_at_ms,
                                    },
                                    HwcDisplayPresented{
                                            .displayId = kDisplay1,
                                            .expectedPresentTime = 35_at_ms,
                                            .receivedAt = 30_at_ms,
                                    },
                                    HwcBufferPendingRelease{
                                            .displayId = kDisplay2,
                                            .layerId = kLayer2,
                                            .bufferId = kBuffer4,
                                            .expectedPresentTime = 35_at_ms,
                                            .receivedAt = 30_at_ms,
                                    },
                                    HwcDisplayPresented{
                                            .displayId = kDisplay2,
                                            .expectedPresentTime = 35_at_ms,
                                            .receivedAt = 30_at_ms,
                                    },
                                    SfBufferReleased{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer3,
                                            .receivedAt = 32_at_ms,
                                    },
                                    SfBufferReleased{
                                            .surfaceId = kSurface2,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer4,
                                            .receivedAt = 32_at_ms,
                                    },
                            },
            },
            {
                    // Simulates a single surface receiving continuous transactions to set a new
                    // buffer without any real wait.
                    //
                    // SurfaceFlinger will at that point process all transactions together for a
                    // commit, and will immediately release all but the last buffer, which be the
                    // only one displayed.
                    //
                    // The completions will also be signalled together after the display is
                    // presented, in case other state from those transactions ended up being used
                    // for the display state.
                    .testName = "single surface valid continuous",
                    .events =
                            Events{
                                    SfTransactionInitiated{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer1,
                                            .receivedAt = 0_at_ms,
                                    },
                                    SfTransactionInitiated{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer2,
                                            .receivedAt = 1_at_ms,
                                    },
                                    SfTransactionInitiated{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 3,
                                            .bufferId = kBuffer3,
                                            .receivedAt = 2_at_ms,
                                    },
                                    SfTransactionCommitted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer1,
                                            .latchTime = 10_at_ms,
                                            .receivedAt = 12_at_ms,
                                    },
                                    SfTransactionCommitted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer2,
                                            .latchTime = 10_at_ms,
                                            .receivedAt = 12_at_ms,
                                    },
                                    SfBufferReleased{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer1,
                                            .receivedAt = 12_at_ms,
                                    },
                                    SfTransactionCommitted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 3,
                                            .bufferId = kBuffer3,
                                            .latchTime = 10_at_ms,
                                            .receivedAt = 12_at_ms,
                                    },
                                    SfBufferReleased{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer2,
                                            .receivedAt = 12_at_ms,
                                    },
                                    HwcBufferPendingDisplay{
                                            .displayId = kDisplay1,
                                            .layerId = kLayer1,
                                            .bufferId = kBuffer3,
                                            .expectedPresentTime = 15_at_ms,
                                            .receivedAt = 12_at_ms,
                                    },
                                    HwcDisplayPresented{
                                            .displayId = kDisplay1,
                                            .expectedPresentTime = 15_at_ms,
                                            .receivedAt = 12_at_ms,
                                    },
                                    SfTransactionCompleted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer1,
                                            .latchTime = 10_at_ms,
                                            .receivedAt = 14_at_ms,
                                    },
                                    SfTransactionCompleted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer2,
                                            .latchTime = 10_at_ms,
                                            .receivedAt = 14_at_ms,
                                    },
                                    SfTransactionCompleted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 3,
                                            .bufferId = kBuffer3,
                                            .latchTime = 10_at_ms,
                                            .receivedAt = 14_at_ms,
                                    },

                                    // Simulate layer destruction
                                    HwcBufferPendingRelease{
                                            .displayId = kDisplay1,
                                            .layerId = kLayer1,
                                            .bufferId = kBuffer3,
                                            .expectedPresentTime = 25_at_ms,
                                            .receivedAt = 22_at_ms,
                                    },
                                    HwcDisplayPresented{
                                            .displayId = kDisplay2,
                                            .expectedPresentTime = 25_at_ms,
                                            .receivedAt = 22_at_ms,
                                    },
                                    SfBufferReleased{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 3,
                                            .bufferId = kBuffer3,
                                            .receivedAt = 23_at_ms,
                                    },
                            },
            },
            {
                    .testName = "double initiated",
                    .events =
                            Events{
                                    SfTransactionInitiated{},
                                    SfTransactionInitiated{},
                            },
                    .expected = "unexpected initiate, expected commit"s,
            },
            {
                    .testName = "initiated after committed",
                    .events =
                            Events{
                                    SfTransactionInitiated{},
                                    SfTransactionCommitted{},
                                    SfTransactionInitiated{},
                            },
                    .expected = "unexpected initiate, expected complete"s,
            },
            {
                    .testName = "committed without initiated",
                    .events = Events{SfTransactionCommitted{}},
                    .expected = "expected initiate"s,
            },
            {
                    .testName = "completed without committed",
                    .events = Events{SfTransactionInitiated{}, SfTransactionCompleted{}},
                    .expected = "expected commit"s,
            },
            {
                    .testName = "bufferId mismatch initiated to committed",
                    .events =
                            Events{
                                    SfTransactionInitiated{.bufferId = kBuffer1},
                                    SfTransactionCommitted{.bufferId = kBuffer2},
                                    SfTransactionCompleted{.bufferId = kBuffer2},
                            },
                    .expected = "bufferId mismatch"s,
            },
            {
                    .testName = "bufferId consistent for transaction lifecycle",
                    .events =
                            Events{
                                    SfTransactionInitiated{.bufferId = kBuffer1},
                                    SfTransactionCommitted{.bufferId = kBuffer1},
                                    SfTransactionCompleted{.bufferId = kBuffer2},
                            },
                    .expected = "bufferId mismatch"s,
            },
            {
                    .testName = "latchTime consistent for transaction commit to complete",
                    .events =
                            Events{
                                    SfTransactionInitiated{},
                                    SfTransactionCommitted{.latchTime = 200_at_ms},
                                    SfTransactionCompleted{.latchTime = 100_at_ms},
                            },
                    .expected = "latchTime mismatch"s,
            },
            {
                    .testName = "missing surfaceflinger buffer release single display",
                    .events =
                            Events{
                                    SfTransactionInitiated{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer1,
                                            .receivedAt = 0_at_ms,
                                    },
                                    SfTransactionCommitted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer1,
                                            .latchTime = 10_at_ms,
                                            .receivedAt = 12_at_ms,
                                    },
                                    SfTransactionInitiated{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer2,
                                            .receivedAt = 12_at_ms,
                                    },
                                    HwcBufferPendingDisplay{
                                            .displayId = kDisplay1,
                                            .layerId = kLayer1,
                                            .bufferId = kBuffer1,
                                            .expectedPresentTime = 15_at_ms,
                                            .receivedAt = 13_at_ms,
                                    },
                                    HwcDisplayPresented{
                                            .displayId = kDisplay1,
                                            .expectedPresentTime = 15_at_ms,
                                            .receivedAt = 13_at_ms,
                                    },
                                    SfTransactionCompleted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer1,
                                            .latchTime = 10_at_ms,
                                            .receivedAt = 14_at_ms,
                                    },
                                    SfTransactionCommitted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer2,
                                            .latchTime = 20_at_ms,
                                            .receivedAt = 22_at_ms,
                                    },
                                    SfTransactionInitiated{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 3,
                                            .bufferId = kBuffer3,
                                            .receivedAt = 22_at_ms,
                                    },
                                    HwcBufferPendingDisplay{
                                            .displayId = kDisplay1,
                                            .layerId = kLayer1,
                                            .bufferId = kBuffer2,
                                            .expectedPresentTime = 25_at_ms,
                                            .receivedAt = 23_at_ms,
                                    },
                                    HwcBufferPendingRelease{
                                            .displayId = kDisplay1,
                                            .layerId = kLayer1,
                                            .bufferId = kBuffer1,
                                            .expectedPresentTime = 25_at_ms,
                                            .receivedAt = 23_at_ms,
                                    },
                                    HwcDisplayPresented{
                                            .displayId = kDisplay1,
                                            .expectedPresentTime = 25_at_ms,
                                            .receivedAt = 23_at_ms,
                                    },
                                    SfTransactionCompleted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 2,
                                            .bufferId = kBuffer2,
                                            .latchTime = 20_at_ms,
                                            .receivedAt = 26_at_ms,
                                    },
                                    SfTransactionCommitted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 3,
                                            .bufferId = kBuffer3,
                                            .latchTime = 30_at_ms,
                                            .receivedAt = 32_at_ms,
                                    },
                            },
                    .expected = "missing sf buffer release"s,
            },
            {
                    .testName = "SurfaceFlinger buffer release too early",
                    .events =
                            Events{
                                    SfTransactionInitiated{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer1,
                                            .receivedAt = 0_at_ms,
                                    },
                                    SfTransactionCommitted{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer1,
                                            .latchTime = 10_at_ms,
                                            .receivedAt = 12_at_ms,
                                    },
                                    HwcBufferPendingDisplay{
                                            .displayId = kDisplay1,
                                            .layerId = kLayer1,
                                            .bufferId = kBuffer1,
                                            .expectedPresentTime = 15_at_ms,
                                            .receivedAt = 13_at_ms,
                                    },
                                    HwcDisplayPresented{
                                            .displayId = kDisplay1,
                                            .expectedPresentTime = 15_at_ms,
                                            .receivedAt = 13_at_ms,
                                    },
                                    SfBufferReleased{
                                            .surfaceId = kSurface1,
                                            .frameNumber = 1,
                                            .bufferId = kBuffer1,
                                            .receivedAt = 14_at_ms,
                                    },
                            },
                    .expected = "sf release while still displayed"s,
            },
    };

    for (const auto& testCase : testCases) {
        auto result = test_framework::core::BasicValidationCheck(testCase.events);
        const std::string actual = !result ? result.error() : ""s;

        if (!actual.empty() && testCase.expected.empty()) {
            ADD_FAILURE() << testCase.testName << ": Expected no error, but received \"" << actual
                          << "\".";
        } else if (!actual.empty() && !actual.contains(testCase.expected)) {
            ADD_FAILURE() << testCase.testName << ": Expected the error to contain \""
                          << testCase.expected << "\", but received \"" << actual << "\".";
        } else if (actual.empty() && !testCase.expected.empty()) {
            ADD_FAILURE() << testCase.testName << ": Expected the error to contain \""
                          << testCase.expected << "\" but there was no error.";
        }
    }
}

}  // namespace
}  // namespace android::surfaceflinger::tests::end2end
