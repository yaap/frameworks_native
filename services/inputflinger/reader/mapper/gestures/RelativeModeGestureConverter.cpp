/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "gestures/RelativeModeGestureConverter.h"

#include <list>
#include <sstream>
#include <string>

#include <android-base/stringprintf.h>

#include "NotifyArgs.h"
#include "TouchCursorInputMapperCommon.h"
#include "android/input.h"
#include "gestures/GestureConverterCommon.h"
#include "include/gestures.h"
#include "input/Input.h"
#include "input/InputDevice.h"
#include "ui/LogicalDisplayId.h"

namespace android {

RelativeModeGestureConverter::RelativeModeGestureConverter(InputReaderContext& readerContext,
                                                           DeviceId deviceId)
      : mDeviceId(deviceId), mReaderContext(readerContext) {}

std::string RelativeModeGestureConverter::dump() const {
    std::stringstream out;
    out << StringPrintf("Button state: 0x%08x\n", mButtonState);
    out << "Down time: " << mDownTime << "\n";
    return out.str();
}

std::list<NotifyArgs> RelativeModeGestureConverter::reset(nsecs_t when) {
    std::list<NotifyArgs> out;
    if (mButtonState != 0) {
        const bool pointerDown = isPointerDown(mButtonState);
        PointerCoords coords;
        coords.clear();
        coords.setAxisValue(AMOTION_EVENT_AXIS_PRESSURE, pointerDown ? 1.0f : 0.0f);
        uint32_t newButtonState = mButtonState;
        for (uint32_t button = AMOTION_EVENT_BUTTON_PRIMARY; button <= AMOTION_EVENT_BUTTON_FORWARD;
             button <<= 1) {
            if (mButtonState & button) {
                newButtonState &= ~button;
                out.push_back(makeMotionArgs(when, when, AMOTION_EVENT_ACTION_BUTTON_RELEASE,
                                             button, newButtonState, &coords));
            }
        }
        mButtonState = 0;
        if (pointerDown) {
            coords.setAxisValue(AMOTION_EVENT_AXIS_PRESSURE, 0.0f);
            out.push_back(makeMotionArgs(when, when, AMOTION_EVENT_ACTION_UP, /*actionButton=*/0,
                                         mButtonState, &coords));
        }
    }
    mDownTime = 0;
    return out;
}

void RelativeModeGestureConverter::populateMotionRanges(InputDeviceInfo& info,
                                                        double pointerMovementPerMm,
                                                        double scrollTicksPerMm) const {
    info.addMotionRange(AMOTION_EVENT_AXIS_PRESSURE, SOURCE, 0.0f, 1.0f, 0, 0, 0);

    info.addMotionRange(AMOTION_EVENT_AXIS_X, SOURCE, -1.0f, 1.0f, 0, 0, pointerMovementPerMm);
    info.addMotionRange(AMOTION_EVENT_AXIS_Y, SOURCE, -1.0f, 1.0f, 0, 0, pointerMovementPerMm);
    info.addMotionRange(AMOTION_EVENT_AXIS_RELATIVE_X, SOURCE, -1.0f, 1.0f, 0, 0,
                        pointerMovementPerMm);
    info.addMotionRange(AMOTION_EVENT_AXIS_RELATIVE_Y, SOURCE, -1.0f, 1.0f, 0, 0,
                        pointerMovementPerMm);

    info.addMotionRange(AMOTION_EVENT_AXIS_VSCROLL, SOURCE, -1.0f, 1.0f, 0, 0, scrollTicksPerMm);
    info.addMotionRange(AMOTION_EVENT_AXIS_HSCROLL, SOURCE, -1.0f, 1.0f, 0, 0, scrollTicksPerMm);
}

std::list<NotifyArgs> RelativeModeGestureConverter::handleGesture(nsecs_t when, nsecs_t readTime,
                                                                  nsecs_t gestureStartTime,
                                                                  const Gesture& gesture) {
    switch (gesture.type) {
        case kGestureTypeMove:
            return handleMove(when, readTime, gestureStartTime, gesture);
        case kGestureTypeButtonsChange:
            return handleButtonsChange(when, readTime, gesture);
        case kGestureTypeScroll:
            return handleScroll(when, readTime, gesture);
        default:
            return {};
    }
}

std::list<NotifyArgs> RelativeModeGestureConverter::handleMove(nsecs_t when, nsecs_t readTime,
                                                               nsecs_t gestureStartTime,
                                                               const Gesture& gesture) {
    float deltaX = gesture.details.move.dx;
    float deltaY = gesture.details.move.dy;

    std::list<NotifyArgs> out;
    PointerCoords coords;
    coords.clear();
    coords.setAxisValue(AMOTION_EVENT_AXIS_X, deltaX);
    coords.setAxisValue(AMOTION_EVENT_AXIS_Y, deltaY);
    coords.setAxisValue(AMOTION_EVENT_AXIS_RELATIVE_X, deltaX);
    coords.setAxisValue(AMOTION_EVENT_AXIS_RELATIVE_Y, deltaY);
    coords.setAxisValue(AMOTION_EVENT_AXIS_PRESSURE, isPointerDown(mButtonState) ? 1.0f : 0.0f);

    out.push_back(makeMotionArgs(when, readTime, AMOTION_EVENT_ACTION_MOVE, /*actionButton=*/0,
                                 mButtonState, &coords));
    return out;
}

std::list<NotifyArgs> RelativeModeGestureConverter::handleButtonsChange(nsecs_t when,
                                                                        nsecs_t readTime,
                                                                        const Gesture& gesture) {
    std::list<NotifyArgs> out;

    PointerCoords coords;
    coords.clear();
    // We don't need to set any axis values on the coords, as all of the axes should be zero.

    const uint32_t buttonsPressed = gesture.details.buttons.down;
    const uint32_t buttonsReleased = gesture.details.buttons.up;

    bool pointerDown = isPointerDown(mButtonState) ||
            buttonsPressed &
                    (GESTURES_BUTTON_LEFT | GESTURES_BUTTON_MIDDLE | GESTURES_BUTTON_RIGHT);
    coords.setAxisValue(AMOTION_EVENT_AXIS_PRESSURE, pointerDown ? 1.0f : 0.0f);
    if (!isPointerDown(mButtonState) && pointerDown) {
        mDownTime = when;
    }

    int32_t newButtonState = mButtonState;
    std::list<NotifyArgs> pressEvents;
    for (uint32_t button = 1; button <= GESTURES_BUTTON_FORWARD; button <<= 1) {
        if (buttonsPressed & button) {
            int32_t actionButton = gesturesButtonToMotionEventButton(button);
            newButtonState |= actionButton;
            pressEvents.push_back(makeMotionArgs(when, readTime, AMOTION_EVENT_ACTION_BUTTON_PRESS,
                                                 actionButton, newButtonState, &coords));
        }
    }
    if (!isPointerDown(mButtonState) && isPointerDown(newButtonState)) {
        out.push_back(makeMotionArgs(when, readTime, AMOTION_EVENT_ACTION_DOWN, /*actionButton=*/0,
                                     newButtonState, &coords));
    }
    out.splice(out.end(), pressEvents);

    // The same button may be in both down and up in the same gesture (e.g. in a tap-to-click), in
    // which case we should treat it as having gone down and then up. So, we treat a single button
    // change gesture as two state changes: a set of buttons going down, followed by a set of
    // buttons going up.
    mButtonState = newButtonState;

    for (uint32_t button = 1; button <= GESTURES_BUTTON_FORWARD; button <<= 1) {
        if (buttonsReleased & button) {
            int32_t actionButton = gesturesButtonToMotionEventButton(button);
            newButtonState &= ~actionButton;
            out.push_back(makeMotionArgs(when, readTime, AMOTION_EVENT_ACTION_BUTTON_RELEASE,
                                         actionButton, newButtonState, &coords));
        }
    }
    if (isPointerDown(mButtonState) && !isPointerDown(newButtonState)) {
        coords.setAxisValue(AMOTION_EVENT_AXIS_PRESSURE, 0.0f);
        out.push_back(makeMotionArgs(when, readTime, AMOTION_EVENT_ACTION_UP, /*actionButton=*/0,
                                     newButtonState, &coords));
    }
    mButtonState = newButtonState;

    return out;
}

std::list<NotifyArgs> RelativeModeGestureConverter::handleScroll(nsecs_t when, nsecs_t readTime,
                                                                 const Gesture& gesture) {
    PointerCoords coords;
    coords.clear();
    coords.setAxisValue(AMOTION_EVENT_AXIS_VSCROLL, gesture.details.scroll.dy);
    coords.setAxisValue(AMOTION_EVENT_AXIS_HSCROLL, gesture.details.scroll.dx);
    coords.setAxisValue(AMOTION_EVENT_AXIS_PRESSURE, isPointerDown(mButtonState) ? 1.0f : 0.0f);

    return {makeMotionArgs(when, readTime, AMOTION_EVENT_ACTION_SCROLL, /*actionButton=*/0,
                           mButtonState, &coords)};
}

NotifyMotionArgs RelativeModeGestureConverter::makeMotionArgs(nsecs_t when, nsecs_t readTime,
                                                              int32_t action, int32_t actionButton,
                                                              int32_t buttonState,
                                                              const PointerCoords* pointerCoords) {
    int32_t flags = 0;
    if (action == AMOTION_EVENT_ACTION_CANCEL) {
        flags |= AMOTION_EVENT_FLAG_CANCELED;
    }

    PointerProperties pointerProperties;
    pointerProperties.clear();
    pointerProperties.id = 0;
    pointerProperties.toolType = ToolType::MOUSE;

    return NotifyMotionArgs(mReaderContext.getNextId(), when, readTime, mDeviceId, SOURCE,
                            ui::LogicalDisplayId::INVALID, POLICY_FLAG_WAKE, action, actionButton,
                            flags, mReaderContext.getGlobalMetaState(), buttonState,
                            MotionClassification::NONE, /*pointerCount=*/1, &pointerProperties,
                            pointerCoords, /*xPrecision=*/1.0f, /*yPrecision=*/1.0f,
                            AMOTION_EVENT_INVALID_CURSOR_POSITION,
                            AMOTION_EVENT_INVALID_CURSOR_POSITION, mDownTime, /*videoFrames=*/{});
}

} // namespace android
