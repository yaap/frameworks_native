/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <utility>

#include "InteractionReporter.h"

namespace android {

InteractionReporter::InteractionReporter(InputListenerInterface& nextListener)
      : mNextListener(nextListener) {
    mAttentionInteractionProvider = std::make_shared<attention::InteractionProvider>();
}

void InteractionReporter::setInteractionProviderService(
        std::unique_ptr<attention::NativeInteractionManager> interactionManager) {
    mInteractionManager = std::move(interactionManager);
    mInteractionManager->registerInteractionProvider(mAttentionInteractionProvider);
}

void InteractionReporter::notifyInputDevicesChanged(const NotifyInputDevicesChangedArgs& args) {
    mNextListener.notify(args);
}

void InteractionReporter::notifyKey(const NotifyKeyArgs& args) {
    std::chrono::milliseconds interactionTime =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::nanoseconds(args.eventTime));
    mAttentionInteractionProvider->reportInteraction(attention::InteractionType::KEY,
                                                     interactionTime);
    mNextListener.notify(args);
}

void InteractionReporter::notifyMotion(const NotifyMotionArgs& args) {
    std::chrono::milliseconds interactionTime =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::nanoseconds(args.eventTime));
    switch (args.action) {
        case AMOTION_EVENT_ACTION_HOVER_ENTER:
        case AMOTION_EVENT_ACTION_HOVER_EXIT:
        case AMOTION_EVENT_ACTION_HOVER_MOVE:
            mAttentionInteractionProvider->reportInteraction(attention::InteractionType::HOVER,
                                                             interactionTime);
            break;
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_MOVE:
        case AMOTION_EVENT_ACTION_SCROLL:
            mAttentionInteractionProvider->reportInteraction(attention::InteractionType::GESTURE,
                                                             interactionTime);
            break;
    }
    mNextListener.notify(args);
}

void InteractionReporter::notifySwitch(const NotifySwitchArgs& args) {
    mNextListener.notify(args);
}

void InteractionReporter::notifySensor(const NotifySensorArgs& args) {
    mNextListener.notify(args);
}

void InteractionReporter::notifyVibratorState(const NotifyVibratorStateArgs& args) {
    mNextListener.notify(args);
}

void InteractionReporter::notifyDeviceReset(const NotifyDeviceResetArgs& args) {
    mNextListener.notify(args);
}

void InteractionReporter::notifyPointerCaptureChanged(const NotifyPointerCaptureChangedArgs& args) {
    mNextListener.notify(args);
}

} // namespace android
