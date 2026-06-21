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

#include <aidl/android/hardware/common/fmq/MQDescriptor.h>
#include <aidl/android/hardware/vibrator/HapticGeneratorCommand.h>
#include <aidl/android/hardware/vibrator/HapticGeneratorReply.h>
#include <aidl/android/hardware/vibrator/VibrationEffectContent.h>
#include <android-base/thread_annotations.h>
#include <fmq/AidlMessageQueue.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace android::vibrator {

// Represents a fake HAL instance for a single vibrator, running in its own thread.
class FakeVibratorHal {
public:
    using CommandQueue =
            AidlMessageQueue<aidl::android::hardware::vibrator::HapticGeneratorCommand,
                             aidl::android::hardware::common::fmq::SynchronizedReadWrite>;
    using EffectQueue =
            AidlMessageQueue<aidl::android::hardware::vibrator::VibrationEffectContent,
                             aidl::android::hardware::common::fmq::SynchronizedReadWrite>;
    using ReplyQueue =
            AidlMessageQueue<aidl::android::hardware::vibrator::HapticGeneratorReply,
                             aidl::android::hardware::common::fmq::SynchronizedReadWrite>;
    using PcmQueue =
            AidlMessageQueue<int8_t, aidl::android::hardware::common::fmq::SynchronizedReadWrite>;

    // Configuration for the fake HAL behavior
    struct Config {
        std::chrono::milliseconds fmqTimeout = std::chrono::milliseconds(50);
        size_t pcmToProduce = 10;
        /** If true, the HAL will always send a `NOT_ENOUGH_DATA` reply on `burstBytes` commands. */
        bool alwaysReplyNotEnoughData = false;
        /** If true, the HAL will not send any replies to `burstBytes` commands. */
        bool shouldNotReplyToBurst = false;
        int32_t vibratorId = 0;
    };

    FakeVibratorHal(std::shared_ptr<CommandQueue> commandMQ, std::shared_ptr<ReplyQueue> replyMQ,
                    std::shared_ptr<EffectQueue> effectMQ, std::shared_ptr<PcmQueue> pcmMQ,
                    const Config& config);
    ~FakeVibratorHal();

    void start();
    std::vector<aidl::android::hardware::vibrator::HapticGeneratorCommand> getRecordedCommands();

private:
    void run();
    void recordCommand(const aidl::android::hardware::vibrator::HapticGeneratorCommand& command);
    void handleEffectCommand(
            const aidl::android::hardware::vibrator::HapticGeneratorCommand& command);
    void handleBurstCommand(aidl::android::hardware::vibrator::HapticGeneratorReply& reply);
    void handleSessionCommand(
            const aidl::android::hardware::vibrator::HapticGeneratorCommand& command);

    std::shared_ptr<CommandQueue> mCommandMQ;
    std::shared_ptr<ReplyQueue> mReplyMQ;
    std::shared_ptr<EffectQueue> mEffectMQ;
    std::shared_ptr<PcmQueue> mPcmMQ;
    Config mConfig;

    std::thread mThread;
    std::atomic<bool> mStopThread{false};
    std::atomic<bool> mEffectComplete{false};

    std::mutex mMutex;
    std::vector<aidl::android::hardware::vibrator::HapticGeneratorCommand> mRecordedCommands
            GUARDED_BY(mMutex);
};

} // namespace android::vibrator
