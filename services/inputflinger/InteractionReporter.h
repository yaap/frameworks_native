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

#include "InteractionReporterInterface.h"
#include "NotifyArgs.h"

#include <memory>

namespace android {

class InteractionReporter : public InteractionReporterInterface {
public:
    InteractionReporter(InputListenerInterface& nextListener);
    ~InteractionReporter() override = default;

    void setInteractionProviderService(
            std::unique_ptr<attention::NativeInteractionManager> interactionManager) override;

    void notifyInputDevicesChanged(const NotifyInputDevicesChangedArgs& args) override;

    void notifyKey(const NotifyKeyArgs& args) override;

    void notifyMotion(const NotifyMotionArgs& args) override;

    void notifySwitch(const NotifySwitchArgs& args) override;

    void notifySensor(const NotifySensorArgs& args) override;

    void notifyVibratorState(const NotifyVibratorStateArgs& args) override;

    void notifyDeviceReset(const NotifyDeviceResetArgs& args) override;

    void notifyPointerCaptureChanged(const NotifyPointerCaptureChangedArgs& args) override;

private:
    InputListenerInterface& mNextListener;
    std::unique_ptr<attention::NativeInteractionManager> mInteractionManager;

protected:
    std::shared_ptr<attention::InteractionProvider> mAttentionInteractionProvider;
};

} // namespace android
