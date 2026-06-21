/*
 * Copyright 2024 The Android Open Source Project
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

#include <include/android/AHardwareBufferUtils.h>
#include <include/core/SkColorSpace.h>
#include <include/gpu/ganesh/GrDirectContext.h>
#include <renderengine/ColorSpaces.h>

#include <android/hardware_buffer.h>
#include <ui/GraphicTypes.h>

namespace android::renderengine::skia {

/**
 * Abstraction over a Skia backend-specific texture type.
 *
 * This class does not do any lifecycle management, and should typically be wrapped in an
 * AutoBackendTexture::LocalRef. Typically created via SkiaGpuContext::makeBackendTexture(...).
 */
class SkiaBackendTexture {
public:
    SkiaBackendTexture(bool isOutputBuffer)  : mIsOutputBuffer(isOutputBuffer) {}
    virtual ~SkiaBackendTexture() = default;

    // These two definitions mirror Skia's own types used for texture release callbacks, which are
    // re-declared multiple times between context-specific implementation headers for Ganesh vs.
    // Graphite, and within the context of SkImages vs. SkSurfaces. Our own re-declaration allows us
    // to not pull in any implementation-specific headers here.
    using ReleaseContext = void*;
    using TextureReleaseProc = void (*)(ReleaseContext);

    // Guaranteed to be non-null (crashes otherwise). An opaque alphaType may coerce the internal
    // color type to RBGX.
    virtual sk_sp<SkImage> makeImage(
            SkAlphaType alphaType, ui::Dataspace dataspace, TextureReleaseProc releaseImageProc,
            ReleaseContext releaseContext,
            ftl::Flags<ColorSpaceOptions> options = ColorSpaceOptions::None) = 0;

    // Guaranteed to be non-null (crashes otherwise).
    virtual sk_sp<SkSurface> makeSurface(
            ui::Dataspace dataspace, TextureReleaseProc releaseSurfaceProc,
            ReleaseContext releaseContext,
            ftl::Flags<ColorSpaceOptions> options = ColorSpaceOptions::None) = 0;

    bool isOutputBuffer() const { return mIsOutputBuffer; }

    std::string toString() const {
        return std::format("isOutputBuffer={}, {}", mIsOutputBuffer, backendDebugInfo().c_str());
    }

protected:
    virtual std::string backendDebugInfo() const = 0;

private:
    const bool mIsOutputBuffer;
};

} // namespace android::renderengine::skia
