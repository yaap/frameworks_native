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

#pragma once

#include <SkRuntimeEffect.h>
#include <android-base/thread_annotations.h>
#include <binder/IBinder.h>

#include <mutex>
#include <unordered_map>

#include "WpHash.h"

namespace android::surfaceflinger {

class ShaderRegistry : public IBinder::DeathRecipient {
public:
    // Override for IBinder::DeathRecipient
    void binderDied(const wp<IBinder>&) override EXCLUDES(mMutex);

    bool registerShader(const sp<IBinder>& shaderToken, const std::string& uniqueShaderName,
                        const std::string& shaderString);
    void unregisterShader(const sp<IBinder>& shaderToken);
    sk_sp<SkRuntimeEffect> getShader(const sp<IBinder>& shaderToken);

private:
    // Force usage of sp<ShaderRegistry>::make()
    ShaderRegistry() = default;
    friend class sp<ShaderRegistry>;

    mutable std::mutex mMutex;

    struct RegisteredShader {
        sk_sp<SkRuntimeEffect> effect;
        std::string uniqueShaderName;
    };

    std::unordered_map<wp<IBinder>, RegisteredShader, WpHash> mRegistry GUARDED_BY(mMutex);
};

} // namespace android::surfaceflinger
