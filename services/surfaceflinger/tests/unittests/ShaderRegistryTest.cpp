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

#undef LOG_TAG
#define LOG_TAG "ShaderRegistryTest"

#include <binder/Binder.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <log/log.h>

#include "ShaderRegistry.h"

namespace android::surfaceflinger {

class ShaderRegistryTest : public testing::Test {
protected:
    sp<ShaderRegistry> mShaderRegistry = sp<ShaderRegistry>::make();
    sp<BBinder> mToken = sp<BBinder>::make();
};

TEST_F(ShaderRegistryTest, RegisterAndGetShader) {
    // Simple pass-through shader
    std::string shaderString = R"(
        vec4 main(vec2 canvas_coords) {
            return vec4(1.0, 0.0, 0.0, 1.0);
        }
    )";

    mShaderRegistry->registerShader(mToken, "TestShader", shaderString);
    auto effect = mShaderRegistry->getShader(mToken);
    EXPECT_NE(effect, nullptr);
}

TEST_F(ShaderRegistryTest, RegisterInvalidShader) {
    std::string invalidShader = "invalid syntax";
    mShaderRegistry->registerShader(mToken, "InvalidShader", invalidShader);
    auto effect = mShaderRegistry->getShader(mToken);
    EXPECT_EQ(effect, nullptr);
}

TEST_F(ShaderRegistryTest, UnregisterShader) {
    std::string shaderString = R"(
        vec4 main(vec2 canvas_coords) {
            return vec4(1.0);
        }
    )";

    mShaderRegistry->registerShader(mToken, "TestShader", shaderString);
    EXPECT_NE(mShaderRegistry->getShader(mToken), nullptr);

    mShaderRegistry->unregisterShader(mToken);
    EXPECT_EQ(mShaderRegistry->getShader(mToken), nullptr);
}

TEST_F(ShaderRegistryTest, BinderDied) {
    std::string shaderString = R"(
        vec4 main(vec2 canvas_coords) {
            return vec4(1.0);
        }
    )";

    mShaderRegistry->registerShader(mToken, "TestShader", shaderString);
    EXPECT_NE(mShaderRegistry->getShader(mToken), nullptr);

    // Simulate binder death by manually calling binderDied
    // In a real scenario, this would be triggered by the binder driver
    mShaderRegistry->binderDied(mToken);

    EXPECT_EQ(mShaderRegistry->getShader(mToken), nullptr);
}

TEST_F(ShaderRegistryTest, MultipleShaders) {
    sp<BBinder> token1 = sp<BBinder>::make();
    sp<BBinder> token2 = sp<BBinder>::make();

    std::string shaderString = R"(
        vec4 main(vec2 canvas_coords) {
            return vec4(1.0);
        }
    )";

    mShaderRegistry->registerShader(token1, "Shader1", shaderString);
    mShaderRegistry->registerShader(token2, "Shader2", shaderString);

    EXPECT_NE(mShaderRegistry->getShader(token1), nullptr);
    EXPECT_NE(mShaderRegistry->getShader(token2), nullptr);

    mShaderRegistry->unregisterShader(token1);

    EXPECT_EQ(mShaderRegistry->getShader(token1), nullptr);
    EXPECT_NE(mShaderRegistry->getShader(token2), nullptr);
}

} // namespace android::surfaceflinger
