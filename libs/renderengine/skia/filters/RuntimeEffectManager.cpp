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

#include "RuntimeEffectManager.h"

#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <SkRuntimeEffect.h>
#include <SkString.h>
#include <android-base/stringprintf.h>
#include <common/trace.h>
#include <ftl/concat.h>
#include <log/log.h>
#include <math/mat4.h>
#include <shaders/shaders.h>
#include <ui/DebugUtils.h>
#include <mutex>

namespace android {
namespace renderengine {
namespace skia {

using base::StringAppendF;

sk_sp<SkRuntimeEffect> RuntimeEffectManager::createAndStoreRuntimeEffect(
        KnownId effectId, const std::string& effectName, const SkString& effectSkSL) {
    SFTRACE_CALL();

    auto effectIdValue = static_cast<size_t>(effectId);
    LOG_ALWAYS_FATAL_IF(mKnownRuntimeEffects[effectIdValue],
                        "RuntimeEffect already created for ID:%zu", effectIdValue);

    std::string name = "RE_" + effectName;
    SkRuntimeEffect::Options options;
    options.fName = name;
    auto [runtimeEffect, error] = SkRuntimeEffect::MakeForShader(effectSkSL, options);
    LOG_ALWAYS_FATAL_IF(!runtimeEffect, "%s (ID:%zu) construction error: %s", name.c_str(),
                        effectIdValue, error.c_str());

    mKnownRuntimeEffects[effectIdValue] = runtimeEffect;
    return runtimeEffect;
}

sk_sp<SkRuntimeEffect> RuntimeEffectManager::getKnownRuntimeEffect(KnownId effectId) {
    auto effectIdValue = static_cast<size_t>(effectId);
    sk_sp<SkRuntimeEffect> runtimeEffect = mKnownRuntimeEffects[effectIdValue];
    LOG_ALWAYS_FATAL_IF(!runtimeEffect, "RuntimeEffect for ID:%zu has not been created yet",
                        effectIdValue);
    return runtimeEffect;
}

sk_sp<SkRuntimeEffect> RuntimeEffectManager::getOrCreateLinearRuntimeEffect(
        const shaders::LinearEffect& linearEffect) {
    SFTRACE_CALL();
    std::lock_guard lock(mMutex);

    auto effectIter = mLinearEffectMap.find(linearEffect);
    if (effectIter == mLinearEffectMap.end()) {
        sk_sp<SkRuntimeEffect> runtimeEffect = buildLinearRuntimeEffect(linearEffect);
        mLinearEffectMap.insert({linearEffect, runtimeEffect});
        return runtimeEffect;
    } else {
        return effectIter->second;
    }
}

std::string variableNameSafeLinearEffectString(const shaders::LinearEffect& effect) {
    // May only contain alphanumeric characters and underscores. Double underscoress are used to
    // delineate fields, since dataspace names may contain their own single underscores.
    return toString(effect.inputDataspace) + "__" + toString(effect.outputDataspace) + "__" +
            (effect.undoPremultipliedAlpha ? "true" : "false") + "__" +
            toString(effect.fakeOutputDataspace) + "__" +
            (effect.type == shaders::LinearEffect::SkSLType::Shader ? "Shader" : "ColorFilter");
}

// Wrapper around existing toString(ui::Dataspace) that tweaks the result to make it copy/pasteable
// into struct initialization code.
std::string dataspaceEnumString(ui::Dataspace dataspace) {
    const std::string dataspaceStr = toString(dataspace);
    if (dataspaceStr.starts_with("0x")) {
        return "static_cast<ui::Dataspace>(" + dataspaceStr + ")";
    } else {
        return "ui::Dataspace::" + dataspaceStr;
    }
}

void RuntimeEffectManager::dump(std::string& result) {
    // LinearEffects are ordered (by hash value) when dumped to reduce churn when iterating on the
    // set of precompiled effects.
    std::vector<shaders::LinearEffect> orderedLinearEffects;
    {
        // Note: rework this locking if other guarded state is dumped in the future!
        std::lock_guard lock(mMutex);
        orderedLinearEffects = std::vector<shaders::LinearEffect>(mLinearEffectMap.size());
        for (const auto& [linearEffect, _] : mLinearEffectMap) {
            orderedLinearEffects.push_back(linearEffect);
        }
    }
    std::sort(orderedLinearEffects.begin(), orderedLinearEffects.end(), [](auto& lhs, auto& rhs) {
        auto hasher = shaders::LinearEffectHasher();
        return hasher(lhs) < hasher(rhs);
    });

    StringAppendF(&result, "RenderEngine LinearEffects (RuntimeEffects): %zu\n",
                  orderedLinearEffects.size());
    for (const auto& linearEffect : orderedLinearEffects) {
        // Note: the formatting of this output should remain copy/pasteable C++
        StringAppendF(&result, "// %s\n", variableNameSafeLinearEffectString(linearEffect).c_str());
        StringAppendF(&result, "{\n");
        StringAppendF(&result, "    .inputDataspace = %s, // %s\n",
                      dataspaceEnumString(linearEffect.inputDataspace).c_str(),
                      dataspaceDetails(static_cast<android_dataspace>(linearEffect.inputDataspace))
                              .c_str());
        StringAppendF(&result, "    .outputDataspace = %s, // %s\n",
                      dataspaceEnumString(linearEffect.outputDataspace).c_str(),
                      dataspaceDetails(static_cast<android_dataspace>(linearEffect.outputDataspace))
                              .c_str());
        StringAppendF(&result, "    .undoPremultipliedAlpha = %s,\n",
                      linearEffect.undoPremultipliedAlpha ? "true" : "false");
        StringAppendF(&result, "    .fakeOutputDataspace = %s, // %s\n",
                      dataspaceEnumString(linearEffect.fakeOutputDataspace).c_str(),
                      dataspaceDetails(
                              static_cast<android_dataspace>(linearEffect.fakeOutputDataspace))
                              .c_str());
        StringAppendF(&result, "    .type = shaders::LinearEffect::SkSLType::%s,\n",
                      linearEffect.type == shaders::LinearEffect::SkSLType::Shader ? "Shader"
                                                                                   : "ColorFilter");
        StringAppendF(&result, "},\n");
    }
}

sk_sp<SkRuntimeEffect> RuntimeEffectManager::buildLinearRuntimeEffect(
        const shaders::LinearEffect& linearEffect) {
    SFTRACE_CALL();
    SkString shaderString = SkString(shaders::buildLinearEffectSkSL(linearEffect));

    std::string name = "RE_LinearEffect_" + variableNameSafeLinearEffectString(linearEffect);
    SkRuntimeEffect::Options options;
    options.fName = name;
    auto [shader, error] = SkRuntimeEffect::MakeForShader(shaderString, options);
    if (!shader) {
        LOG_ALWAYS_FATAL("LinearColorFilter construction error: %s", error.c_str());
    }
    return shader;
}

sk_sp<SkShader> RuntimeEffectManager::createLinearEffectShader(
        sk_sp<SkShader> shader, const shaders::LinearEffect& linearEffect,
        sk_sp<SkRuntimeEffect> runtimeEffect, const mat4& colorTransform, float maxDisplayLuminance,
        float currentDisplayLuminanceNits, float maxLuminance, AHardwareBuffer* buffer,
        aidl::android::hardware::graphics::composer3::RenderIntent renderIntent) {
    SFTRACE_CALL();
    SkRuntimeShaderBuilder effectBuilder(runtimeEffect);

    effectBuilder.child("child") = shader;

    const auto uniforms =
            shaders::buildLinearEffectUniforms(linearEffect, colorTransform, maxDisplayLuminance,
                                               currentDisplayLuminanceNits, maxLuminance, buffer,
                                               renderIntent);

    for (const auto& uniform : uniforms) {
        effectBuilder.uniform(uniform.name.c_str()).set(uniform.value.data(), uniform.value.size());
    }

    return effectBuilder.makeShader();
}

} // namespace skia
} // namespace renderengine
} // namespace android
