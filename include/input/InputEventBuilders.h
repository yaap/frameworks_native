/*
 * Copyright 2023 The Android Open Source Project
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

#include <android/input.h>
#include <attestation/HmacKeyManager.h>
#include <ftl/flags.h>
#include <input/Input.h>
#include <input/InputTransport.h>
#include <ui/LogicalDisplayId.h>
#include <ui/Transform.h>
#include <utils/Timers.h> // for nsecs_t, systemTime

#include <vector>

namespace android {

// An arbitrary device id.
static constexpr uint32_t DEFAULT_DEVICE_ID = 1;

// The default policy flags to use for event injection by tests.
static constexpr uint32_t DEFAULT_POLICY_FLAGS = POLICY_FLAG_FILTERED | POLICY_FLAG_PASS_TO_USER;

class PointerBuilder {
public:
    PointerBuilder(int32_t id, ToolType toolType) {
        mProperties.clear();
        mProperties.id = id;
        mProperties.toolType = toolType;
        mCoords.clear();
    }

    PointerBuilder& x(float x) { return axis(AMOTION_EVENT_AXIS_X, x); }

    PointerBuilder& y(float y) { return axis(AMOTION_EVENT_AXIS_Y, y); }

    PointerBuilder& isResampled(bool isResampled) {
        mCoords.isResampled = isResampled;
        return *this;
    }

    PointerBuilder& axis(int32_t axis, float value) {
        mCoords.setAxisValue(axis, value);
        return *this;
    }

    PointerProperties buildProperties() const { return mProperties; }

    PointerCoords buildCoords() const { return mCoords; }

private:
    PointerProperties mProperties;
    PointerCoords mCoords;
};

class InputMessageBuilder {
public:
    InputMessageBuilder(InputMessage::Type type, uint32_t seq) : mType{type}, mSeq{seq} {}

    InputMessageBuilder& eventId(int32_t eventId) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::KEY &&
                                    mType != InputMessage::Type::MOTION &&
                                    mType != InputMessage::Type::FOCUS &&
                                    mType != InputMessage::Type::CAPTURE &&
                                    mType != InputMessage::Type::DRAG &&
                                    mType != InputMessage::Type::TIMELINE &&
                                    mType != InputMessage::Type::TOUCH_MODE,
                            "Mismatched message type: %d, expected KEY, MOTION, FOCUS, CAPTURE, "
                            "DRAG, TIMELINE, or TOUCH_MODE",
                            mType);
        mEventId = eventId;
        return *this;
    }

    InputMessageBuilder& eventTime(nsecs_t eventTime) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::KEY && mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected KEY or MOTION", mType);
        mEventTime = eventTime;
        return *this;
    }

    InputMessageBuilder& deviceId(DeviceId deviceId) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::KEY && mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected KEY or MOTION", mType);
        mDeviceId = deviceId;
        return *this;
    }

    InputMessageBuilder& source(int32_t source) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::KEY && mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected KEY or MOTION", mType);
        mSource = source;
        return *this;
    }

    InputMessageBuilder& displayId(ui::LogicalDisplayId displayId) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::KEY && mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected KEY or MOTION", mType);
        mDisplayId = displayId;
        return *this;
    }

    InputMessageBuilder& hmac(const std::array<uint8_t, 32>& hmac) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::KEY && mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected KEY or MOTION", mType);
        mHmac = hmac;
        return *this;
    }

    InputMessageBuilder& action(int32_t action) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::KEY && mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected KEY or MOTION", mType);
        mAction = action;
        return *this;
    }

    InputMessageBuilder& flags(int32_t flags) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::KEY && mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected KEY or MOTION", mType);
        mFlags = flags;
        return *this;
    }

    InputMessageBuilder& metaState(int32_t metaState) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::KEY && mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected KEY or MOTION", mType);
        mMetaState = metaState;
        return *this;
    }

    InputMessageBuilder& downTime(nsecs_t downTime) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::KEY && mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected KEY or MOTION", mType);
        mDownTime = downTime;
        return *this;
    }

    // Builder methods for Key
    InputMessageBuilder& keyCode(int32_t keyCode) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::KEY,
                            "Mismatched message type: %d, expected KEY", mType);
        mKeyCode = keyCode;
        return *this;
    }
    InputMessageBuilder& scanCode(int32_t scanCode) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::KEY,
                            "Mismatched message type: %d, expected KEY", mType);
        mScanCode = scanCode;
        return *this;
    }
    InputMessageBuilder& repeatCount(int32_t repeatCount) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::KEY,
                            "Mismatched message type: %d, expected KEY", mType);
        mRepeatCount = repeatCount;
        return *this;
    }

    // Builder methods for Motion
    InputMessageBuilder& actionButton(int32_t actionButton) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected MOTION", mType);
        mActionButton = actionButton;
        return *this;
    }
    InputMessageBuilder& buttonState(int32_t buttonState) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected MOTION", mType);
        mButtonState = buttonState;
        return *this;
    }
    InputMessageBuilder& classification(MotionClassification classification) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected MOTION", mType);
        mClassification = classification;
        return *this;
    }
    InputMessageBuilder& edgeFlags(int32_t edgeFlags) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected MOTION", mType);
        mEdgeFlags = edgeFlags;
        return *this;
    }
    InputMessageBuilder& transform(const ui::Transform& transform) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected MOTION", mType);
        mTransform = transform;
        return *this;
    }
    InputMessageBuilder& xPrecision(float xPrecision) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected MOTION", mType);
        mXPrecision = xPrecision;
        return *this;
    }
    InputMessageBuilder& yPrecision(float yPrecision) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected MOTION", mType);
        mYPrecision = yPrecision;
        return *this;
    }
    InputMessageBuilder& xCursorPosition(float xCursorPosition) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected MOTION", mType);
        mXCursorPosition = xCursorPosition;
        return *this;
    }
    InputMessageBuilder& yCursorPosition(float yCursorPosition) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected MOTION", mType);
        mYCursorPosition = yCursorPosition;
        return *this;
    }
    InputMessageBuilder& rawTransform(const ui::Transform& rawTransform) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected MOTION", mType);
        mRawTransform = rawTransform;
        return *this;
    }
    InputMessageBuilder& pointer(PointerBuilder pointerBuilder) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::MOTION,
                            "Mismatched message type: %d, expected MOTION", mType);
        mPointers.push_back(pointerBuilder);
        return *this;
    }

    // Builder methods for Finished
    InputMessageBuilder& handled(bool handled) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::FINISHED,
                            "Mismatched message type: %d, expected FINISHED", mType);
        mHandled = handled;
        return *this;
    }
    InputMessageBuilder& consumeTime(nsecs_t consumeTime) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::FINISHED,
                            "Mismatched message type: %d, expected FINISHED", mType);
        mConsumeTime = consumeTime;
        return *this;
    }

    // Builder methods for Focus
    InputMessageBuilder& hasFocus(bool hasFocus) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::FOCUS,
                            "Mismatched message type: %d, expected FOCUS", mType);
        mHasFocus = hasFocus;
        return *this;
    }

    // Builder methods for Capture
    InputMessageBuilder& pointerCaptureEnabled(bool enabled) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::CAPTURE,
                            "Mismatched message type: %d, expected CAPTURE", mType);
        mPointerCaptureEnabled = enabled;
        return *this;
    }

    // Builder methods for Drag
    InputMessageBuilder& x(float x) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::DRAG,
                            "Mismatched message type: %d, expected DRAG", mType);
        mX = x;
        return *this;
    }
    InputMessageBuilder& y(float y) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::DRAG,
                            "Mismatched message type: %d, expected DRAG", mType);
        mY = y;
        return *this;
    }
    InputMessageBuilder& isExiting(bool isExiting) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::DRAG,
                            "Mismatched message type: %d, expected DRAG", mType);
        mIsExiting = isExiting;
        return *this;
    }

    // Builder methods for TouchMode
    InputMessageBuilder& inTouchMode(bool inTouchMode) {
        LOG_ALWAYS_FATAL_IF(mType != InputMessage::Type::TOUCH_MODE,
                            "Mismatched message type: %d, expected TOUCH_MODE", mType);
        mInTouchMode = inTouchMode;
        return *this;
    }

    InputMessage build() const {
        InputMessage message{};
        // Header
        message.header.type = mType;
        message.header.seq = mSeq;

        // Body
        switch (mType) {
            case InputMessage::Type::KEY: {
                message.body.key.eventId = mEventId;
                message.body.key.eventTime = mEventTime;
                message.body.key.deviceId = mDeviceId;
                message.body.key.source = mSource;
                message.body.key.displayId = mDisplayId.val();
                message.body.key.hmac = std::move(mHmac);
                message.body.key.action = mAction;
                message.body.key.flags = mFlags;
                message.body.key.keyCode = mKeyCode;
                message.body.key.scanCode = mScanCode;
                message.body.key.metaState = mMetaState;
                message.body.key.repeatCount = mRepeatCount;
                message.body.key.downTime = mDownTime;
                break;
            }
            case InputMessage::Type::MOTION: {
                message.body.motion.eventId = mEventId;
                message.body.motion.pointerCount = mPointers.size();
                message.body.motion.eventTime = mEventTime;
                message.body.motion.deviceId = mDeviceId;
                message.body.motion.source = mSource;
                message.body.motion.displayId = mDisplayId.val();
                message.body.motion.hmac = std::move(mHmac);
                message.body.motion.action = mAction;
                message.body.motion.actionButton = mActionButton;
                message.body.motion.flags = mFlags;
                message.body.motion.metaState = mMetaState;
                message.body.motion.buttonState = mButtonState;
                message.body.motion.classification = mClassification;
                message.body.motion.edgeFlags = mEdgeFlags;
                message.body.motion.downTime = mDownTime;
                message.body.motion.dsdx = mTransform.dsdx();
                message.body.motion.dtdx = mTransform.dtdx();
                message.body.motion.dtdy = mTransform.dtdy();
                message.body.motion.dsdy = mTransform.dsdy();
                message.body.motion.tx = mTransform.tx();
                message.body.motion.ty = mTransform.ty();
                message.body.motion.xPrecision = mXPrecision;
                message.body.motion.yPrecision = mYPrecision;
                message.body.motion.xCursorPosition = mXCursorPosition;
                message.body.motion.yCursorPosition = mYCursorPosition;
                message.body.motion.dsdxRaw = mRawTransform.dsdx();
                message.body.motion.dtdxRaw = mRawTransform.dtdx();
                message.body.motion.dtdyRaw = mRawTransform.dtdy();
                message.body.motion.dsdyRaw = mRawTransform.dsdy();
                message.body.motion.txRaw = mRawTransform.tx();
                message.body.motion.tyRaw = mRawTransform.ty();

                for (size_t i = 0; i < mPointers.size(); ++i) {
                    message.body.motion.pointers[i].properties = mPointers[i].buildProperties();
                    message.body.motion.pointers[i].coords = mPointers[i].buildCoords();
                }
                break;
            }
            case InputMessage::Type::FINISHED: {
                message.body.finished.handled = mHandled;
                message.body.finished.consumeTime = mConsumeTime;
                break;
            }
            case InputMessage::Type::FOCUS: {
                message.body.focus.eventId = mEventId;
                message.body.focus.hasFocus = mHasFocus;
                break;
            }
            case InputMessage::Type::CAPTURE: {
                message.body.capture.eventId = mEventId;
                message.body.capture.pointerCaptureEnabled = mPointerCaptureEnabled;
                break;
            }
            case InputMessage::Type::DRAG: {
                message.body.drag.eventId = mEventId;
                message.body.drag.x = mX;
                message.body.drag.y = mY;
                message.body.drag.isExiting = mIsExiting;
                break;
            }
            case InputMessage::Type::TIMELINE: {
                message.body.timeline.eventId = mEventId;
                message.body.timeline.graphicsTimeline = mGraphicsTimeline;
                break;
            }
            case InputMessage::Type::TOUCH_MODE: {
                message.body.touchMode.eventId = mEventId;
                message.body.touchMode.isInTouchMode = mInTouchMode;
                break;
            }
            default: {
                LOG_ALWAYS_FATAL("Unknown message type: %d", mType);
            }
        }
        return message;
    }

private:
    const InputMessage::Type mType;
    const uint32_t mSeq;

    // Common fields
    int32_t mEventId{InputEvent::nextId()};
    nsecs_t mEventTime{systemTime(SYSTEM_TIME_MONOTONIC)};
    DeviceId mDeviceId{DEFAULT_DEVICE_ID};
    int32_t mSource{AINPUT_SOURCE_TOUCHSCREEN};
    ui::LogicalDisplayId mDisplayId{ui::LogicalDisplayId::DEFAULT};
    std::array<uint8_t, 32> mHmac{INVALID_HMAC};
    int32_t mAction{AMOTION_EVENT_ACTION_MOVE};
    int32_t mFlags{0};
    int32_t mMetaState{AMETA_NONE};
    nsecs_t mDownTime{mEventTime};

    // Key fields
    int32_t mKeyCode{0};
    int32_t mScanCode{0};
    int32_t mRepeatCount{0};

    // Motion fields
    int32_t mActionButton{0};
    int32_t mButtonState{0};
    MotionClassification mClassification{MotionClassification::NONE};
    int32_t mEdgeFlags{0};
    ui::Transform mTransform{};
    float mXPrecision{1.0f};
    float mYPrecision{1.0f};
    float mXCursorPosition{AMOTION_EVENT_INVALID_CURSOR_POSITION};
    float mYCursorPosition{AMOTION_EVENT_INVALID_CURSOR_POSITION};
    ui::Transform mRawTransform{};
    std::vector<PointerBuilder> mPointers;

    // Finished fields
    bool mHandled{true};
    nsecs_t mConsumeTime{systemTime(SYSTEM_TIME_MONOTONIC)};

    // Focus fields
    bool mHasFocus{false};

    // Capture fields
    bool mPointerCaptureEnabled{false};

    // Drag fields
    float mX{0.0f};
    float mY{0.0f};
    bool mIsExiting{false};

    // Timeline fields
    std::array<nsecs_t, GraphicsTimeline::SIZE> mGraphicsTimeline;

    // TouchMode fields
    bool mInTouchMode{false};
};

class MotionEventBuilder {
public:
    MotionEventBuilder(int32_t action, int32_t source) {
        mAction = action;
        if (mAction == AMOTION_EVENT_ACTION_CANCEL) {
            mFlags |= MotionFlag::CANCELED;
        }
        mSource = source;
        mEventTime = systemTime(SYSTEM_TIME_MONOTONIC);
        mDownTime = mEventTime;
    }

    MotionEventBuilder& deviceId(int32_t deviceId) {
        mDeviceId = deviceId;
        return *this;
    }

    MotionEventBuilder& downTime(nsecs_t downTime) {
        mDownTime = downTime;
        return *this;
    }

    MotionEventBuilder& eventTime(nsecs_t eventTime) {
        mEventTime = eventTime;
        return *this;
    }

    MotionEventBuilder& displayId(ui::LogicalDisplayId displayId) {
        mDisplayId = displayId;
        return *this;
    }

    MotionEventBuilder& actionButton(int32_t actionButton) {
        mActionButton = actionButton;
        return *this;
    }

    MotionEventBuilder& buttonState(int32_t buttonState) {
        mButtonState = buttonState;
        return *this;
    }

    MotionEventBuilder& rawXCursorPosition(float rawXCursorPosition) {
        mRawXCursorPosition = rawXCursorPosition;
        return *this;
    }

    MotionEventBuilder& rawYCursorPosition(float rawYCursorPosition) {
        mRawYCursorPosition = rawYCursorPosition;
        return *this;
    }

    MotionEventBuilder& pointer(PointerBuilder pointer) {
        mPointers.push_back(pointer);
        return *this;
    }

    MotionEventBuilder& addFlag(ftl::Flags<MotionFlag> flags) {
        mFlags |= flags;
        return *this;
    }

    MotionEventBuilder& transform(ui::Transform t) {
        mTransform = t;
        return *this;
    }

    MotionEventBuilder& rawTransform(ui::Transform t) {
        mRawTransform = t;
        return *this;
    }

    MotionEvent build() const {
        std::vector<PointerProperties> pointerProperties;
        std::vector<PointerCoords> pointerCoords;
        for (const PointerBuilder& pointer : mPointers) {
            pointerProperties.push_back(pointer.buildProperties());
            pointerCoords.push_back(pointer.buildCoords());
        }

        auto [xCursorPosition, yCursorPosition] =
                std::make_pair(mRawXCursorPosition, mRawYCursorPosition);
        // Set mouse cursor position for the most common cases to avoid boilerplate.
        if (mSource == AINPUT_SOURCE_MOUSE &&
            !MotionEvent::isValidCursorPosition(xCursorPosition, yCursorPosition)) {
            xCursorPosition = pointerCoords[0].getX();
            yCursorPosition = pointerCoords[0].getY();
        }

        MotionEvent event;
        event.initialize(InputEvent::nextId(), mDeviceId, mSource, mDisplayId, INVALID_HMAC,
                         mAction, mActionButton, mFlags, /*edgeFlags=*/0, AMETA_NONE, mButtonState,
                         MotionClassification::NONE, mTransform,
                         /*xPrecision=*/0, /*yPrecision=*/0, xCursorPosition, yCursorPosition,
                         mRawTransform, mDownTime, mEventTime, mPointers.size(),
                         pointerProperties.data(), pointerCoords.data());
        return event;
    }

private:
    int32_t mAction;
    int32_t mDeviceId{DEFAULT_DEVICE_ID};
    int32_t mSource;
    nsecs_t mDownTime;
    nsecs_t mEventTime;
    ui::LogicalDisplayId mDisplayId{ui::LogicalDisplayId::DEFAULT};
    int32_t mActionButton{0};
    int32_t mButtonState{0};
    ftl::Flags<MotionFlag> mFlags{};
    float mRawXCursorPosition{AMOTION_EVENT_INVALID_CURSOR_POSITION};
    float mRawYCursorPosition{AMOTION_EVENT_INVALID_CURSOR_POSITION};
    ui::Transform mTransform;
    ui::Transform mRawTransform;

    std::vector<PointerBuilder> mPointers;
};

class KeyEventBuilder {
public:
    KeyEventBuilder(int32_t action, int32_t source) {
        mAction = action;
        mSource = source;
        mEventTime = systemTime(SYSTEM_TIME_MONOTONIC);
        mDownTime = mEventTime;
    }

    KeyEventBuilder(const KeyEvent& event) {
        mAction = event.getAction();
        mDeviceId = event.getDeviceId();
        mSource = event.getSource();
        mDownTime = event.getDownTime();
        mEventTime = event.getEventTime();
        mDisplayId = event.getDisplayId();
        mFlags = event.getFlags();
        mKeyCode = event.getKeyCode();
        mScanCode = event.getScanCode();
        mMetaState = event.getMetaState();
        mRepeatCount = event.getRepeatCount();
    }

    KeyEventBuilder& deviceId(int32_t deviceId) {
        mDeviceId = deviceId;
        return *this;
    }

    KeyEventBuilder& downTime(nsecs_t downTime) {
        mDownTime = downTime;
        return *this;
    }

    KeyEventBuilder& eventTime(nsecs_t eventTime) {
        mEventTime = eventTime;
        return *this;
    }

    KeyEventBuilder& displayId(ui::LogicalDisplayId displayId) {
        mDisplayId = displayId;
        return *this;
    }

    KeyEventBuilder& policyFlags(int32_t policyFlags) {
        mPolicyFlags = policyFlags;
        return *this;
    }

    KeyEventBuilder& addFlag(uint32_t flags) {
        mFlags |= flags;
        return *this;
    }

    KeyEventBuilder& keyCode(int32_t keyCode) {
        mKeyCode = keyCode;
        return *this;
    }

    KeyEventBuilder& repeatCount(int32_t repeatCount) {
        mRepeatCount = repeatCount;
        return *this;
    }

    KeyEvent build() const {
        KeyEvent event{};
        event.initialize(InputEvent::nextId(), mDeviceId, mSource, mDisplayId, INVALID_HMAC,
                         mAction, mFlags, mKeyCode, mScanCode, mMetaState, mRepeatCount, mDownTime,
                         mEventTime);
        return event;
    }

private:
    int32_t mAction;
    int32_t mDeviceId = DEFAULT_DEVICE_ID;
    uint32_t mSource;
    nsecs_t mDownTime;
    nsecs_t mEventTime;
    ui::LogicalDisplayId mDisplayId{ui::LogicalDisplayId::DEFAULT};
    uint32_t mPolicyFlags = DEFAULT_POLICY_FLAGS;
    int32_t mFlags{0};
    int32_t mKeyCode{AKEYCODE_UNKNOWN};
    int32_t mScanCode{0};
    int32_t mMetaState{AMETA_NONE};
    int32_t mRepeatCount{0};
};

} // namespace android
