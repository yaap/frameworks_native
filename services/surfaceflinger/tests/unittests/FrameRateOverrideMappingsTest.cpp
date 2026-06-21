/*
 * Copyright 2022 The Android Open Source Project
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
#define LOG_TAG "FrameRateOverrideMappingsTest"

#include <com_android_graphics_surfaceflinger_flags.h>
#include <common/test/FlagUtils.h>
#include <gtest/gtest.h>
#include <unordered_map>

#include "Scheduler/FrameRateOverrideMappings.h"

namespace android::scheduler {

using FrameRateOverride = DisplayEventReceiver::Event::FrameRateOverride;
using UidToFrameRateOverride = std::map<uid_t, Fps>;

class FrameRateOverrideMappingsTest : public testing::Test {
protected:
    FrameRateOverrideMappings mFrameRateOverrideMappings;
    UidToFrameRateOverride mFrameRateOverrideByContent;
};

namespace {
using namespace com::android::graphics::surfaceflinger;

TEST_F(FrameRateOverrideMappingsTest, testUpdateFrameRateOverridesByContent) {
    mFrameRateOverrideByContent.clear();
    mFrameRateOverrideByContent.emplace(0, 30.0_Hz);
    mFrameRateOverrideByContent.emplace(1, 60.0_Hz);
    ASSERT_TRUE(mFrameRateOverrideMappings.updateFrameRateOverridesByContent(
            mFrameRateOverrideByContent));

    ASSERT_TRUE(isApproxEqual(30.0_Hz,
                              *mFrameRateOverrideMappings.getFrameRateOverrideForUid(
                                      0, /*supportsFrameRateOverrideByContent*/ true)));
    ASSERT_TRUE(isApproxEqual(60.0_Hz,
                              *mFrameRateOverrideMappings.getFrameRateOverrideForUid(
                                      1, /*supportsFrameRateOverrideByContent*/ true)));
    ASSERT_EQ(std::nullopt,
              mFrameRateOverrideMappings
                      .getFrameRateOverrideForUid(1, /*supportsFrameRateOverrideByContent*/ false));
    ASSERT_EQ(std::nullopt,
              mFrameRateOverrideMappings
                      .getFrameRateOverrideForUid(3, /*supportsFrameRateOverrideByContent*/ true));
    ASSERT_EQ(std::nullopt,
              mFrameRateOverrideMappings
                      .getFrameRateOverrideForUid(3, /*supportsFrameRateOverrideByContent*/ false));
}

TEST_F(FrameRateOverrideMappingsTest, testSetPreferredRefreshRateForUid) {
    mFrameRateOverrideMappings.setPreferredRefreshRateForUid({0, 60.0f});
    mFrameRateOverrideMappings.setPreferredRefreshRateForUid({2, 120.0f});

    ASSERT_TRUE(isApproxEqual(60.0_Hz,
                              *mFrameRateOverrideMappings.getFrameRateOverrideForUid(
                                      0, /*supportsFrameRateOverrideByContent*/ true)));
    ASSERT_TRUE(isApproxEqual(120.0_Hz,
                              *mFrameRateOverrideMappings.getFrameRateOverrideForUid(
                                      2, /*supportsFrameRateOverrideByContent*/ false)));
    ASSERT_EQ(std::nullopt,
              mFrameRateOverrideMappings
                      .getFrameRateOverrideForUid(1, /*supportsFrameRateOverrideByContent*/ true));
    ASSERT_EQ(std::nullopt,
              mFrameRateOverrideMappings
                      .getFrameRateOverrideForUid(1, /*supportsFrameRateOverrideByContent*/ false));
}
} // namespace
} // namespace android::scheduler
