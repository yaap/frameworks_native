/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <gtest/gtest.h>
#include <gui/LayerState.h>

namespace android {

TEST(LayerStateTest, SanitizeCompositionFilterFlag) {
    layer_state_t state;

    // Test unprivileged caller
    state.what = layer_state_t::eCompositionFilterFlagChanged;
    state.compositionFilterFlag = 0x1;
    state.sanitize(0 /* permissions */);
    EXPECT_FALSE(state.what & layer_state_t::eCompositionFilterFlagChanged);

    // Test privileged caller
    state.what = layer_state_t::eCompositionFilterFlagChanged;
    state.compositionFilterFlag = 0x1;
    state.sanitize(layer_state_t::Permission::ACCESS_SURFACE_FLINGER);
    EXPECT_TRUE(state.what & layer_state_t::eCompositionFilterFlagChanged);
    EXPECT_EQ(0x1u, state.compositionFilterFlag);
}

} // namespace android
