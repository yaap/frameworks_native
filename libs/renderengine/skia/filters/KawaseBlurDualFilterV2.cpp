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

#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "KawaseBlurDualFilterV2.h"

#include <SkAlphaType.h>
#include <SkBlendMode.h>
#include <SkCanvas.h>
#include <SkColorType.h>
#include <SkData.h>
#include <SkImage.h>
#include <SkImageInfo.h>
#include <SkPaint.h>
#include <SkRRect.h>
#include <SkRect.h>
#include <SkRuntimeEffect.h>
#include <SkShader.h>
#include <SkSize.h>
#include <SkString.h>
#include <SkSurface.h>
#include <SkTileMode.h>
#include <common/FlagManager.h>
#include <common/ThreadStateCrashLogger.h>
#include <common/trace.h>
#include <include/gpu/GpuTypes.h>
#include <include/gpu/ganesh/SkSurfaceGanesh.h>
#include <log/log.h>

#include "RuntimeEffectManager.h"
#include "renderengine/RenderEngine.h"

namespace android {
namespace renderengine {
namespace skia {

// Samples each vertex of a diamond using a total of 4 samples.
// This shader is used for the initial 4x down-sampling pass, with the sampling offset
// handpicked to minimize aliasing.

const SkString kEffectSource_KawaseBlurDualFilterV2_QuarterResDownSampleBlurEffect(R"(
    uniform shader child;

    const float2 STEP_0 = float2( 0.25, 0.25);
    const float2 STEP_1 = float2( 0.25, -0.25);
    const float2 STEP_2 = float2(-0.25, -0.25);
    const float2 STEP_3 = float2(-0.25, 0.25);

    half4 main(float2 xy) {
        half3 c = child.eval(xy + STEP_0).rgb;
        c += child.eval(xy + STEP_1).rgb;
        c += child.eval(xy + STEP_2).rgb;
        c += child.eval(xy + STEP_3).rgb;

        return half4(c * 0.25, 1.0);
    }
)");

// Samples each vertex of a diamond plus the center pixel, using a total of 5 samples.
// This shader is used for the 2x down-sampling passes after the initial pass.
const SkString kEffectSource_KawaseBlurDualFilterV2_HalfResDownSampleBlurEffect(R"(
    uniform shader child;

    const float2 STEP_0 = float2( 0.5, 0.5);
    const float2 STEP_1 = float2( 0.5, -0.5);
    const float2 STEP_2 = float2(-0.5, -0.5);
    const float2 STEP_3 = float2(-0.5, 0.5);

    half4 main(float2 xy) {
        half3 c = child.eval(xy).rgb * 4.0;
        c += child.eval(xy + STEP_0).rgb;
        c += child.eval(xy + STEP_1).rgb;
        c += child.eval(xy + STEP_2).rgb;
        c += child.eval(xy + STEP_3).rgb;

        return half4(c * 0.125, 1.0);
    }
)");

// A shader to sample each vertex of a unit regular heptagon, plus the original fragment
// coordinate, using a total of 8 samples.
const SkString kEffectSource_KawaseBlurDualFilterV2_UpSampleBlurEffect(R"(
    uniform shader child;
    uniform float in_blurOffset;
    uniform float in_crossFade;
    uniform float in_weightedCrossFade;

    const float2 STEP_0 = float2( 1.0, 0.0);
    const float2 STEP_1 = float2( 0.623489802,  0.781831482);
    const float2 STEP_2 = float2(-0.222520934,  0.974927912);
    const float2 STEP_3 = float2(-0.900968868,  0.433883739);
    const float2 STEP_4 = float2( 0.900968868, -0.433883739);
    const float2 STEP_5 = float2(-0.222520934, -0.974927912);
    const float2 STEP_6 = float2(-0.623489802, -0.781831482);

    half4 main(float2 xy) {
        half3 c = child.eval(xy).rgb;

        c += child.eval(xy + STEP_0 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_1 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_2 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_3 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_4 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_5 * in_blurOffset).rgb;
        c += child.eval(xy + STEP_6 * in_blurOffset).rgb;

        return half4(c * in_weightedCrossFade, in_crossFade);
    }
)");

KawaseBlurDualFilterV2::KawaseBlurDualFilterV2(RuntimeEffectManager& effectManager)
      : BlurFilter(effectManager) {
    mQuarterResDownSampleBlurEffect =
            effectManager.mKnownEffects[kKawaseBlurDualFilterV2_QuarterResDownSampleBlurEffect];
    mHalfResDownSampleBlurEffect =
            effectManager.mKnownEffects[kKawaseBlurDualFilterV2_HalfResDownSampleBlurEffect];
    mUpSampleBlurEffect = effectManager.mKnownEffects[kKawaseBlurDualFilterV2_UpSampleBlurEffect];
}

void KawaseBlurDualFilterV2::blurInto(const sk_sp<SkSurface>& drawSurface, const int destWidth,
                                      const sk_sp<SkImage>& readImage, const SkIRect& srcRect,
                                      const bool inputEdgesNeedClamp, const float radius,
                                      const float alpha,
                                      const sk_sp<SkRuntimeEffect>& blurEffect) const {
    const float scale = static_cast<float>(destWidth) / static_cast<float>(srcRect.width());
    SkMatrix blurMatrix = SkMatrix::Scale(scale, scale);
    blurInto(drawSurface,
             readImage->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
                                   SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone)),
             srcRect, blurMatrix, inputEdgesNeedClamp, radius, alpha, blurEffect);
}

void KawaseBlurDualFilterV2::blurInto(const sk_sp<SkSurface>& drawSurface, sk_sp<SkShader> input,
                                      const SkIRect& srcRect, const SkMatrix& srcRectToInputMatrix,
                                      const bool inputEdgesNeedClamp, const float radius,
                                      const float alpha,
                                      const sk_sp<SkRuntimeEffect>& blurEffect) const {
    // Using preallocated textures that may be larger than the current blur region could lead to
    // bilinear filtering sampling beyond the desired bounds. This is prevented by insetting srcRect
    // slightly more than 0.5 texel (with a tiny epsilon to avoid rounding ambiguity on some
    // drivers. The epsilon of 0.0001 is lifted from Skia).
    if (inputEdgesNeedClamp) {
        constexpr float insetFactor = 0.5001f;
        SkRect insetRect = SkRect::Make(srcRect).makeInset(insetFactor, insetFactor);
        if (insetRect.width() < 0) {
            const float centerX = insetRect.centerX();
            insetRect.fLeft = centerX;
            insetRect.fRight = centerX;
        }
        if (insetRect.height() < 0) {
            const float centerY = insetRect.centerY();
            insetRect.fTop = centerY;
            insetRect.fBottom = centerY;
        }
        input = SkShaders::CoordClamp(input, insetRect);
    }
    input = input->makeWithLocalMatrix(srcRectToInputMatrix);

    SkPaint paint;
    if (blurEffect == mUpSampleBlurEffect) {
        if (radius == 0) {
            paint.setShader(std::move(input));
            paint.setAlphaf(alpha);
        } else {
            SkRuntimeShaderBuilder blurBuilder(blurEffect);
            blurBuilder.child("child") = std::move(input);
            if (blurEffect == mUpSampleBlurEffect) {
                blurBuilder.uniform("in_crossFade") = alpha;
                blurBuilder.uniform("in_weightedCrossFade") = alpha * 0.125f;
                blurBuilder.uniform("in_blurOffset") = radius;
            }
            paint.setShader(blurBuilder.makeShader(nullptr));
        }
    } else {
        SkRuntimeShaderBuilder blurBuilder(blurEffect);
        blurBuilder.child("child") = std::move(input);
        paint.setShader(blurBuilder.makeShader(nullptr));
    }
    paint.setBlendMode(alpha == 1.0f ? SkBlendMode::kSrc : SkBlendMode::kSrcOver);
    drawSurface->getCanvas()->drawPaint(paint);
}

sk_sp<SkImage> KawaseBlurDualFilterV2::generateTemporaryImage(SkiaGpuContext* context,
                                                              const DisplaySettings& display,
                                                              const uint32_t blurRadius,
                                                              const sk_sp<SkImage> input,
                                                              const SkRect& blurRect) const {
    // Apply a conversion factor of (1 / sqrt(3)) to match Skia's built-in blur as used by
    // RenderEffect. See the comment in SkBlurMask.cpp for reasoning behind this.
    const float radius = blurRadius * 0.57735f;

    // Use a variable number of blur passes depending on the radius. The non-integer part of this
    // calculation is used to mix the final pass into the second-last with an alpha blend.
    const float filterDepth = std::min(kMaxSurfaces - 1.0f, radius * kInputScale / 2.5f);
    const int filterPasses = std::min(kMaxSurfaces - 1, static_cast<int>(ceil(filterDepth)));

    // Ensure that no (partial) pixels outside the blurRect are included in the blur.
    SkIRect targetBlurRect;
    blurRect.roundIn(&targetBlurRect);

    auto makeSurface = [&](float scale) -> sk_sp<SkSurface> {
        const int newW =
                std::max(1, static_cast<int>(static_cast<float>(targetBlurRect.width()) / scale));
        const int newH =
                std::max(1, static_cast<int>(static_cast<float>(targetBlurRect.height()) / scale));
        sk_sp<SkSurface> surface =
                context->createRenderTarget(input->imageInfo().makeWH(newW, newH));
        LOG_THREAD_STATE_AND_CRASH_IF(!surface, "%s: Failed to create surface for blurring!",
                                      __func__);
        return surface;
    };

    auto makeImageInfo = [&](float scale) -> SkImageInfo {
        const int newW =
                std::max(1, static_cast<int>(static_cast<float>(targetBlurRect.width()) / scale));
        const int newH =
                std::max(1, static_cast<int>(static_cast<float>(targetBlurRect.height()) / scale));
        return input->imageInfo().makeWH(newW, newH);
    };

    const bool isProtected = context->supportsProtectedContent();
    bool usesPreallocatedTextures =
            FlagManager::getInstance().window_blur_kawase2_preallocate_buffers() &&
            (isProtected ||
             context->getBackend_onlyUseForCriticalWorkarounds() ==
                     RenderEngine::SkiaBackend::Graphite);
    auto& preallocatedTexturesRef = isProtected ? mProtectedTextures : mUnprotectedTextures;

    // Render into surfaces downscaled by 1x, 2x, 4x and 8x from the initial downscale.
    sk_sp<SkSurface> surfaces[kMaxSurfaces];
    SkImageInfo imageInfos[kMaxSurfaces];
    if (!usesPreallocatedTextures) {
        for (int i = 0; i < kMaxSurfaces; i++) {
            surfaces[i] = filterPasses >= i ? makeSurface(kScales[i]) : nullptr;
            imageInfos[i] = surfaces[i] != nullptr ? surfaces[i]->imageInfo() : SkImageInfo();
        }
    } else {
        for (int i = 0; i < kMaxSurfaces; i++) {
            surfaces[i] = preallocatedTexturesRef[i]->getOrCreateSurface(display.outputDataspace);
            imageInfos[i] = makeImageInfo(kScales[i]);
        }
    }

    // Kawase is an approximation of Gaussian, but behaves differently because it is made up of many
    // simpler blurs. A transformation is required to approximate the same effect as Gaussian.
    float sumSquaredR = 0;
    float sumSquaredStep = 0;
    for (int i = 0; i < filterPasses; i++) {
        const float alpha = std::min(1.0f, filterDepth - i);
        sumSquaredR += powf(powf(2.0f, i - 1) * alpha * M_SQRT2, 2.0f);
        sumSquaredStep += powf(powf(2.0f, i) * alpha, 2.0f);
    }
    // Solve for R = sqrt(sum(r_i^2)).
    float step = sqrt(max(0.0f, powf(radius * kInputScale, 2) - sumSquaredR) /
                      (sumSquaredStep == 0 ? 1 : sumSquaredStep));

    // Start by downscaling and doing the first blur pass
    {
        // For sampling Skia's API expects the inverse of what logically seems appropriate. In this
        // case one may expect Translate(blurRect.fLeft, blurRect.fTop) * Scale(kInverseInputScale)
        // but instead we must do the inverse.
        SkMatrix blurMatrix = SkMatrix::Translate(-blurRect.fLeft, -blurRect.fTop);
        blurMatrix.postScale(kInputScale, kInputScale);
        const auto sourceShader =
                input->makeShader(SkTileMode::kClamp, SkTileMode::kClamp,
                                  SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kNone));
        blurInto(surfaces[0], std::move(sourceShader), targetBlurRect, blurMatrix,
                 usesPreallocatedTextures,
                 0, // unused blur radius. The blur effect hardcodes the radius.
                 1.0f, mQuarterResDownSampleBlurEffect);
    }

    // Next the remaining downscale blur passes.
    for (int i = 0; i < filterPasses; i++) {
        blurInto(surfaces[i + 1], imageInfos[i + 1].width(), surfaces[i]->makeTemporaryImage(),
                 imageInfos[i].bounds(), usesPreallocatedTextures,
                 0, // unused blur radius. The blur effect hardcodes the radius.
                 1.0f, mHalfResDownSampleBlurEffect);
    }
    // Finally blur+upscale back to our original size.
    for (int i = filterPasses - 1; i >= 0; i--) {
        blurInto(surfaces[i], imageInfos[i].width(), surfaces[i + 1]->makeTemporaryImage(),
                 imageInfos[i + 1].bounds(), usesPreallocatedTextures, step,
                 std::min(1.0f, filterDepth - i), mUpSampleBlurEffect);
    }

    return surfaces[0]->makeTemporaryImage();
}

bool KawaseBlurDualFilterV2::areBuffersPreallocated(const SkiaGpuContext* context,
                                                    ui::Size displaySize) const {
    const bool isProtected = context->supportsProtectedContent();

    // Lie about buffers be preallocated if on the unprotected context in GaneshGL, so that callers
    // don't need to be aware of this nuance of not being supported.
    if (!isProtected &&
        context->getBackend_onlyUseForCriticalWorkarounds() == RenderEngine::SkiaBackend::Ganesh) {
        return true;
    }

    const auto& preallocatedTextures = isProtected ? mProtectedTextures : mUnprotectedTextures;
    if (preallocatedTextures[0] == nullptr) {
        return false;
    }

    const ui::Size preallocatedDisplaySize =
            isProtected ? mProtectedDisplaySize : mUnprotectedDisplaySize;
    return displaySize.width <= preallocatedDisplaySize.width &&
            displaySize.height <= preallocatedDisplaySize.height;
}

void KawaseBlurDualFilterV2::preallocateBuffers(SkiaGpuContext* context, ui::Size size) {
    SFTRACE_CALL();
    std::lock_guard<std::mutex> lock(mRenderingMutex);

    const bool isProtected = context->supportsProtectedContent();
    if (!isProtected &&
        context->getBackend_onlyUseForCriticalWorkarounds() == RenderEngine::SkiaBackend::Ganesh) {
        return;
    }

    auto& texturesRef = isProtected ? mProtectedTextures : mUnprotectedTextures;
    ui::Size& displaySizeRef = isProtected ? mProtectedDisplaySize : mUnprotectedDisplaySize;
    const uint64_t usageFlags = isProtected ? kProtectedUsageFlags : kUnprotectedUsageFlags;
    // Use the longer side as the ref size for the preallocated square, so that it can handle
    // both portrait and landscape cases.
    // TODO(b/486256401): use transform to handle screen rotation better.
    const int32_t side = std::max(size.width, size.height);
    displaySizeRef = ui::Size(side, side);
    size_t totalBytes = 0;
    for (int i = 0; i < kMaxSurfaces; i++) {
        const int newW = std::max(1, static_cast<int>(static_cast<float>(side) / kScales[i]));
        const int newH = std::max(1, static_cast<int>(static_cast<float>(side) / kScales[i]));

        sp<GraphicBuffer> buffer = sp<GraphicBuffer>::make(newW, newH, PIXEL_FORMAT_RGBA_8888, 1,
                                                           usageFlags, "KawaseBlurDualFilterV2");
        LOG_THREAD_STATE_AND_CRASH_IF(buffer->initCheck() != OK,
                                      "Failed to preallocate %s GraphicBuffer for intermediate "
                                      "blur surface: %s. %s(i:%d, %dx%x, RGBA_8888, "
                                      "usage:0x%" PRIx64 ")",
                                      isProtected ? "protected" : "unprotected",
                                      statusToString(buffer->initCheck()).c_str(),
                                      isProtected ? "Is protected memory supported? " : "", i, newW,
                                      newH, kUnprotectedUsageFlags);
        std::unique_ptr<SkiaBackendTexture> backendTexture =
                context->makeBackendTexture(buffer->toAHardwareBuffer(), true);
        texturesRef[i] = std::make_shared<AutoBackendTexture::LocalRef>(std::move(backendTexture),
                                                                        mTextureCleanupMgr);
        totalBytes += newW * newH * bytesPerPixel(PIXEL_FORMAT_RGBA_8888);
    }
    ALOGD("(Re)allocated %zu bytes of %s memory for intermediate blur surfaces (%dx%d display)",
          totalBytes, isProtected ? "protected" : "unprotected", size.width, size.height);
}

} // namespace skia
} // namespace renderengine
} // namespace android
