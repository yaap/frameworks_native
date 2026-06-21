/*
 * Copyright (C) 2020 The Android Open Source Project
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
#define LOG_TAG "ExternalVibrationUtils"

#include <cmath>
#include <cstring>

#include <android_os_vibrator.h>

#include <algorithm>

#include <log/log.h>
#include <vibrator/ExternalVibrationUtils.h>

namespace android::os {

namespace {
static constexpr float SCALE_LEVEL_GAIN = 1.4f; // Same as VibrationConfig.DEFAULT_SCALE_LEVEL_GAIN

/* Same as VibrationScaler.getScaleFactor */
float getHapticScaleFactor(HapticScale scale) {
    if (scale.getScaleFactor() >= 0) {
        // ExternalVibratorService provided the scale factor, use it.
        return scale.getScaleFactor();
    }

    HapticLevel level = scale.getLevel();
    switch (level) {
        case HapticLevel::MUTE:
            return 0.0f;
        case HapticLevel::NONE:
            return 1.0f;
        default:
            float scaleFactor = powf(SCALE_LEVEL_GAIN, static_cast<int32_t>(level));
            if (scaleFactor <= 0) {
                ALOGE("Invalid scale factor %.2f for level %d, using fallback to 1.0",
                        scaleFactor, static_cast<int32_t>(level));
                scaleFactor = 1.0f;
            }
            return scaleFactor;
    }
}

void applyHapticScale(float* buffer, size_t length, HapticScale scale) {
    if (scale.isScaleMute()) {
        memset(buffer, 0, length * sizeof(float));
        return;
    }
    if (scale.isScaleNone()) {
        return;
    }
    float scaleFactor = getHapticScaleFactor(scale);
    float adaptiveScaleFactor = scale.getAdaptiveScaleFactor();

    for (size_t i = 0; i < length; i++) {
        float value = buffer[i];
        if (scaleFactor >= 0) {
            // Using S * x / (1 + (S - 1) * x^2) as the scale up function to converge to 1.0.
            value = (scaleFactor <= 1 || value == 0)
                    ? (value * scaleFactor)
                    : (value * scaleFactor) / (1 + (scaleFactor - 1) * value * value);
        }
        if (adaptiveScaleFactor >= 0) {
            value *= adaptiveScaleFactor;
        }
        // Make sure scaled value is within supported float PCM range of [-1,1].
        buffer[i] = std::clamp(value, -1.0f, 1.0f);
    }
}

void clipHapticData(float* buffer, size_t length, float limit) {
    if (isnan(limit) || limit <= 0 || limit >= 1) {
        return;
    }
    for (size_t i = 0; i < length; i++) {
        buffer[i] = std::clamp(buffer[i], -limit, limit);
    }
}

} // namespace

bool isValidHapticScale(HapticScale scale) {
    switch (scale.getLevel()) {
    case HapticLevel::MUTE:
    case HapticLevel::VERY_LOW:
    case HapticLevel::LOW:
    case HapticLevel::NONE:
    case HapticLevel::HIGH:
    case HapticLevel::VERY_HIGH:
        return true;
    }
    return false;
}

void scaleHapticData(float* buffer, size_t length, HapticScale scale, float limit) {
    if (isValidHapticScale(scale)) {
        applyHapticScale(buffer, length, scale);
    }
    clipHapticData(buffer, length, limit);
}

} // namespace android::os
