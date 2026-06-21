/*
 * Copyright (C) 2025 The Android Open Source Project
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

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace android {

/**
 * A utility class for exponential histogram bin index calculation.
 */
class HistogramScale {
public:
    static constexpr float kFirstBinThreshold = 5.0f;
    static constexpr float kInverseFirstBinThreshold = 0.2f; // 1 / 5.0
    static constexpr float kLastBinThreshold = 287562411.534772744f;
    static constexpr size_t kBucketCount = 100;
    // The ScaleFactor is 1.2
    static constexpr float kInverseLogOfScaleFactor = 5.48481846175f; // 1 / ln(1.2)

    /**
     * Calculates the bin index for a given value based on an exponential scale.
     */
    static uint8_t getBinIndex(int64_t durationNanos) {
        float value = static_cast<float>(durationNanos) * 0.001f;
        if (value < kFirstBinThreshold) {
            return 0;
        } else if (value > kLastBinThreshold) {
            return kBucketCount - 1;
        }
        // Bin 0 is for values < kFirstBinThreshold.
        // The rest of the bins are determined by an exponential scale.
        // i = 1 + floor(log(value / kFirstBinThreshold) / log(factor))
        return static_cast<uint8_t>(
                1.0f +
                std::floor(std::log(value * kInverseFirstBinThreshold) * kInverseLogOfScaleFactor));
    }
};

} // namespace android
