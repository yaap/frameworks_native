/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <chrono>
#include <vector>

#include <android-base/result-gmock.h>
#include <attestation/HmacKeyManager.h>
#include <com_android_input_flags.h>
#include <flag_macros.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <input/InputConsumer.h>
#include <input/InputTransport.h>
#include <input/ScopedFlagOverride.h>

using namespace std::chrono_literals;

using android::base::testing::Ok;

namespace android {

namespace input_flags = com::android::input::flags;

namespace {

struct Pointer {
    int32_t id;
    float x;
    float y;
    ToolType toolType = ToolType::FINGER;
    bool isResampled = false;
    std::map<int32_t /* axis */, float /* value */> axisValues = {};
};

struct InputEventEntry {
    std::chrono::nanoseconds eventTime;
    std::vector<Pointer> pointers;
    int32_t action;
    ui::LogicalDisplayId displayId = ui::LogicalDisplayId::DEFAULT;
};

} // namespace

class TouchResamplingTest : public testing::Test {
protected:
    std::unique_ptr<InputPublisher> mPublisher;
    std::unique_ptr<InputConsumer> mConsumer;
    PreallocatedInputEventFactory mEventFactory;

    uint32_t mSeq = 1;

    void SetUp() override {
        std::unique_ptr<InputChannel> serverChannel, clientChannel;
        status_t result =
                InputChannel::openInputChannelPair("channel name", serverChannel, clientChannel);
        ASSERT_EQ(OK, result);

        mPublisher = std::make_unique<InputPublisher>(std::move(serverChannel));
        mConsumer = std::make_unique<InputConsumer>(std::move(clientChannel),
                                                    /*enableTouchResampling=*/true);
    }

    status_t publishSimpleMotionEventWithCoords(int32_t action, nsecs_t eventTime,
                                                const std::vector<PointerProperties>& properties,
                                                const std::vector<PointerCoords>& coords,
                                                ui::LogicalDisplayId displayId);
    void publishSimpleMotionEvent(int32_t action, nsecs_t eventTime,
                                  const std::vector<Pointer>& pointers, ui::LogicalDisplayId);
    void publishInputEventEntries(const std::vector<InputEventEntry>& entries);
    void consumeInputEventEntries(const std::vector<InputEventEntry>& entries,
                                  std::chrono::nanoseconds frameTime, bool consumeBatches);
    void receiveResponseUntilSequence(uint32_t seq);
};

status_t TouchResamplingTest::publishSimpleMotionEventWithCoords(
        int32_t action, nsecs_t eventTime, const std::vector<PointerProperties>& properties,
        const std::vector<PointerCoords>& coords, ui::LogicalDisplayId displayId) {
    const ui::Transform identityTransform;
    const nsecs_t downTime = 0;

    if (action == AMOTION_EVENT_ACTION_DOWN && eventTime != 0) {
        ADD_FAILURE() << "Downtime should be equal to 0 (hardcoded for convenience)";
    }
    return mPublisher->publishMotionEvent(mSeq++, InputEvent::nextId(), /*deviceId=*/1,
                                          AINPUT_SOURCE_TOUCHSCREEN, displayId, INVALID_HMAC,
                                          action, /*actionButton=*/0, /*flags=*/0, AMETA_NONE,
                                          /*buttonState=*/0, MotionClassification::NONE,
                                          identityTransform, /*xPrecision=*/0, /*yPrecision=*/0,
                                          AMOTION_EVENT_INVALID_CURSOR_POSITION,
                                          AMOTION_EVENT_INVALID_CURSOR_POSITION, identityTransform,
                                          downTime, eventTime, properties.size(), properties.data(),
                                          coords.data());
}

void TouchResamplingTest::publishSimpleMotionEvent(int32_t action, nsecs_t eventTime,
                                                   const std::vector<Pointer>& pointers,
                                                   ui::LogicalDisplayId displayId) {
    std::vector<PointerProperties> properties;
    std::vector<PointerCoords> coords;

    for (const Pointer& pointer : pointers) {
        properties.push_back({});
        properties.back().clear();
        properties.back().id = pointer.id;
        properties.back().toolType = pointer.toolType;

        coords.push_back({});
        coords.back().clear();
        coords.back().setAxisValue(AMOTION_EVENT_AXIS_X, pointer.x);
        coords.back().setAxisValue(AMOTION_EVENT_AXIS_Y, pointer.y);
        for (const auto& [axis, value] : pointer.axisValues) {
            coords.back().setAxisValue(axis, value);
        }
    }

    status_t result =
            publishSimpleMotionEventWithCoords(action, eventTime, properties, coords, displayId);
    ASSERT_EQ(OK, result);
}

/**
 * Each entry is published separately, one entry at a time. As a result, action is used here
 * on a per-entry basis.
 */
void TouchResamplingTest::publishInputEventEntries(const std::vector<InputEventEntry>& entries) {
    for (const InputEventEntry& entry : entries) {
        publishSimpleMotionEvent(entry.action, entry.eventTime.count(), entry.pointers,
                                 entry.displayId);
    }
}

/**
 * Inside the publisher, read responses repeatedly until the desired sequence number is returned.
 *
 * Sometimes, when you call 'sendFinishedSignal', you would be finishing a batch which is comprised
 * of several input events. As a result, consumer will generate multiple 'finish' signals on your
 * behalf.
 *
 * In this function, we call 'receiveConsumerResponse' in a loop until the desired sequence number
 * is returned.
 */
void TouchResamplingTest::receiveResponseUntilSequence(uint32_t seq) {
    size_t consumedEvents = 0;
    while (consumedEvents < 100) {
        android::base::Result<InputPublisher::ConsumerResponse> response =
                mPublisher->receiveConsumerResponse();
        ASSERT_THAT(response, Ok());
        ASSERT_TRUE(std::holds_alternative<InputPublisher::Finished>(*response));
        const InputPublisher::Finished& finish = std::get<InputPublisher::Finished>(*response);
        ASSERT_TRUE(finish.handled)
                << "publisher receiveFinishedSignal should have set handled to consumer's reply";
        if (finish.seq == seq) {
            return;
        }
        consumedEvents++;
    }
    FAIL() << "Got " << consumedEvents << "events, but still no event with seq=" << seq;
}

/**
 * All entries are compared against a single MotionEvent, but the same data structure
 * InputEventEntry is used here for simpler code. As a result, the entire array of InputEventEntry
 * must contain identical values for the action field.
 */
void TouchResamplingTest::consumeInputEventEntries(const std::vector<InputEventEntry>& entries,
                                                   std::chrono::nanoseconds frameTime,
                                                   bool consumeBatches) {
    uint32_t consumeSeq;
    InputEvent* event;

    auto [result, unfinishedInputMessages] =
            mConsumer->consume(&mEventFactory, consumeBatches, frameTime.count(), &consumeSeq,
                               &event);

    if (!consumeBatches) {
        // When consumeBatches is false, mConsumer->consume only returns an event if a NEW message
        // is received from the channel *and* that message is either not batchable
        // (e.g., DOWN, UP) or causes a pending batch to be flushed.
        if (!result.ok() && result.error().code() == WOULD_BLOCK) {
            // This is the common case if no new message has arrived on the channel
            // since the last call. No event should have been produced.
            ASSERT_TRUE(entries.empty())
                    << "consume(consumeBatches=false) returned WOULD_BLOCK, indicating no new "
                    << "channel message was processed. Expected entries should be empty.";
            return;
        }

        // If not WOULD_BLOCK, an event must have been consumed. This implies a new message
        // arrived and was processed, resulting in an event.
        ASSERT_THAT(result, Ok()) << "consume(consumeBatches=false) failed unexpectedly: "
                                  << result.error().message();
        ASSERT_FALSE(entries.empty()) << "consume(consumeBatches=false) returned an event, but the "
                                         "expected entries list is empty.";
    } else { // consumeBatches == true
        // When consumeBatches is true, mConsumer->consume will always try to produce an event
        // from any buffered batches if the channel has no new messages (WOULD_BLOCK).
        // An error result here would likely indicate a more serious issue like NO_MEMORY.
        ASSERT_THAT(result, Ok()) << "consume(consumeBatches=true) failed unexpectedly: "
                                  << result.error().message();

        if (event == nullptr) {
            // This means consumeBatch was called but determined no batched events were ready
            // to be dispatched for the given frameTime.
            ASSERT_TRUE(entries.empty())
                    << "consume(consumeBatches=true) returned OK but no event (event == nullptr), "
                    << "indicating no batch was ready. Expected entries should be empty.";
            return;
        }
        // An event was successfully consumed from a batch.
        ASSERT_FALSE(entries.empty()) << "consume(consumeBatches=true) returned an event, but the "
                                         "expected entries list is empty.";
    }

    ASSERT_TRUE(event != nullptr) << "Result is OK, but no event was returned.";
    MotionEvent* motionEvent = static_cast<MotionEvent*>(event);

    ASSERT_EQ(entries.size() - 1, motionEvent->getHistorySize());
    for (size_t i = 0; i < entries.size(); i++) { // most recent sample is last
        SCOPED_TRACE(i);
        const InputEventEntry& entry = entries[i];
        ASSERT_EQ(entry.action, motionEvent->getAction());
        ASSERT_EQ(entry.eventTime.count(), motionEvent->getHistoricalEventTime(i));
        ASSERT_EQ(entry.pointers.size(), motionEvent->getPointerCount());
        ASSERT_EQ(entry.displayId, motionEvent->getDisplayId());

        for (size_t p = 0; p < motionEvent->getPointerCount(); p++) {
            SCOPED_TRACE(p);
            // The pointers can be in any order, both in MotionEvent as well as InputEventEntry
            ssize_t motionEventPointerIndex = motionEvent->findPointerIndex(entry.pointers[p].id);
            ASSERT_GE(motionEventPointerIndex, 0) << "Pointer must be present in MotionEvent";
            ASSERT_EQ(entry.pointers[p].x,
                      motionEvent->getHistoricalAxisValue(AMOTION_EVENT_AXIS_X,
                                                          motionEventPointerIndex, i));
            ASSERT_EQ(entry.pointers[p].x,
                      motionEvent->getHistoricalRawAxisValue(AMOTION_EVENT_AXIS_X,
                                                             motionEventPointerIndex, i));
            ASSERT_EQ(entry.pointers[p].y,
                      motionEvent->getHistoricalAxisValue(AMOTION_EVENT_AXIS_Y,
                                                          motionEventPointerIndex, i));
            ASSERT_EQ(entry.pointers[p].y,
                      motionEvent->getHistoricalRawAxisValue(AMOTION_EVENT_AXIS_Y,
                                                             motionEventPointerIndex, i));
            ASSERT_EQ(entry.pointers[p].isResampled,
                      motionEvent->isResampled(motionEventPointerIndex, i));
            for (const auto& [axis, value] : entry.pointers[p].axisValues) {
                ASSERT_EQ(value,
                          motionEvent->getHistoricalAxisValue(axis, motionEventPointerIndex, i));
            }
        }
    }

    status_t status = mConsumer->sendFinishedSignal(consumeSeq, true);
    ASSERT_EQ(OK, status);

    receiveResponseUntilSequence(consumeSeq);
}

/**
 * Timeline
 * ---------+------------------+------------------+--------+-----------------+----------------------
 *          0 ms               10 ms              20 ms    25 ms            35 ms
 *          ACTION_DOWN       ACTION_MOVE      ACTION_MOVE  ^                ^
 *                                                          |                |
 *                                                         resampled value   |
 *                                                                          frameTime
 * Typically, the prediction is made for time frameTime - RESAMPLE_LATENCY, or 30 ms in this case
 * However, that would be 10 ms later than the last real sample (which came in at 20 ms).
 * Therefore, the resampling should happen at 20 ms + RESAMPLE_MAX_PREDICTION = 28 ms.
 * In this situation, though, resample time is further limited by taking half of the difference
 * between the last two real events, which would put this time at:
 * 20 ms + (20 ms - 10 ms) / 2 = 25 ms.
 */
TEST_F(TouchResamplingTest, EventIsResampled) {
    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // Initial ACTION_DOWN should be separate, because the first consume event will only return
    // InputEvent with a single action.
    entries = {
            //      id  x   y
            {0ms, {{0, 10, 20}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    expectedEntries = {
            //      id  x   y
            {0ms, {{0, 10, 20}}, AMOTION_EVENT_ACTION_DOWN},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Two ACTION_MOVE events 10 ms apart that move in X direction and stay still in Y
    entries = {
            //      id  x   y
            {10ms, {{0, 20, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 35ms;
    expectedEntries = {
            //      id  x   y
            {10ms, {{0, 20, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {25ms, {{0, 35, 30, .isResampled = true}}, AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
}

/**
 * Same as above test, but use pointer id=1 instead of 0 to make sure that system does not
 * have these hardcoded.
 */
TEST_F(TouchResamplingTest, EventIsResampledWithDifferentId) {
    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // Initial ACTION_DOWN should be separate, because the first consume event will only return
    // InputEvent with a single action.
    entries = {
            //      id  x   y
            {0ms, {{1, 10, 20}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    expectedEntries = {
            //      id  x   y
            {0ms, {{1, 10, 20}}, AMOTION_EVENT_ACTION_DOWN},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Two ACTION_MOVE events 10 ms apart that move in X direction and stay still in Y
    entries = {
            //      id  x   y
            {10ms, {{1, 20, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{1, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 35ms;
    expectedEntries = {
            //      id  x   y
            {10ms, {{1, 20, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{1, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {25ms, {{1, 35, 30, .isResampled = true}}, AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
}

/**
 * Stylus pointer coordinates are resampled.
 */
TEST_F(TouchResamplingTest, StylusEventIsResampled) {
    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // Initial ACTION_DOWN should be separate, because the first consume event will only return
    // InputEvent with a single action.
    entries = {
            //      id  x   y
            {0ms, {{0, 10, 20, .toolType = ToolType::STYLUS}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    expectedEntries = {
            //      id  x   y
            {0ms, {{0, 10, 20, .toolType = ToolType::STYLUS}}, AMOTION_EVENT_ACTION_DOWN},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Two ACTION_MOVE events 10 ms apart that move in X direction and stay still in Y
    entries = {
            //      id  x   y
            {10ms, {{0, 20, 30, .toolType = ToolType::STYLUS}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30, .toolType = ToolType::STYLUS}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 35ms;
    expectedEntries = {
            //      id  x   y
            {10ms, {{0, 20, 30, .toolType = ToolType::STYLUS}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30, .toolType = ToolType::STYLUS}}, AMOTION_EVENT_ACTION_MOVE},
            {25ms,
             {{0, 35, 30, .toolType = ToolType::STYLUS, .isResampled = true}},
             AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
}

/**
 * Mouse pointer coordinates are resampled.
 */
TEST_F(TouchResamplingTest, MouseEventIsResampled) {
    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // Initial ACTION_DOWN should be separate, because the first consume event will only return
    // InputEvent with a single action.
    entries = {
            //      id  x   y
            {0ms, {{0, 10, 20, .toolType = ToolType::MOUSE}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    expectedEntries = {
            //      id  x   y
            {0ms, {{0, 10, 20, .toolType = ToolType::MOUSE}}, AMOTION_EVENT_ACTION_DOWN},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Two ACTION_MOVE events 10 ms apart that move in X direction and stay still in Y
    entries = {
            //      id  x   y
            {10ms, {{0, 20, 30, .toolType = ToolType::MOUSE}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30, .toolType = ToolType::MOUSE}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 35ms;
    expectedEntries = {
            //      id  x   y
            {10ms, {{0, 20, 30, .toolType = ToolType::MOUSE}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30, .toolType = ToolType::MOUSE}}, AMOTION_EVENT_ACTION_MOVE},
            {25ms,
             {{0, 35, 30, .toolType = ToolType::MOUSE, .isResampled = true}},
             AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
}

/**
 * Mouse pointer coordinates are resampled.
 */
TEST_F(TouchResamplingTest, MouseEventIsResampledClearingRelativeAxes) {
    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // Initial ACTION_DOWN should be separate, because the first consume event will only return
    // InputEvent with a single action.
    entries = {
            //      id  x   y
            {0ms, {{0, 10, 20, .toolType = ToolType::MOUSE}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    expectedEntries = {
            //      id  x   y
            {0ms, {{0, 10, 20, .toolType = ToolType::MOUSE}}, AMOTION_EVENT_ACTION_DOWN},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Two ACTION_MOVE events 10 ms apart that move in X direction and stay still in Y.
    // In the resampled event, RELATIVE_X and RELATIVE_Y are cleared to zero.
    entries = {
            // id  x   y
            {10ms,
             {{0, 20, 30, .toolType = ToolType::MOUSE,
               .axisValues = {{AMOTION_EVENT_AXIS_RELATIVE_X, 10},
                              {AMOTION_EVENT_AXIS_RELATIVE_Y, 10}}}},
             AMOTION_EVENT_ACTION_MOVE},
            {20ms,
             {{0, 30, 30, .toolType = ToolType::MOUSE,
               .axisValues = {{AMOTION_EVENT_AXIS_RELATIVE_X, 10},
                              {AMOTION_EVENT_AXIS_RELATIVE_Y, 0}}}},
             AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 35ms;
    expectedEntries = {
            // id  x   y
            {10ms,
             {{0, 20, 30, .toolType = ToolType::MOUSE,
               .axisValues = {{AMOTION_EVENT_AXIS_RELATIVE_X, 10},
                              {AMOTION_EVENT_AXIS_RELATIVE_Y, 10}}}},
             AMOTION_EVENT_ACTION_MOVE},
            {20ms,
             {{0, 30, 30, .toolType = ToolType::MOUSE,
               .axisValues = {{AMOTION_EVENT_AXIS_RELATIVE_X, 10},
                              {AMOTION_EVENT_AXIS_RELATIVE_Y, 0}}}},
             AMOTION_EVENT_ACTION_MOVE},
            {25ms,
             {{0, 35, 30, .toolType = ToolType::MOUSE, .isResampled = true,
               .axisValues = {{AMOTION_EVENT_AXIS_RELATIVE_X, 0},
                              {AMOTION_EVENT_AXIS_RELATIVE_Y, 0}}}},
             AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
}

/**
 * Motion events with palm tool type are not resampled.
 */
TEST_F(TouchResamplingTest, PalmEventIsNotResampled) {
    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // Initial ACTION_DOWN should be separate, because the first consume event will only return
    // InputEvent with a single action.
    entries = {
            //      id  x   y
            {0ms, {{0, 10, 20, .toolType = ToolType::PALM}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    expectedEntries = {
            //      id  x   y
            {0ms, {{0, 10, 20, .toolType = ToolType::PALM}}, AMOTION_EVENT_ACTION_DOWN},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Two ACTION_MOVE events 10 ms apart that move in X direction and stay still in Y
    entries = {
            //      id  x   y
            {10ms, {{0, 20, 30, .toolType = ToolType::PALM}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30, .toolType = ToolType::PALM}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 35ms;
    expectedEntries = {
            //      id  x   y
            {10ms, {{0, 20, 30, .toolType = ToolType::PALM}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30, .toolType = ToolType::PALM}}, AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
}

/**
 * Event should not be resampled when sample time is equal to event time.
 */
TEST_F(TouchResamplingTest, SampleTimeEqualsEventTime) {
    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // Initial ACTION_DOWN should be separate, because the first consume event will only return
    // InputEvent with a single action.
    entries = {
            //      id  x   y
            {0ms, {{0, 10, 20}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    expectedEntries = {
            //      id  x   y
            {0ms, {{0, 10, 20}}, AMOTION_EVENT_ACTION_DOWN},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Two ACTION_MOVE events 10 ms apart that move in X direction and stay still in Y
    entries = {
            //      id  x   y
            {10ms, {{0, 20, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 20ms + 5ms /*RESAMPLE_LATENCY*/;
    expectedEntries = {
            //      id  x   y
            {10ms, {{0, 20, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
            // no resampled event because the time of resample falls exactly on the existing event
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
}

/**
 * Once we send a resampled value to the app, we should continue to "lie" if the pointer
 * does not move. So, if the pointer keeps the same coordinates, resampled value should continue
 * to be used.
 */
TEST_F(TouchResamplingTest, ResampledValueIsUsedForIdenticalCoordinates) {
    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // Initial ACTION_DOWN should be separate, because the first consume event will only return
    // InputEvent with a single action.
    entries = {
            //      id  x   y
            {0ms, {{0, 10, 20}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    expectedEntries = {
            //      id  x   y
            {0ms, {{0, 10, 20}}, AMOTION_EVENT_ACTION_DOWN},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Two ACTION_MOVE events 10 ms apart that move in X direction and stay still in Y
    entries = {
            //      id  x   y
            {10ms, {{0, 20, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 35ms;
    expectedEntries = {
            //      id  x   y
            {10ms, {{0, 20, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {25ms, {{0, 35, 30, .isResampled = true}}, AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Coordinate value 30 has been resampled to 35. When a new event comes in with value 30 again,
    // the system should still report 35.
    entries = {
            //      id  x   y
            {40ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 45ms + 5ms /*RESAMPLE_LATENCY*/;
    expectedEntries = {
            //      id  x   y
            {40ms,
             {{0, 35, 30, .isResampled = true}},
             AMOTION_EVENT_ACTION_MOVE}, // original event, rewritten
            {45ms,
             {{0, 35, 30, .isResampled = true}},
             AMOTION_EVENT_ACTION_MOVE}, // resampled event, rewritten
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
}

TEST_F(TouchResamplingTest, OldEventReceivedAfterResampleOccurs) {
    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // Initial ACTION_DOWN should be separate, because the first consume event will only return
    // InputEvent with a single action.
    entries = {
            //      id  x   y
            {0ms, {{0, 10, 20}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    expectedEntries = {
            //      id  x   y
            {0ms, {{0, 10, 20}}, AMOTION_EVENT_ACTION_DOWN},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Two ACTION_MOVE events 10 ms apart that move in X direction and stay still in Y
    entries = {
            //      id  x   y
            {10ms, {{0, 20, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 35ms;
    expectedEntries = {
            //      id  x   y
            {10ms, {{0, 20, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {25ms, {{0, 35, 30, .isResampled = true}}, AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
    // Above, the resampled event is at 25ms rather than at 30 ms = 35ms - RESAMPLE_LATENCY
    // because we are further bound by how far we can extrapolate by the "last time delta".
    // That's 50% of (20 ms - 10ms) => 5ms. So we can't predict more than 5 ms into the future
    // from the event at 20ms, which is why the resampled event is at t = 25 ms.

    // We resampled the event to 25 ms. Now, an older 'real' event comes in.
    // Resampled event is synthesized at 24 + (24 - 20) / 2 = 26 ms.
    entries = {
            //      id  x   y
            {24ms, {{0, 40, 30}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 50ms;
    expectedEntries = {
            //      id  x   y
            {24ms,
             {{0, 35, 30, .isResampled = true}},
             AMOTION_EVENT_ACTION_MOVE}, // original event, rewritten
            {26ms,
             {{0, 45, 30, .isResampled = true}},
             AMOTION_EVENT_ACTION_MOVE}, // resampled event, rewritten
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
}

// Similar to OldEventReceivedAfterResampleOccurs, but axis values are set and verified.
TEST_F(TouchResamplingTest, OldEventReceivedAfterResampleOccursVerifyAxes) {
    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // Initial ACTION_DOWN should be separate, because the first consume event will only return
    // InputEvent with a single action.
    entries = {
            //      id  x   y
            {0ms, {{0, 10, 20}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    expectedEntries = {
            //      id  x   y
            {0ms, {{0, 10, 20}}, AMOTION_EVENT_ACTION_DOWN},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Two ACTION_MOVE events 10 ms apart that move in X direction and stay still in Y
    entries = {
            // id  x   y
            {10ms,
             {{0, 20, 30,
               .axisValues = {{AMOTION_EVENT_AXIS_GESTURE_SCROLL_X_DISTANCE, 10},
                              {AMOTION_EVENT_AXIS_GESTURE_SCROLL_Y_DISTANCE, 10}}}},
             AMOTION_EVENT_ACTION_MOVE},
            {20ms,
             {{0, 30, 30,
               .axisValues = {{AMOTION_EVENT_AXIS_GESTURE_SCROLL_X_DISTANCE, 10},
                              {AMOTION_EVENT_AXIS_GESTURE_SCROLL_Y_DISTANCE, 0}}}},
             AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 35ms;
    expectedEntries = {
            // id  x   y
            {10ms,
             {{0, 20, 30,
               .axisValues = {{AMOTION_EVENT_AXIS_GESTURE_SCROLL_X_DISTANCE, 10},
                              {AMOTION_EVENT_AXIS_GESTURE_SCROLL_Y_DISTANCE, 10}}}},
             AMOTION_EVENT_ACTION_MOVE},
            {20ms,
             {{0, 30, 30,
               .axisValues = {{AMOTION_EVENT_AXIS_GESTURE_SCROLL_X_DISTANCE, 10},
                              {AMOTION_EVENT_AXIS_GESTURE_SCROLL_Y_DISTANCE, 0}}}},
             AMOTION_EVENT_ACTION_MOVE},
            {25ms,
             {{0, 35, 30, .isResampled = true,
               .axisValues = {{AMOTION_EVENT_AXIS_GESTURE_SCROLL_X_DISTANCE, 0},
                              {AMOTION_EVENT_AXIS_GESTURE_SCROLL_Y_DISTANCE, 0}}}},
             AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
    // Above, the resampled event is at 25ms rather than at 30 ms = 35ms - RESAMPLE_LATENCY
    // because we are further bound by how far we can extrapolate by the "last time delta".
    // That's 50% of (20 ms - 10ms) => 5ms. So we can't predict more than 5 ms into the future
    // from the event at 20ms, which is why the resampled event is at t = 25 ms.

    // We resampled the event to 25 ms. Now, an older 'real' event comes in.
    // Resampled event is synthesized at 24 + (24 - 20) / 2 = 26 ms.
    entries = {
            // id  x   y
            {24ms,
             {{0, 40, 30,
               .axisValues = {{AMOTION_EVENT_AXIS_GESTURE_SCROLL_X_DISTANCE, 10},
                              {AMOTION_EVENT_AXIS_GESTURE_SCROLL_Y_DISTANCE, 0}}}},
             AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 50ms;
    expectedEntries = {
            // id  x   y
            {24ms,
             {{0, 35, 30, .isResampled = true,
               .axisValues = {{AMOTION_EVENT_AXIS_GESTURE_SCROLL_X_DISTANCE, 10},
                              {AMOTION_EVENT_AXIS_GESTURE_SCROLL_Y_DISTANCE, 0}}}},
             AMOTION_EVENT_ACTION_MOVE}, // axis values are kept
            {26ms,
             {{0, 45, 30, .isResampled = true,
               .axisValues = {{AMOTION_EVENT_AXIS_GESTURE_SCROLL_X_DISTANCE, 0},
                              {AMOTION_EVENT_AXIS_GESTURE_SCROLL_Y_DISTANCE, 0}}}},
             AMOTION_EVENT_ACTION_MOVE}, // axis values are cleared
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
}

TEST_F(TouchResamplingTest, TwoPointersAreResampledIndependently) {
    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // full action for when a pointer with index=1 appears (some other pointer must already be
    // present)
    constexpr int32_t actionPointer1Down =
            AMOTION_EVENT_ACTION_POINTER_DOWN + (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);

    // full action for when a pointer with index=0 disappears (some other pointer must still remain)
    constexpr int32_t actionPointer0Up =
            AMOTION_EVENT_ACTION_POINTER_UP + (0 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);

    // Initial ACTION_DOWN should be separate, because the first consume event will only return
    // InputEvent with a single action.
    entries = {
            //      id  x   y
            {0ms, {{0, 100, 100}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    expectedEntries = {
            //      id  x   y
            {0ms, {{0, 100, 100}}, AMOTION_EVENT_ACTION_DOWN},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    entries = {
            //       id  x   y
            {10ms, {{0, 100, 100}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 10ms + 5ms /*RESAMPLE_LATENCY*/;
    expectedEntries = {
            //       id  x   y
            {10ms, {{0, 100, 100}}, AMOTION_EVENT_ACTION_MOVE},
            // no resampled value because frameTime - RESAMPLE_LATENCY == eventTime
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Second pointer id=1 appears
    entries = {
            //      id  x    y
            {15ms, {{0, 100, 100}, {1, 500, 500}}, actionPointer1Down},
    };
    publishInputEventEntries(entries);
    frameTime = 20ms + 5ms /*RESAMPLE_LATENCY*/;
    expectedEntries = {
            //      id  x    y
            {15ms, {{0, 100, 100}, {1, 500, 500}}, actionPointer1Down},
            // no resampled value because frameTime - RESAMPLE_LATENCY == eventTime
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Both pointers move
    entries = {
            //      id  x    y
            {30ms, {{0, 100, 100}, {1, 500, 500}}, AMOTION_EVENT_ACTION_MOVE},
            {40ms, {{0, 120, 120}, {1, 600, 600}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 45ms + 5ms /*RESAMPLE_LATENCY*/;
    expectedEntries = {
            //      id  x    y
            {30ms, {{0, 100, 100}, {1, 500, 500}}, AMOTION_EVENT_ACTION_MOVE},
            {40ms, {{0, 120, 120}, {1, 600, 600}}, AMOTION_EVENT_ACTION_MOVE},
            {45ms,
             {{0, 130, 130, .isResampled = true}, {1, 650, 650, .isResampled = true}},
             AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Both pointers move again
    entries = {
            //      id  x    y
            {60ms, {{0, 120, 120}, {1, 600, 600}}, AMOTION_EVENT_ACTION_MOVE},
            {70ms, {{0, 130, 130}, {1, 700, 700}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 75ms + 5ms /*RESAMPLE_LATENCY*/;
    /**
     * The sample at t = 60, pointer id 0 is not equal to 120, because this value of 120 was
     * received twice, and resampled to 130. So if we already reported it as "130", we continue
     * to report it as such. Similar with pointer id 1.
     */
    expectedEntries = {
            {60ms,
             {{0, 130, 130, .isResampled = true}, // not 120! because it matches previous real event
              {1, 650, 650, .isResampled = true}},
             AMOTION_EVENT_ACTION_MOVE},
            {70ms, {{0, 130, 130}, {1, 700, 700}}, AMOTION_EVENT_ACTION_MOVE},
            {75ms,
             {{0, 135, 135, .isResampled = true}, {1, 750, 750, .isResampled = true}},
             AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // First pointer id=0 leaves the screen
    if (input_flags::fix_action_up_resampling()) {
        // We resample the ACTION_UP event to use the coordinates of the previous move
        // event's resampled coordinates. This fixes situations where the last ACTION_MOVE would use
        // resampled coordinates, but the ACTION_UP used the non-resampled coordinates. This caused
        // an unwanted 'jump back' in coordinates.
        entries = {
            //      id  x    y
            {80ms, {{0, 135, 135, .isResampled = true},
                    {1, 750, 750, .isResampled = true}}, actionPointer0Up},
        };
    } else {
        entries = {
            //      id  x    y
            {80ms, {{0, 120, 120},
                    {1, 600, 600}}, actionPointer0Up},
        };
    }
    publishInputEventEntries(entries);
    frameTime = 90ms;
    if (input_flags::fix_action_up_resampling()) {
        expectedEntries = {
            //      id  x    y
            {80ms, {{0, 135, 135, .isResampled = true},
                    {1, 750, 750, .isResampled = true}}, actionPointer0Up},
        };
    } else {
        expectedEntries = {
            //      id  x    y
            {80ms, {{0, 120, 120},
                    {1, 600, 600}}, actionPointer0Up},
            // no resampled event for ACTION_POINTER_UP
        };
    }
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Remaining pointer id=1 is still present, but doesn't move
    entries = {
            //      id  x    y
            {90ms, {{1, 600, 600}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);
    frameTime = 100ms;
    expectedEntries = {
            //      id  x    y
            {90ms, {{1, 600, 600}}, AMOTION_EVENT_ACTION_MOVE},
            /**
             * The latest event with ACTION_MOVE was at t = 70, coord = 700.
             * Use that value for resampling here: (600 - 700) / (90 - 70) * 5 + 600
             */
            {95ms, {{1, 575, 575, .isResampled = true}}, AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
}

TEST_F(TouchResamplingTest, EventsOnDifferentDisplaysAreNotResampled) {
    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // Initial ACTION_DOWN should be separate, because the first consume event will only return
    // InputEvent with a single action.
    entries = {
            //      id  x   y
            {0ms, {{0, 10, 20}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    expectedEntries = {
            //      id  x   y
            {0ms, {{0, 10, 20}}, AMOTION_EVENT_ACTION_DOWN},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);

    // Two ACTION_MOVE events 10 ms apart that move in X direction and stay still in Y, but on
    // different displays.
    entries = {
            //      id  x   y
            {10ms, {{0, 20, 30}}, AMOTION_EVENT_ACTION_MOVE, ui::LogicalDisplayId::DEFAULT},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE, ui::LogicalDisplayId(1)},
    };
    publishInputEventEntries(entries);

    // They are not resampled and sent as two separate events.
    frameTime = 35ms;
    expectedEntries = {
            //      id  x   y
            {10ms, {{0, 20, 30}}, AMOTION_EVENT_ACTION_MOVE, ui::LogicalDisplayId::DEFAULT},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
    expectedEntries = {
            //      id  x   y
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE, ui::LogicalDisplayId(1)},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
}

/*
 * When an ACTION_UP occurs and there are no pending move events, we expect that the ACTION_UP is
 * resampled to the last resampled ACTION_MOVE's coordinate. Note this applies when the
 * fix_action_up_resampling flag is enabled.
 */
TEST_F(TouchResamplingTest, ActionUpUsesLastConsumedResampledStateNoPendingMoves) {
    SCOPED_FLAG_OVERRIDE(fix_action_up_resampling, true);

    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // Initial ACTION_DOWN
    entries = {
            //     id  x   y
            {0ms, {{0, 10, 10}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    consumeInputEventEntries(entries, frameTime, /*consumeBatches=*/true);

    // Two ACTION_MOVE events to establish a resampling history
    entries = {
            //      id  x   y
            {10ms, {{0, 20, 20}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);

    // Consume the batch, frameTime will cause extrapolation
    frameTime = 35ms; // RESAMPLE_LATENCY is 5ms, so sampleTime is 30ms
    // Extrapolation limit: 20ms + (20ms - 10ms) / 2 = 25ms
    expectedEntries = {
            //      id  x   y
            {10ms, {{0, 20, 20}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {25ms, {{0, 35, 35, .isResampled = true}}, AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
    // At this point, TouchState::lastResample is based on the 25ms resampled event (x=35, y=35)

    // Publish ACTION_UP
    entries = {
            //      id  x   y
            {60ms, {{0, 205, 205}}, AMOTION_EVENT_ACTION_UP},
    };
    publishInputEventEntries(entries);

    expectedEntries = {
            //      id  x   y
            {60ms, {{0, 35, 35, .isResampled = true}}, AMOTION_EVENT_ACTION_UP},
    };

    consumeInputEventEntries(expectedEntries, -1ms, /*consumeBatches=*/false);
}

/*
 * A test for the legacy behaviour of the test above. The expected behaviour is to not resample
 * the ACTION_UP event coordinates.
 */
TEST_F(TouchResamplingTest, ActionUpUsesLastConsumedResampledStateNoPendingMoves_legacy) {
    SCOPED_FLAG_OVERRIDE(fix_action_up_resampling, false);

    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // Initial ACTION_DOWN
    entries = {
            //     id  x   y
            {0ms, {{0, 10, 10}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    consumeInputEventEntries(entries, frameTime, /*consumeBatches=*/true);

    // Two ACTION_MOVE events to establish a resampling history
    entries = {
            //      id  x   y
            {10ms, {{0, 20, 20}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);

    // Consume the batch, frameTime will cause extrapolation
    frameTime = 35ms; // RESAMPLE_LATENCY is 5ms, so sampleTime is 30ms
    // Extrapolation limit: 20ms + (20ms - 10ms) / 2 = 25ms
    expectedEntries = {
            //      id  x   y
            {10ms, {{0, 20, 20}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {25ms, {{0, 35, 35, .isResampled = true}}, AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
    // At this point, TouchState::lastResample is based on the 25ms resampled event (x=35, y=35)

    // Publish ACTION_UP
    entries = {
            //      id  x   y
            {60ms, {{0, 205, 205}}, AMOTION_EVENT_ACTION_UP},
    };
    publishInputEventEntries(entries);

    expectedEntries = {
            //      id  x   y
            {60ms, {{0, 205, 205}}, AMOTION_EVENT_ACTION_UP},
    };

    consumeInputEventEntries(expectedEntries, -1ms, /*consumeBatches=*/false);
}

/*
 * Similar to above, however we don't resample as the previously resampled batched move coordinates
 * are invalid in regards to lastResample given that we have pending moves. Therefore we just use
 * the ACTION_UP's actual coordinates.
 */
TEST_F(TouchResamplingTest, ActionUpUsesLastConsumedResampledStatePendingMoves) {
    std::chrono::nanoseconds frameTime;
    std::vector<InputEventEntry> entries, expectedEntries;

    // Initial ACTION_DOWN
    entries = {
            //     id  x   y
            {0ms, {{0, 10, 10}}, AMOTION_EVENT_ACTION_DOWN},
    };
    publishInputEventEntries(entries);
    frameTime = 5ms;
    consumeInputEventEntries(entries, frameTime, /*consumeBatches=*/true);

    // Two ACTION_MOVE events to establish a resampling history
    entries = {
            //      id  x   y
            {10ms, {{0, 20, 20}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);

    // Consume the batch, frameTime will cause extrapolation
    frameTime = 35ms; // RESAMPLE_LATENCY is 5ms, so sampleTime is 30ms
    // Extrapolation limit: 20ms + (20ms - 10ms) / 2 = 25ms
    expectedEntries = {
            //      id  x   y
            {10ms, {{0, 20, 20}}, AMOTION_EVENT_ACTION_MOVE},
            {20ms, {{0, 30, 30}}, AMOTION_EVENT_ACTION_MOVE},
            {25ms, {{0, 35, 35, .isResampled = true}}, AMOTION_EVENT_ACTION_MOVE},
    };
    consumeInputEventEntries(expectedEntries, frameTime, /*consumeBatches=*/true);
    // At this point, TouchState::lastResample is based on the 25ms resampled event (x=35, y=35)

    // Publish more ACTION_MOVEs (new batch), NOT consumed yet.
    entries = {
            //      id  x   y
            {40ms, {{0, 100, 100}}, AMOTION_EVENT_ACTION_MOVE},
            {50ms, {{0, 200, 200}}, AMOTION_EVENT_ACTION_MOVE},
    };
    publishInputEventEntries(entries);

    // Publish ACTION_UP
    entries = {
            //      id  x   y
            {60ms, {{0, 205, 205}}, AMOTION_EVENT_ACTION_UP},
    };
    publishInputEventEntries(entries);

    expectedEntries = {
            //      id  x   y
            {40ms, {{0, 100, 100}}, AMOTION_EVENT_ACTION_MOVE},
            {50ms, {{0, 200, 200}}, AMOTION_EVENT_ACTION_MOVE},
    };

    // The batched moves are consumed, but the ACTION_UP is deferred to the next consume cycle.
    consumeInputEventEntries(expectedEntries, -1ms, /*consumeBatches=*/false);

    expectedEntries = {
            //      id  x   y
            {60ms, {{0, 205, 205}}, AMOTION_EVENT_ACTION_UP},
    };

    consumeInputEventEntries(expectedEntries, -1ms, /*consumeBatches=*/false);
}

} // namespace android
