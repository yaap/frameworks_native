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

#include <fmq/AidlMessageQueue.h>
#include <vibrationflinger/VibratorQueues.h>

#include <android-base/result.h>
#include <span>

#include <deque>
#include <vector>

namespace android::vibrator {

class HapticGeneratorStream {
public:
    /**
     * Constructs a stream. This is a lightweight operation and does not block.
     * The stream must be explicitly started with a call to start().
     *
     * @param vibratorId The ID of the vibrator to use for this stream.
     * @param effect The complete vibration effect to be converted to PCM data.
     */
    HapticGeneratorStream(
            int32_t vibratorId,
            const std::vector<aidl::android::hardware::vibrator::VibrationEffectContent>& effect);

    /**
     * Destroys the stream. This is non-blocking and does not send any commands to the HAL.
     * The parent HapticGeneratorSession is responsible for cleanup.
     */
    ~HapticGeneratorStream() = default;

    /**
     * Starts the stream by sending a START command to the HAL.
     * This must be called before any calls to read().
     *
     * @param queues A reference to the FMQs for the vibrator associated with this stream.
     *
     * @return OK on success, or an error code like TIMED_OUT.
     */
    status_t start(VibratorQueues& queues);

    /**
     * Reads the next chunk of generated PCM data from the HAL.
     *
     * @param queues A reference to the FMQs for the vibrator associated with this stream.
     * @param buffer A span representing the buffer where the PCM data will be written.
     *
     * @return A Result containing the number of bytes read on success, or an error.
     */
    android::base::Result<size_t> read(VibratorQueues& queues, std::span<int8_t> buffer);

    /**
     * Closes the stream by sending a CANCEL command to the HAL.
     * This is a non-blocking call that cleans up HAL resources for this stream.
     *
     * @param queues A reference to the FMQs for the vibrator associated with this stream.
     *
     * @return OK on success
     */
    status_t stop(VibratorQueues& queues);

private:
    // Helper to write a vibration effect to the HAL's queue.
    status_t maybeWriteEffects(VibratorQueues& queues);

    const int32_t mVibratorId;

    bool mIsActive = false;
    bool mIsPcmComplete = false;
    bool mIsEffectComplete = false;

    // Internal buffer for vibration effect segments that haven't been sent to the HAL yet.
    std::deque<aidl::android::hardware::vibrator::VibrationEffectContent> mPendingEffects;
};

} // namespace android::vibrator