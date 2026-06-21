/*
 * Copyright 2026 The Android Open Source Project
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

#include "ShaderRegistry.h"
#include <SkString.h>
#include <log/log.h>

namespace android::surfaceflinger {

void ShaderRegistry::binderDied(const wp<IBinder>& who) {
    std::scoped_lock lock(mMutex);
    mRegistry.erase(who);
}

bool ShaderRegistry::registerShader(const sp<IBinder>& shaderToken,
                                    const std::string& uniqueShaderName,
                                    const std::string& shaderString) {
    if (!shaderToken) {
        ALOGE("Attempted to register shader with null token");
        return false;
    }
    SkRuntimeEffect::Options options;
    options.fName = uniqueShaderName;
    auto [effect, error] = SkRuntimeEffect::MakeForShader(SkString(shaderString), options);
    if (!effect) {
        ALOGE("Failed to create SkRuntimeEffect: %s", error.c_str());
        return false;
    }

    shaderToken->linkToDeath(sp<DeathRecipient>::fromExisting(this));

    std::scoped_lock lock(mMutex);
    mRegistry.emplace(shaderToken, RegisteredShader{effect, uniqueShaderName});
    return true;
}

void ShaderRegistry::unregisterShader(const sp<IBinder>& shaderToken) {
    if (!shaderToken) return;

    shaderToken->unlinkToDeath(sp<DeathRecipient>::fromExisting(this));

    std::scoped_lock lock(mMutex);
    mRegistry.erase(shaderToken);
}

sk_sp<SkRuntimeEffect> ShaderRegistry::getShader(const sp<IBinder>& shaderToken) {
    if (!shaderToken) return nullptr;

    std::scoped_lock lock(mMutex);
    auto it = mRegistry.find(shaderToken);
    if (it != mRegistry.end()) {
        return it->second.effect;
    }
    return nullptr;
}

} // namespace android::surfaceflinger