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
#include <aidl/android/hardware/vibrator/HapticGeneratorReply.h>
#include <aidl/android/hardware/vibrator/VibrationEffectContent.h>
#include <fmq/AidlMessageQueue.h>

#include <memory>

namespace android::vibrator {

/**
 * Data structure holding the Fast Message Queues for a single vibrator
 * within a session.
 */
struct VibratorQueues {
    std::shared_ptr<android::AidlMessageQueue<
            aidl::android::hardware::vibrator::HapticGeneratorCommand,
            ::aidl::android::hardware::common::fmq::SynchronizedReadWrite>>
            command;
    std::shared_ptr<android::AidlMessageQueue<
            aidl::android::hardware::vibrator::HapticGeneratorReply,
            ::aidl::android::hardware::common::fmq::SynchronizedReadWrite>>
            reply;
    std::shared_ptr<android::AidlMessageQueue<
            aidl::android::hardware::vibrator::VibrationEffectContent,
            ::aidl::android::hardware::common::fmq::SynchronizedReadWrite>>
            effect;
    std::shared_ptr<android::AidlMessageQueue<
            int8_t, ::aidl::android::hardware::common::fmq::SynchronizedReadWrite>>
            pcm;
};

} // namespace android::vibrator
