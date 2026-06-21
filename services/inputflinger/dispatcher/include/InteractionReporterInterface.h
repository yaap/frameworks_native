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

#pragma once

#include "InputListener.h"
#include "NotifyArgs.h"

#include <attention/NativeInteractionManager.h>

#include <memory>

namespace android {

class InteractionReporterInterface : public InputListenerInterface {
public:
    virtual void setInteractionProviderService(
            std::unique_ptr<attention::NativeInteractionManager> interactionManager) = 0;

    void notifyInputDevicesChanged(const NotifyInputDevicesChangedArgs& args) override = 0;

    void notifyKey(const NotifyKeyArgs& args) override = 0;

    void notifyMotion(const NotifyMotionArgs& args) override = 0;

    void notifySwitch(const NotifySwitchArgs& args) override = 0;

    void notifySensor(const NotifySensorArgs& args) override = 0;

    void notifyVibratorState(const NotifyVibratorStateArgs& args) override = 0;

    void notifyDeviceReset(const NotifyDeviceResetArgs& args) override = 0;

    void notifyPointerCaptureChanged(const NotifyPointerCaptureChangedArgs& args) override = 0;
};

} // namespace android