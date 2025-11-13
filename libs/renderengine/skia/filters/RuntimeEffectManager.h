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

#include <android-base/thread_annotations.h>
#include <ftl/enum.h>
#include <math/mat4.h>

#include <cstddef>

#include <shaders/shaders.h>
#include "SkRuntimeEffect.h"
#include "SkShader.h"

namespace android {
namespace renderengine {
namespace skia {

/**
 * Centralized location for sharing access to SkRuntimeEffects between rendering and precompilation.
 *
 * Note: data that is lazily-initialized (e.g. SkRuntimeEffects for LinearEffects) must be
 * thread-safe, otherwise the rendering and precompilation threads may race.
 */
class RuntimeEffectManager {
public:
    // Ordered alphabetically by overall filter, then by declaration order for filters with multiple
    // effects.
    // TODO(b/380159947): use macros to generate enum/name pair
    enum class KnownId : size_t {
        kBlurFilter_MixEffect,
        kEdgeExtensionEffect,
        kGainmapEffect,
        kKawaseBlurDualFilter_LowSampleBlurEffect,
        kKawaseBlurDualFilter_HighSampleBlurEffect,
        kKawaseBlurEffect,
        kLutEffect,
        kMouriMap_CrossTalkAndChunk16x16Effect,
        kMouriMap_Chunk8x8Effect,
        kMouriMap_BlurEffect,
        kMouriMap_TonemapEffect,
        kStretchEffect,

        kLast,
    };

    // Fatal error if a RuntimeEffect has already been created/stored for effectId, or if
    // RuntimeEffect compilation fails.
    // TODO(b/380159947): use macros to generate enum/name pair
    sk_sp<SkRuntimeEffect> createAndStoreRuntimeEffect(KnownId effectId,
                                                       const std::string& effectName,
                                                       const SkString& effectSkSL);
    // Fatal error if a RuntimeEffect has not been created/stored yet for effectId.
    sk_sp<SkRuntimeEffect> getKnownRuntimeEffect(KnownId effectId);

    sk_sp<SkRuntimeEffect> getOrCreateLinearRuntimeEffect(const shaders::LinearEffect& linearEffect)
            EXCLUDES(mMutex);

    void dump(std::string& result) EXCLUDES(mMutex);

    // Generates a shader resulting from applying a linear effect shader (created according to the
    // given LinearEffect settings struct) to the given inputShader.
    //
    // Optionally, a color transform may also be provided, which combines with the
    // matrix transforming from linear XYZ to linear RGB immediately before OETF.
    // We also provide additional HDR metadata upon creating the shader:
    // * The max display luminance is the max luminance of the physical display in nits
    // * The current luminance of the physical display in nits
    // * The max luminance is provided as the max luminance for the buffer, either from the SMPTE
    // 2086 or as the max light level from the CTA 861.3 standard.
    // * An AHardwareBuffer for implementations that support gralloc4 metadata for
    // communicating any HDR metadata.
    // * A RenderIntent that communicates the downstream renderintent for a physical display, for
    // image quality compensation.
    static sk_sp<SkShader> createLinearEffectShader(
            sk_sp<SkShader> inputShader, const shaders::LinearEffect& linearEffect,
            sk_sp<SkRuntimeEffect> runtimeEffect, const mat4& colorTransform,
            float maxDisplayLuminance, float currentDisplayLuminanceNits, float maxLuminance,
            AHardwareBuffer* buffer,
            aidl::android::hardware::graphics::composer3::RenderIntent renderIntent);

private:
    static sk_sp<SkRuntimeEffect> buildLinearRuntimeEffect(
            const shaders::LinearEffect& linearEffect);

    std::mutex mMutex;
    std::array<sk_sp<SkRuntimeEffect>, static_cast<size_t>(KnownId::kLast)> mKnownRuntimeEffects{};
    std::unordered_map<shaders::LinearEffect, sk_sp<SkRuntimeEffect>, shaders::LinearEffectHasher>
            mLinearEffectMap GUARDED_BY(mMutex);
};

} // namespace skia
} // namespace renderengine
} // namespace android
