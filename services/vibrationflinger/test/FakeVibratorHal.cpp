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

#include "FakeVibratorHal.h"
#include <aidl/android/hardware/vibrator/HapticGeneratorCommand.h>

#include <thread>

namespace android::vibrator {

using namespace android;
using namespace std::chrono_literals;
namespace fmq = aidl::android::hardware::common::fmq;
using aidl::android::hardware::vibrator::HapticGeneratorCommand;
using aidl::android::hardware::vibrator::HapticGeneratorReply;
using aidl::android::hardware::vibrator::VibrationEffectContent;

FakeVibratorHal::FakeVibratorHal(std::shared_ptr<CommandQueue> commandMQ,
                                 std::shared_ptr<ReplyQueue> replyMQ,
                                 std::shared_ptr<EffectQueue> effectMQ,
                                 std::shared_ptr<PcmQueue> pcmMQ, const Config& config)
      : mCommandMQ(std::move(commandMQ)),
        mReplyMQ(std::move(replyMQ)),
        mEffectMQ(std::move(effectMQ)),
        mPcmMQ(std::move(pcmMQ)),
        mConfig(config) {}

FakeVibratorHal::~FakeVibratorHal() {
    mStopThread = true;
    if (mThread.joinable()) {
        mThread.join();
    }
}

void FakeVibratorHal::start() {
    mThread = std::thread(&FakeVibratorHal::run, this);
}

std::vector<HapticGeneratorCommand> FakeVibratorHal::getRecordedCommands() {
    std::lock_guard lock(mMutex);
    return mRecordedCommands;
}

void FakeVibratorHal::recordCommand(const HapticGeneratorCommand& command) {
    std::lock_guard<std::mutex> lock(mMutex);
    mRecordedCommands.push_back(command);
}

void FakeVibratorHal::run() {
    const int64_t fmqTimeout =
            std::chrono::duration_cast<std::chrono::nanoseconds>(mConfig.fmqTimeout).count();
    while (!mStopThread) {
        HapticGeneratorCommand command;
        if (!mCommandMQ->readBlocking(&command, 1, fmqTimeout)) {
            continue; // No command, loop again.
        }

        recordCommand(command);

        HapticGeneratorReply reply = {.status = OK};

        switch (command.getTag()) {
            case HapticGeneratorCommand::Tag::effect: {
                handleEffectCommand(command);
                break;
            }
            case HapticGeneratorCommand::Tag::burstBytes: {
                handleBurstCommand(reply);
                break;
            }
            case HapticGeneratorCommand::Tag::session: {
                handleSessionCommand(command);
                break;
            }
            default:
                ALOGE("FakeHapticGeneratorHal: Received unknown command tag.");
                reply.status = UNKNOWN_ERROR;
                break;
        }

        if (reply.status == TIMED_OUT || !mReplyMQ->writeBlocking(&reply, 1, fmqTimeout)) {
            ALOGE("FakeHapticGeneratorHal: Failed to write reply.");
        }
    }
}

void FakeVibratorHal::handleEffectCommand(const HapticGeneratorCommand& command) {
    auto effectCommand = command.get<HapticGeneratorCommand::Tag::effect>();
    if (effectCommand == HapticGeneratorCommand::Effect::START) {
        ALOGD("FakeHapticGeneratorHal: Received START command.");
        mEffectComplete = false;
        // Drain any stale effects from the queue.
        VibrationEffectContent effect;
        while (mEffectMQ->read(&effect)) {
            // Discard.
        }
    } else if (effectCommand == HapticGeneratorCommand::Effect::COMPLETE) {
        ALOGD("FakeHapticGeneratorHal: Received COMPLETE command.");
        mEffectComplete = true;
    } else if (effectCommand == HapticGeneratorCommand::Effect::CANCEL) {
        ALOGD("FakeHapticGeneratorHal: Received CANCEL command.");
        mEffectComplete = false;
    }
}

void FakeVibratorHal::handleBurstCommand(HapticGeneratorReply& reply) {
    if (mConfig.shouldNotReplyToBurst) {
        ALOGD("FakeHapticGeneratorHal: Intentionally not replying to BURST command to test "
              "timeout.");
        reply.status = TIMED_OUT;
    } else if (mConfig.alwaysReplyNotEnoughData) {
        ALOGD("FakeHapticGeneratorHal: Intentionally always replying "
              "NOT_ENOUGH_DATA.");
        reply.status = NOT_ENOUGH_DATA;
    } else if (mEffectComplete && mEffectMQ->availableToRead() == 0) {
        ALOGD("FakeHapticGeneratorHal: End of stream, replying with 0 bytes.");
        reply.burstBytesReady = 0;
    } else {
        VibrationEffectContent effect;
        if (mEffectMQ->read(&effect)) {
            ALOGD("FakeHapticGeneratorHal: Received BURST, effect read, producing "
                  "PCM.");
            std::vector<int8_t> pcm(mConfig.pcmToProduce,
                                    mConfig.vibratorId); // Use id for data to differentiate
            mPcmMQ->write(pcm.data(), pcm.size());
            reply.burstBytesReady = pcm.size();
        } else {
            ALOGD("FakeHapticGeneratorHal: Received BURST, no effect data, "
                  "replying NOT_ENOUGH_DATA.");
            reply.status = NOT_ENOUGH_DATA;
        }
    }
}

void FakeVibratorHal::handleSessionCommand(const HapticGeneratorCommand& command) {
    auto sessionCommand = command.get<HapticGeneratorCommand::Tag::session>();
    if (sessionCommand == HapticGeneratorCommand::Session::CLOSE) {
        ALOGD("FakeHapticGeneratorHal: Received CLOSE command.");
        mEffectComplete = false;
        VibrationEffectContent effect;
        while (mEffectMQ->read(&effect)) {
            // Discard.
        }
    }
}

} // namespace android::vibrator