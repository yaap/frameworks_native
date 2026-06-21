/*
 * Copyright (C) 2023 The Android Open Source Project
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

#define LOG_TAG "InputVerifier"

#include <android-base/logging.h>
#include <input/Input.h>
#include <input/InputVerifier.h>
#include <input/PrintTools.h>
#include <utils/Timers.h>
#include "input_cxx_bridge.rs.h"

using android::base::Error;
using android::base::Result;
using android::input::ProcessMovementResult;
using android::input::RustPointerProperties;

using DeviceId = int32_t;

namespace android {

// --- InputVerifier ---

InputVerifier::InputVerifier(const std::string& name)
      : mVerifier(android::input::verifier::create(rust::String::lossy(name))) {}

Result<bool> InputVerifier::processMovement(DeviceId deviceId, nsecs_t eventTime, int32_t source,
                                            int32_t action, int32_t actionButton,
                                            uint32_t pointerCount,
                                            const PointerProperties* pointerProperties,
                                            const PointerCoords* pointerCoords, int32_t flags,
                                            int32_t buttonState, nsecs_t downTime) {
    std::vector<RustPointerProperties> rpp;
    for (size_t i = 0; i < pointerCount; i++) {
        rpp.emplace_back(RustPointerProperties{.id = pointerProperties[i].id});
    }
    rust::Slice<const RustPointerProperties> properties{rpp.data(), rpp.size()};
    const ProcessMovementResult result =
            android::input::verifier::process_movement(*mVerifier, deviceId, eventTime, source,
                                                       action, actionButton, properties,
                                                       static_cast<uint32_t>(flags),
                                                       static_cast<uint32_t>(buttonState),
                                                       downTime);
    if (result.error.empty()) {
        return result.is_empty;
    } else {
        return Error() << result.error;
    }
}

bool InputVerifier::isEmpty() const {
    return android::input::verifier::is_empty(*mVerifier);
}

std::string InputVerifier::dump() const {
    const rust::String out = android::input::verifier::dump(*mVerifier);
    return streamableToString(out);
}

void InputVerifier::resetDevice(DeviceId deviceId) {
    android::input::verifier::reset_device(*mVerifier, deviceId);
}

} // namespace android
