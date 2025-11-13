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

#include <memory>
#include <span>
#include <string>

#include <android-base/expected.h>

#include "test_framework/core/DisplayConfiguration.h"
#include "test_framework/hwc3/events/ClientDestroyed.h"
#include "test_framework/hwc3/events/DisplayPresented.h"
#include "test_framework/hwc3/events/PendingBufferSwap.h"
#include "test_framework/hwc3/events/PowerMode.h"
#include "test_framework/hwc3/events/VSync.h"
#include "test_framework/hwc3/events/VSyncEnabled.h"

namespace android::surfaceflinger::tests::end2end::test_framework::hwc3 {

class FakeComposer;
class ObservingComposer;

class Hwc3Controller final : public std::enable_shared_from_this<Hwc3Controller> {
    struct Passkey;  // Uses the passkey idiom to restrict construction.

  public:
    struct Callbacks final {
        // Invoked when SF destroys its HWC client connection.
        events::ClientDestroyed::AsyncConnector onClientDestroyed;

        // Invoked when SF configures the power mode for a display.
        events::PowerMode::AsyncConnector onPowerModeChanged;

        // Invoked when SF enables or disables vsync callbacks for a display.
        events::VSyncEnabled::AsyncConnector onVsyncEnabledChanged;

        // Invoked when SF presents a display.
        events::DisplayPresented::AsyncConnector onDisplayPresented;

        // Invoked when SF is swapping the buffer content of a hardware overlay.
        events::PendingBufferSwap::AsyncConnector onPendingBufferSwap;

        // Invoked when the client sends SF a vsync callback.
        events::VSync::AsyncConnector onVSyncCallbackSent;
    };

    // Gets the service name for the HWC3 instance that will be created and registered
    [[nodiscard]] static auto getServiceName() -> std::string;

    // Makes the HWC3 controller instance.
    [[nodiscard]] static auto make(std::span<const core::DisplayConfiguration> displays)
            -> base::expected<std::shared_ptr<hwc3::Hwc3Controller>, std::string>;

    explicit Hwc3Controller(Passkey passkey);

    // Allows the callbacks to be routed.
    [[nodiscard]] auto editCallbacks() -> Callbacks&;

    // Allows the callbacks to be sent.
    [[nodiscard]] auto callbacks() const -> const Callbacks&;

    // Adds a new display to the HWC3, which will become a hotplug connect event.
    void addDisplay(const core::DisplayConfiguration& config);

    // Removes a new display from the HWC3, which will become a hotplug disconnect event.
    void removeDisplay(core::DisplayConfiguration::Id displayId);

  private:
    static constexpr std::string baseServiceName = "fake";

    [[nodiscard]] auto init(std::span<const core::DisplayConfiguration> displays)
            -> base::expected<void, std::string>;

    std::shared_ptr<FakeComposer> mFakeComposer;
    std::shared_ptr<ObservingComposer> mObservingComposer;
    Callbacks mCallbacks;
};

}  // namespace android::surfaceflinger::tests::end2end::test_framework::hwc3
