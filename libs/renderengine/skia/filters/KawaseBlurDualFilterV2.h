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

#include <SkCanvas.h>
#include <SkImage.h>
#include <SkRuntimeEffect.h>
#include <SkSurface.h>
#include "BlurFilter.h"

#include "../AutoBackendTexture.h"
#include "RuntimeEffectManager.h"
#include "ui/Size.h"

namespace android {
namespace renderengine {
namespace skia {

using android::hardware::graphics::common::V1_2::BufferUsage;

/**
 * This is an implementation of a Kawase blur with dual-filtering passes, as described in here:
 * https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/siggraph2015_2D00_mmg_2D00_marius_2D00_slides.pdf
 * https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/siggraph2015_2D00_mmg_2D00_marius_2D00_notes.pdf
 * It uses fixed blur radii in the downsampling passes to minimize aliasing, and dynamic radii
 * in the upsampling passes to produce sufficiently smooth blurs and support animation.
 */
class KawaseBlurDualFilterV2 : public BlurFilter {
public:
    static constexpr int kMaxSurfaces = 4;
    static constexpr float kInverseInputScale = 1.0f / kInputScale;
    static constexpr float kScales[kMaxSurfaces] = {1 * kInverseInputScale, 2 * kInverseInputScale,
                                                    4 * kInverseInputScale, 8 * kInverseInputScale};

    explicit KawaseBlurDualFilterV2(RuntimeEffectManager& effectManager);
    virtual ~KawaseBlurDualFilterV2() {}

    // Execute blur, saving it to a texture
    sk_sp<SkImage> generateTemporaryImage(SkiaGpuContext* context, const DisplaySettings& display,
                                          const uint32_t radius, const sk_sp<SkImage> blurInput,
                                          const SkRect& blurRect) const override;

    bool areBuffersPreallocated(const SkiaGpuContext* context, ui::Size displaySize) const override;
    // Note: the buffer preallocation code path is never enabled for the unprotected context on
    // GaneshGL due to performance reasons (nuance around texture copying logic for wrapped AHBs).
    // It can be enabled for the protected context on GaneshGL for memory savings, as protected
    // memory is extremely limited.
    void preallocateBuffers(SkiaGpuContext* context, ui::Size size) override;

private:
    static constexpr uint64_t kUnprotectedUsageFlags =
            BufferUsage::GPU_RENDER_TARGET | BufferUsage::GPU_TEXTURE;
    static constexpr uint64_t kProtectedUsageFlags =
            kUnprotectedUsageFlags | BufferUsage::PROTECTED;

    sk_sp<SkRuntimeEffect> mQuarterResDownSampleBlurEffect;
    sk_sp<SkRuntimeEffect> mHalfResDownSampleBlurEffect;
    sk_sp<SkRuntimeEffect> mUpSampleBlurEffect;

    AutoBackendTexture::CleanupManager mTextureCleanupMgr GUARDED_BY(mRenderingMutex);
    // Mutex guarding rendering operations, so that internal state related to
    // rendering that is potentially modified by multiple threads is guaranteed thread-safe.
    mutable std::mutex mRenderingMutex;
    std::shared_ptr<AutoBackendTexture::LocalRef> mUnprotectedTextures[kMaxSurfaces];
    std::shared_ptr<AutoBackendTexture::LocalRef> mProtectedTextures[kMaxSurfaces];
    ui::Size mUnprotectedDisplaySize;
    ui::Size mProtectedDisplaySize;

    void blurInto(const sk_sp<SkSurface>& drawSurface, const int destWidth,
                  const sk_sp<SkImage>& readImage, const SkIRect& srcRect,
                  const bool inputEdgesNeedClamp, const float radius, const float alpha,
                  const sk_sp<SkRuntimeEffect>&) const;

    void blurInto(const sk_sp<SkSurface>& drawSurface, const sk_sp<SkShader> input,
                  const SkIRect& srcRect, const SkMatrix& srcRectToInputMatrix,
                  const bool inputEdgesNeedClamp, const float radius, const float alpha,
                  const sk_sp<SkRuntimeEffect>&) const;
};

} // namespace skia
} // namespace renderengine
} // namespace android
