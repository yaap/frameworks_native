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

#include "FakeInputTracingBackend.h"

#include <android-base/logging.h>
#include <android/input.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <utils/Errors.h>

#include "RawEvent.h"

namespace android::input_trace {

namespace {

// Use a larger timeout while waiting for events to be traced, compared to the timeout used while
// waiting to receive events through the input channel. Events are traced from a separate thread,
// which does not have the same high thread priority as the InputDispatcher's thread, so the tracer
// is expected to lag behind the Dispatcher at times.
constexpr auto TRACE_TIMEOUT = std::chrono::seconds(5);

base::ResultError<> error(const std::ostringstream& ss) {
    return base::ResultError(ss.str(), BAD_VALUE);
}

inline auto getId(const TracedEvent& v) {
    return std::visit([](const auto& event) { return event.id; }, v);
}

MotionEvent toInputEvent(const TracedMotionEvent& e, const WindowDispatchArgs& dispatchArgs,
                         const std::array<uint8_t, 32>& hmac) {
    MotionEvent traced;
    traced.initialize(e.id, e.deviceId, e.source, e.displayId, hmac, e.action, e.actionButton,
                      dispatchArgs.resolvedMotionFlags, AMOTION_EVENT_EDGE_FLAG_NONE, e.metaState,
                      e.buttonState, e.classification, dispatchArgs.transform, e.xPrecision,
                      e.yPrecision, e.xCursorPosition, e.yCursorPosition, dispatchArgs.rawTransform,
                      e.downTime, e.eventTime, e.pointerProperties.size(),
                      e.pointerProperties.data(), e.pointerCoords.data());
    return traced;
}

KeyEvent toInputEvent(const TracedKeyEvent& e, const WindowDispatchArgs& dispatchArgs,
                      const std::array<uint8_t, 32>& hmac) {
    KeyEvent traced;
    traced.initialize(e.id, e.deviceId, e.source, e.displayId, hmac, e.action, e.flags, e.keyCode,
                      e.scanCode, e.metaState, dispatchArgs.resolvedKeyRepeatCount, e.downTime,
                      e.eventTime);
    return traced;
}

} // namespace

// --- VerifyingTrace ---

void VerifyingTrace::expectKeyDispatchTraced(const KeyEvent& event, int32_t windowId) {
    std::scoped_lock lock(mLock);
    mExpectedEvents.emplace_back(event, windowId);
}

void VerifyingTrace::expectMotionDispatchTraced(const MotionEvent& event, int32_t windowId) {
    std::scoped_lock lock(mLock);
    mExpectedEvents.emplace_back(event, windowId);
}

void VerifyingTrace::expectAllRawEventsAtTimestampToMatchMetadata(
        nsecs_t when, ::testing::Matcher<TracedEventMetadata> metadataMatcher) {
    std::scoped_lock lock(mLock);
    mExpectedRawEvents.emplace_back(when, metadataMatcher);
}

void VerifyingTrace::verifyExpectedEventsTraced() {
    std::unique_lock lock(mLock);
    base::ScopedLockAssertion assumeLocked(mLock);

    // Poll for all expected events to be traced, and keep track of the latest poll result.
    base::Result<void> result;
    mEventTracedCondition.wait_for(lock, TRACE_TIMEOUT, [&]() REQUIRES(mLock) {
        for (const auto& [expectedEvent, windowId] : mExpectedEvents) {
            std::visit([&](const auto& event)
                               REQUIRES(mLock) { result = verifyEventTraced(event, windowId); },
                       expectedEvent);
            if (!result.ok()) {
                return false;
            }
        }

        for (const auto& [when, metadataMatcher] : mExpectedRawEvents) {
            result = verifyRawEventTraced(when, metadataMatcher);
            if (!result.ok()) {
                return false;
            }
        }
        return true;
    });

    EXPECT_TRUE(result.ok())
            << "Timed out waiting for all expected events to be traced successfully: "
            << result.error().message();
}

void VerifyingTrace::reset() {
    std::scoped_lock lock(mLock);
    mTracedEvents.clear();
    mTracedRawEvents.clear();
    mTracedWindowDispatches.clear();
    mExpectedEvents.clear();
}

template <typename Event>
base::Result<void> VerifyingTrace::verifyEventTraced(const Event& expectedEvent,
                                                     int32_t expectedWindowId) const {
    std::ostringstream msg;

    auto tracedEventsIt = mTracedEvents.find(expectedEvent.getId());
    if (tracedEventsIt == mTracedEvents.end()) {
        msg << "Expected event with ID 0x" << std::hex << expectedEvent.getId()
            << " to be traced, but it was not.\n"
            << "Expected event: " << expectedEvent;
        return error(msg);
    }

    auto tracedDispatchesIt =
            std::find_if(mTracedWindowDispatches.begin(), mTracedWindowDispatches.end(),
                         [&](const WindowDispatchArgs& args) {
                             return args.windowId == expectedWindowId &&
                                     getId(args.eventEntry) == expectedEvent.getId();
                         });
    if (tracedDispatchesIt == mTracedWindowDispatches.end()) {
        msg << "Expected dispatch of event with ID 0x" << std::hex << expectedEvent.getId()
            << " to window with ID 0x" << expectedWindowId << " to be traced, but it was not.\n"
            << "Expected event: " << expectedEvent;
        return error(msg);
    }

    // Verify that the traced event matches the expected event exactly.
    return std::visit(
            [&](const auto& traced) -> base::Result<void> {
                Event tracedEvent;
                using T = std::decay_t<decltype(traced)>;
                if constexpr (std::is_same_v<Event, MotionEvent> &&
                              std::is_same_v<T, TracedMotionEvent>) {
                    tracedEvent =
                            toInputEvent(traced, *tracedDispatchesIt, expectedEvent.getHmac());
                } else if constexpr (std::is_same_v<Event, KeyEvent> &&
                                     std::is_same_v<T, TracedKeyEvent>) {
                    tracedEvent =
                            toInputEvent(traced, *tracedDispatchesIt, expectedEvent.getHmac());
                } else {
                    msg << "Received the wrong event type!\n"
                        << "Expected event: " << expectedEvent;
                    return error(msg);
                }

                const auto result = testing::internal::CmpHelperEQ("expectedEvent", "tracedEvent",
                                                                   expectedEvent, tracedEvent);
                if (!result) {
                    msg << result.failure_message();
                    return error(msg);
                }
                return {};
            },
            tracedEventsIt->second);
}

base::Result<void> VerifyingTrace::verifyRawEventTraced(
        nsecs_t when, ::testing::Matcher<TracedEventMetadata> metadataMatcher) const {
    std::ostringstream msg;

    auto [startOfRange, endOfRange] = mTracedRawEvents.equal_range(when);
    if (startOfRange == mTracedRawEvents.end()) {
        msg << "Expected one or more raw events with timestamp " << when
            << " to be traced, but none were.";
        return error(msg);
    }

    ::testing::StringMatchResultListener resultListener;
    for (auto i = startOfRange; i != endOfRange; i++) {
        auto [rawEvent, metadata] = i->second;
        if (!::testing::ExplainMatchResult(metadataMatcher, metadata, &resultListener)) {
            msg << "Raw event with timestamp " << when
                << " didn't match metadata: " << resultListener.str();
            return error(msg);
        }
    }

    return {};
}

// --- FakeInputTracingBackend ---

void FakeInputTracingBackend::traceKeyEvent(const TracedKeyEvent& event,
                                            const TracedEventMetadata&) {
    {
        std::scoped_lock lock(mTrace->mLock);
        mTrace->mTracedEvents.emplace(event.id, event);
    }
    mTrace->mEventTracedCondition.notify_all();
}

void FakeInputTracingBackend::traceMotionEvent(const TracedMotionEvent& event,
                                               const TracedEventMetadata&) {
    {
        std::scoped_lock lock(mTrace->mLock);
        mTrace->mTracedEvents.emplace(event.id, event);
    }
    mTrace->mEventTracedCondition.notify_all();
}

void FakeInputTracingBackend::traceWindowDispatch(const WindowDispatchArgs& args,
                                                  const TracedEventMetadata&) {
    {
        std::scoped_lock lock(mTrace->mLock);
        mTrace->mTracedWindowDispatches.push_back(args);
    }
    mTrace->mEventTracedCondition.notify_all();
}

void FakeInputTracingBackend::traceRawEvent(const RawEvent& entry,
                                            const TracedEventMetadata& metadata) {
    {
        std::scoped_lock lock(mTrace->mLock);
        mTrace->mTracedRawEvents.insert({entry.when, {entry, metadata}});
    }
    mTrace->mEventTracedCondition.notify_all();
}

} // namespace android::input_trace
