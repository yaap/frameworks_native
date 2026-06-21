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
#include <vibrationflinger/HapticGeneratorSession.h>

#include <utils/Log.h>

namespace android::vibrator {
using namespace ::std::chrono_literals;
using aidl::android::hardware::common::fmq::SynchronizedReadWrite;
using aidl::android::hardware::vibrator::HapticGeneratorCommand;
using aidl::android::hardware::vibrator::HapticGeneratorReply;
using aidl::android::hardware::vibrator::VibrationEffectContent;
using CommandQueue = android::AidlMessageQueue<HapticGeneratorCommand, SynchronizedReadWrite>;
using ReplyQueue = android::AidlMessageQueue<HapticGeneratorReply, SynchronizedReadWrite>;
using EffectQueue = android::AidlMessageQueue<VibrationEffectContent, SynchronizedReadWrite>;
using PcmQueue = android::AidlMessageQueue<int8_t, SynchronizedReadWrite>;

HapticGeneratorSession::HapticGeneratorSession(
        aidl::android::hardware::vibrator::HapticGeneratorSession&& halSession) {
    for (const auto& halQueues : halSession.queues) {
        mQueues.emplace(halQueues.vibratorId,
                        VibratorQueues{
                                .command = std::make_shared<CommandQueue>(halQueues.command, true),
                                .reply = std::make_shared<ReplyQueue>(halQueues.reply, true),
                                .effect = std::make_shared<EffectQueue>(halQueues.effect, true),
                                .pcm = std::make_shared<PcmQueue>(halQueues.pcm, true),
                        });
    }
}

// Test-only constructor
HapticGeneratorSession::HapticGeneratorSession(std::map<int32_t, VibratorQueues>&& queues)
      : mQueues(std::move(queues)) {}

HapticGeneratorSession::~HapticGeneratorSession() {
    close();
}

status_t HapticGeneratorSession::startStream(int32_t vibratorId,
                                             const std::vector<VibrationEffectContent>& effect) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mIsClosed) {
        ALOGE("HapticGeneratorSession: startStream called on a closed session.");
        return INVALID_OPERATION;
    }

    auto it = mQueues.find(vibratorId);
    if (it == mQueues.end()) {
        ALOGE("HapticGeneratorSession: vibrator %d not part of this session.", vibratorId);
        return INVALID_OPERATION;
    }

    // If a stream is already running on this vibrator, stop it before starting a new one
    stopStreamInternal(vibratorId);

    auto stream = std::make_unique<HapticGeneratorStream>(vibratorId, effect);
    VibratorQueues& queues = it->second;
    status_t status = stream->start(queues);

    if (status == OK) {
        mStreams[vibratorId] = std::move(stream);
    }

    return status;
}

android::base::Result<size_t> HapticGeneratorSession::readStream(int32_t vibratorId,
                                                                 std::span<int8_t> buffer) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mIsClosed) {
        ALOGE("HapticGeneratorSession: readStream called on a closed session.");
        return android::base::Error(INVALID_OPERATION) << "Session is closed";
    }

    auto streamIt = mStreams.find(vibratorId);
    if (streamIt == mStreams.end()) {
        ALOGE("HapticGeneratorSession: No active stream for vibrator %d.", vibratorId);
        return android::base::Error(NAME_NOT_FOUND) << "Stream not found";
    }

    auto queueIt = mQueues.find(vibratorId);
    if (queueIt == mQueues.end()) {
        ALOGE("HapticGeneratorSession: No queues for vibrator %d.", vibratorId);
        return android::base::Error(NAME_NOT_FOUND) << "Queues not found";
    }

    return streamIt->second->read(queueIt->second, buffer);
}

status_t HapticGeneratorSession::stopStream(int32_t vibratorId) {
    std::lock_guard<std::mutex> lock(mMutex);
    stopStreamInternal(vibratorId);

    return OK;
}

void HapticGeneratorSession::stopStreamInternal(int32_t vibratorId) {
    auto streamIt = mStreams.find(vibratorId);
    if (streamIt == mStreams.end()) {
        return; // Nothing to close
    }

    auto queueIt = mQueues.find(vibratorId);
    if (queueIt != mQueues.end()) {
        VibratorQueues& queues = queueIt->second;
        streamIt->second->stop(queues);
    }

    mStreams.erase(streamIt);
}

status_t HapticGeneratorSession::close() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mIsClosed) {
        return OK;
    }

    // Close all active streams first
    for (const auto& [vibratorId, stream] : mStreams) {
        auto queueIt = mQueues.find(vibratorId);
        if (queueIt != mQueues.end()) {
            VibratorQueues& queues = queueIt->second;
            stream->stop(queues);
        }
    }
    mStreams.clear();

    // Send CLOSE command to the HAL for each vibrator
    HapticGeneratorCommand command;
    command.set<HapticGeneratorCommand::Tag::session>(HapticGeneratorCommand::Session::CLOSE);
    status_t finalStatus = OK;

    for (const auto& [vibratorId, queues] : mQueues) {
        HapticGeneratorReply reply;
        sendCommandAndReceiveReply(vibratorId, command, &reply);
        status_t currentStatus = reply.status;
        if (currentStatus != OK) {
            ALOGE("HapticGeneratorSession: Failed to cleanly close session for vibrator %d: %d",
                  vibratorId, currentStatus);
            if (finalStatus == OK) {
                finalStatus = currentStatus;
            }
        }
    }

    mIsClosed = true;
    mQueues.clear();
    return finalStatus;
}

bool HapticGeneratorSession::isValidForVibrators(const std::vector<int32_t>& vibratorIds) {
    std::lock_guard<std::mutex> lock(mMutex);

    if (mQueues.size() != vibratorIds.size()) {
        ALOGE("HapticGeneratorSession: Invalid session, expected %zu queues but was %zu",
              vibratorIds.size(), mQueues.size());
        return false;
    }

    for (int32_t id : vibratorIds) {
        if (mQueues.find(id) == mQueues.end()) {
            // A vibrator id is missing
            ALOGE("HapticGeneratorSession: Requested vibrator %d is missing from HAL session", id);
            return false;
        }
    }

    return true;
}

void HapticGeneratorSession::sendCommandAndReceiveReply(int32_t vibratorId,
                                                        const HapticGeneratorCommand& command,
                                                        HapticGeneratorReply* reply) {
    if (mIsClosed) {
        reply->status = INVALID_OPERATION;
        return;
    }

    auto it = mQueues.find(vibratorId);
    if (it == mQueues.end()) {
        reply->status = NAME_NOT_FOUND;
        return;
    }

    const auto& queues = it->second;
    if (!queues.command->writeBlocking(&command, 1,
                                       std::chrono::nanoseconds(kFmqTimeout).count())) {
        ALOGE("HapticGeneratorSession: Timed out writing command for vibrator %d.", vibratorId);
        reply->status = TIMED_OUT;
        return;
    }
    if (!queues.reply->readBlocking(reply, 1, std::chrono::nanoseconds(kFmqTimeout).count())) {
        ALOGE("HapticGeneratorSession: Timed out waiting for reply from vibrator %d", vibratorId);
        reply->status = TIMED_OUT;
        return;
    }
}

} // namespace android::vibrator