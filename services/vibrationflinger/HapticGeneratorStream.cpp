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

#include <vibrationflinger/HapticGeneratorConstants.h>
#include <vibrationflinger/HapticGeneratorStream.h>

#include <utils/Log.h>
#include <utils/Timers.h>

namespace android::vibrator {

namespace fmq = aidl::android::hardware::common::fmq;

using namespace ::std::chrono_literals;
using aidl::android::hardware::vibrator::HapticGeneratorCommand;
using aidl::android::hardware::vibrator::HapticGeneratorReply;
using aidl::android::hardware::vibrator::VibrationEffectContent;

using CommandQueue = android::AidlMessageQueue<HapticGeneratorCommand, fmq::SynchronizedReadWrite>;
using EffectQueue = android::AidlMessageQueue<VibrationEffectContent, fmq::SynchronizedReadWrite>;
using ReplyQueue = android::AidlMessageQueue<HapticGeneratorReply, fmq::SynchronizedReadWrite>;
using PcmQueue = android::AidlMessageQueue<int8_t, fmq::SynchronizedReadWrite>;

static constexpr size_t kDrainBufferSize = 256;

// Helper to send a command and block until a reply is received
static status_t sendCommandAndReceiveReply(VibratorQueues& queues,
                                           const HapticGeneratorCommand& command,
                                           HapticGeneratorReply* reply) {
    if (!queues.command->writeBlocking(&command, 1,
                                       std::chrono::nanoseconds(kFmqTimeout).count())) {
        ALOGE("Haptic stream: Timed out writing command.");
        return TIMED_OUT;
    }
    if (!queues.reply->readBlocking(reply, 1, std::chrono::nanoseconds(kFmqTimeout).count())) {
        ALOGE("Haptic stream: Timed out waiting for reply.");
        return TIMED_OUT;
    }
    return reply->status;
}

HapticGeneratorStream::HapticGeneratorStream(int32_t vibratorId,
                                             const std::vector<VibrationEffectContent>& effect)
      : mVibratorId(vibratorId), mPendingEffects(effect.begin(), effect.end()) {}

status_t HapticGeneratorStream::start(VibratorQueues& queues) {
    if (mIsActive) {
        ALOGE("Haptic stream for vibrator %d already started.", mVibratorId);
        return INVALID_OPERATION;
    }

    // Send START command to reset the HAL's state for a new conversion
    HapticGeneratorCommand command;
    command.set<HapticGeneratorCommand::Tag::effect>(HapticGeneratorCommand::Effect::START);
    HapticGeneratorReply reply;
    status_t status = sendCommandAndReceiveReply(queues, command, &reply);
    if (status != OK) {
        ALOGE("Haptic stream failed to send START command for vibrator %d: %d", mVibratorId,
              status);
        return status;
    }

    // Clear the pcm queue of any stale data from a previous stream
    if (queues.pcm->availableToRead() > 0) {
        int8_t drainBuffer[kDrainBufferSize];
        while (queues.pcm->read(drainBuffer, kDrainBufferSize)) {
            // Discard data.
        }
        ALOGD("Drained leftover PCM data for vibrator %d", mVibratorId);
    }

    mIsActive = true;
    ALOGD("Haptic stream started successfully for vibrator %d", mVibratorId);
    return status;
}

android::base::Result<size_t> HapticGeneratorStream::read(VibratorQueues& queues,
                                                          std::span<int8_t> buffer) {
    if (!mIsActive) {
        ALOGE("Haptic stream read called on an inactive stream for vibrator %d", mVibratorId);
        return INVALID_OPERATION;
    }

    if (mIsPcmComplete) {
        return OK; // End of stream was already reached
    }

    const nsecs_t deadline =
            systemTime(SYSTEM_TIME_MONOTONIC) + std::chrono::nanoseconds(kReadPcmTimeout).count();
    size_t totalBytesRead = 0;

    while (systemTime(SYSTEM_TIME_MONOTONIC) < deadline && totalBytesRead < buffer.size()) {
        // Try to write any pending effect data to the HAL
        status_t writeStatus = maybeWriteEffects(queues);
        if (writeStatus != OK) {
            return android::base::Error(writeStatus) << "Failed to write effects to FMQ";
        }

        // Create a subspan for the current write position.
        auto remainingBuffer = buffer.subspan(totalBytesRead);

        // Send a burst command to request the remaining PCM data
        HapticGeneratorCommand command;
        command.set<HapticGeneratorCommand::Tag::burstBytes>(
                static_cast<int32_t>(remainingBuffer.size()));
        HapticGeneratorReply reply;
        status_t status = sendCommandAndReceiveReply(queues, command, &reply);

        if (status == OK) {
            if (reply.burstBytesReady == 0) {
                // End of stream
                mIsPcmComplete = true;
                ALOGV("Haptic stream for vibrator %d fully consumed.", mVibratorId);
                break;
            }

            size_t bytesToRead =
                    std::min(remainingBuffer.size(), static_cast<size_t>(reply.burstBytesReady));

            if (!queues.pcm->read(remainingBuffer.data(), bytesToRead)) {
                ALOGE("Haptic stream failed to read PCM data from queue for vibrator %d",
                      mVibratorId);
                return android::base::Error(INVALID_OPERATION) << "Failed to read from PCM FMQ";
            }
            totalBytesRead += bytesToRead;

        } else if (status == NOT_ENOUGH_DATA) {
            if (mIsEffectComplete) {
                // This is an error. The HAL received the 'COMPLETE' command
                // but is incorrectly asking for more effect data.
                ALOGE("Haptic stream for vibrator %d: HAL replied NOT_ENOUGH_DATA "
                      "after effect was complete.",
                      mVibratorId);
                return android::base::Error(UNKNOWN_ERROR)
                        << "HAL requested data after effect was complete";
            }

            ALOGD("Haptic stream for vibrator %d needs more data, retrying...", mVibratorId);
            continue;
        } else {
            ALOGE("Haptic stream for vibrator %d received error status from HAL: %d", mVibratorId,
                  status);
            return android::base::Error(status) << "HAL returned an unrecoverable error";
        }
    }

    return android::base::Result<size_t>(totalBytesRead);
}

status_t HapticGeneratorStream::stop(VibratorQueues& queues) {
    if (!mIsActive || mIsPcmComplete) {
        // Stream is already closed
        return OK;
    }

    ALOGW("Haptic stream for vibrator %d is being closed. Cancelling effect.", mVibratorId);
    HapticGeneratorCommand command;
    command.set<HapticGeneratorCommand::Tag::effect>(HapticGeneratorCommand::Effect::CANCEL);
    HapticGeneratorReply reply;
    status_t status = sendCommandAndReceiveReply(queues, command, &reply);

    if (status != OK) {
        ALOGE("Haptic stream failed to send CANCEL command for vibrator %d: %d", mVibratorId,
              status);
        return status;
    }

    mIsActive = false;
    return OK;
}

status_t HapticGeneratorStream::maybeWriteEffects(android::vibrator::VibratorQueues& queues) {
    if (mIsEffectComplete) {
        return OK; // All data has been sent
    }

    while (!mPendingEffects.empty() && queues.effect->write(&mPendingEffects.front())) {
        mPendingEffects.pop_front();
    }

    // If we have successfully sent all pending segments, send the COMPLETE command
    if (mPendingEffects.empty()) {
        HapticGeneratorCommand command;
        command.set<HapticGeneratorCommand::Tag::effect>(HapticGeneratorCommand::Effect::COMPLETE);
        HapticGeneratorReply reply;
        status_t status = sendCommandAndReceiveReply(queues, command, &reply);
        if (status != OK) {
            ALOGE("Haptic stream failed to send COMPLETE command for vibrator %d: %d", mVibratorId,
                  status);
            return status;
        }
        mIsEffectComplete = true;
    }

    return OK;
}

} // namespace android::vibrator