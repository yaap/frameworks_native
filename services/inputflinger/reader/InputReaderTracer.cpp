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

#include "include/InputReaderTracer.h"

#include <android-base/logging.h>
#if defined(__ANDROID__)
#include <gui/SurfaceComposerClient.h>
#endif
#include <gui/WindowInfosUpdate.h>
#include <input/Input.h>
#include <utils/StrongPointer.h>

#include <algorithm>
#include <memory>
#include <mutex>

#include "InputTracingBackendInterface.h"
#include "RawEvent.h"

namespace android {

InputReaderTracer::InputReaderTracer(
        std::shared_ptr<input_trace::InputTracingBackendInterface> backend)
      : InputReaderTracer(
                backend,
                [](const sp<android::gui::WindowInfosListener>& listener) {
#if defined(__ANDROID__)
                    android::base::Result<gui::WindowInfosUpdate> result =
                            SurfaceComposerClient::getDefault()->addWindowInfosListener(listener);
                    if (result.ok()) {
                        return result->windowInfos;
                    }
                    LOG(ERROR) << "Failed to register WindowInfosListener in InputReaderTracer: "
                               << result.error().message();
#endif
                    return std::vector<gui::WindowInfo>();
                },
                [](const sp<android::gui::WindowInfosListener>& listener) {
#if defined(__ANDROID__)
                    SurfaceComposerClient::getDefault()->removeWindowInfosListener(listener);
#endif
                }) {
}

InputReaderTracer::InputReaderTracer(
        std::shared_ptr<input_trace::InputTracingBackendInterface> backend,
        const WindowListenerRegisterConsumer& registerListener,
        const WindowListenerUnregisterConsumer& unregisterListener)
      : mBackend(backend),
        mRegisterListener(registerListener),
        mUnregisterListener(unregisterListener) {
    mWindowListener = sp<WindowListener>::make(*this);
    std::vector<gui::WindowInfo> initialWindowInfos = mRegisterListener(mWindowListener);
    mWindowListener->onWindowInfosChanged(
            {initialWindowInfos, /*displayInfos=*/{}, /*vsyncId=*/-1, /*timestamp=*/0});
}

InputReaderTracer::~InputReaderTracer() {
    // After unregistering a WindowInfosListener it could still be called one last time, if the
    // callback was queued before it was unregistered. Set a flag on the listener to stop it from
    // calling the tracer after disposal.
    mWindowListener->mTracerDestroyed = true;
    mUnregisterListener(mWindowListener);
}

void InputReaderTracer::traceRawEvent(const RawEvent& event) {
    input_trace::TracedEventMetadata metadata;
    {
        std::scoped_lock lock(mLock);
        metadata.isSecure = mSecureWindowVisible;
    }
    metadata.processingTimestamp = event.readTime;
    mBackend->traceRawEvent(event, metadata);
}

void InputReaderTracer::traceDeviceAddition(nsecs_t timestamp,
                                            const input_trace::TracedEvdevDevice& device) {
    input_trace::TracedEventMetadata metadata;
    {
        std::scoped_lock lock(mLock);
        metadata.isSecure = mSecureWindowVisible;
    }
    metadata.processingTimestamp = timestamp;
    mBackend->traceEvdevDeviceAddition(device, metadata);
}

void InputReaderTracer::traceDeviceRemoval(nsecs_t timestamp, RawDeviceId deviceId) {
    input_trace::TracedEventMetadata metadata;
    {
        std::scoped_lock lock(mLock);
        metadata.isSecure = mSecureWindowVisible;
    }
    metadata.processingTimestamp = timestamp;
    mBackend->traceEvdevDeviceRemoval(deviceId, metadata);
}

void InputReaderTracer::setSecureWindowVisible(bool visible) {
    std::scoped_lock lock(mLock);
    mSecureWindowVisible = visible;
}

void InputReaderTracer::WindowListener::onWindowInfosChanged(const gui::WindowInfosUpdate& update) {
    if (mTracerDestroyed) {
        return;
    }
    // Since we trace raw events before we know what window(s) they will result in events being
    // dispatched to, we calculate the trace level for them by assuming that they will go to all
    // visible windows. So, if there is any secure window visible on the screen, we'll set isSecure
    // in the metadata.
    const bool secureWindowVisible =
            std::ranges::any_of(update.windowInfos, [](const gui::WindowInfo& info) {
                return !info.inputConfig.test(gui::WindowInfo::InputConfig::NOT_VISIBLE) &&
                        !info.inputConfig.test(gui::WindowInfo::InputConfig::NO_INPUT_CHANNEL) &&
                        info.inputConfig.test(gui::WindowInfo::InputConfig::SENSITIVE_FOR_PRIVACY);
            });
    mTracer.setSecureWindowVisible(secureWindowVisible);
}

} // namespace android
