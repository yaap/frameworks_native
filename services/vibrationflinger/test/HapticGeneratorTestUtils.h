/*
 * Copyright 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may not use a copy of the License at
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

#include <aidl/android/hardware/vibrator/HapticGeneratorCommand.h>
#include <aidl/android/hardware/vibrator/PredefinedEffect.h>
#include <aidl/android/hardware/vibrator/VibrationEffectContent.h>
#include <gtest/gtest.h>
#include <vector>

namespace android::vibrator {
class HapticGeneratorTestUtils {
    using HapticGeneratorCommand = aidl::android::hardware::vibrator::HapticGeneratorCommand;
    using VibrationEffectContent = aidl::android::hardware::vibrator::VibrationEffectContent;
    using PredefinedEffect = aidl::android::hardware::vibrator::PredefinedEffect;
    using Effect = aidl::android::hardware::vibrator::Effect;

private:
    HapticGeneratorTestUtils() = delete;

    static HapticGeneratorCommand createEffectCommand(HapticGeneratorCommand::Effect effect) {
        HapticGeneratorCommand cmd;
        cmd.set<HapticGeneratorCommand::Tag::effect>(effect);
        return cmd;
    }

    static HapticGeneratorCommand createSessionCommand(HapticGeneratorCommand::Session session) {
        HapticGeneratorCommand cmd;
        cmd.set<HapticGeneratorCommand::Tag::session>(session);
        return cmd;
    }

    static HapticGeneratorCommand createBurstCommand(int32_t size) {
        HapticGeneratorCommand cmd;
        cmd.set<HapticGeneratorCommand::Tag::burstBytes>(size);
        return cmd;
    }

public:
    static inline const HapticGeneratorCommand kStartCmd =
            createEffectCommand(HapticGeneratorCommand::Effect::START);
    static inline const HapticGeneratorCommand kCompleteCmd =
            createEffectCommand(HapticGeneratorCommand::Effect::COMPLETE);
    static inline const HapticGeneratorCommand kCancelCmd =
            createEffectCommand(HapticGeneratorCommand::Effect::CANCEL);
    static inline const HapticGeneratorCommand kSessionCloseCmd =
            createSessionCommand(HapticGeneratorCommand::Session::CLOSE);
    static inline const HapticGeneratorCommand kDefaultBurstCmd = createBurstCommand(1024);

    static VibrationEffectContent createPredefinedEffect(Effect effectId) {
        VibrationEffectContent effect;
        effect.set<VibrationEffectContent::Tag::predefined>(PredefinedEffect{.effect = effectId});
        return effect;
    }

    static void assertCommandSequence(const std::vector<HapticGeneratorCommand>& actual,
                                      const std::vector<HapticGeneratorCommand>& expected) {
        ASSERT_EQ(expected.size(), actual.size()) << "Command sequence length mismatch";
        for (size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(expected[i].getTag(), actual[i].getTag())
                    << "Tag mismatch at command index " << i;

            if (expected[i].getTag() == HapticGeneratorCommand::Tag::effect) {
                EXPECT_EQ(expected[i].get<HapticGeneratorCommand::Tag::effect>(),
                          actual[i].get<HapticGeneratorCommand::Tag::effect>())
                        << "Effect command mismatch at index " << i;
            }
        }
    }
};
} // namespace android::vibrator
