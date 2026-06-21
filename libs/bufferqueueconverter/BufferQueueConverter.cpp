/*
 * Copyright 2020 The Android Open Source Project
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

#include <gui/Surface.h>

#include "include/bufferqueueconverter/BufferQueueConverter.h"


using ::android::Surface;
using ::android::IGraphicBufferProducer;


namespace android {

struct SurfaceHolder {
    sp<Surface> surface;
    SurfaceHolder(const sp<Surface>& s) : surface(s) {}
};

/**
 * Custom deleter for SurfaceHolder unique pointer
 */
void destroySurfaceHolder(SurfaceHolder* surfaceHolder) {
    delete surfaceHolder;
}

sp<ANativeWindow> getNativeWindowFromHGBP(const sp<HGraphicBufferProducer>& token) {
    if (token == nullptr) {
        ALOGE("Passed IGraphicBufferProducer handle is invalid.");
        return nullptr;
    }
    return Surface::fromHidl(token);
}

SurfaceHolderUniquePtr getSurfaceFromHGBP(const sp<HGraphicBufferProducer>& token) {
    sp<ANativeWindow> surface = getNativeWindowFromHGBP(token);
    if (surface == nullptr) {
        return SurfaceHolderUniquePtr(nullptr, nullptr);
    }

    sp<Surface> s = sp<Surface>::fromExisting(static_cast<Surface*>(surface.get()));
    return SurfaceHolderUniquePtr(new SurfaceHolder(s), destroySurfaceHolder);
}

ANativeWindow* getNativeWindow(SurfaceHolder* handle) {
    if (handle == nullptr) {
        ALOGE("SurfaceHolder is invalid.");
        return nullptr;
    }

    return static_cast<ANativeWindow*>(handle->surface.get());
}

} // namespace android
