
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
#include <gtest/gtest.h>
#include <vector>
#include "../observer/HistogramScale.h"
using namespace android;

TEST(HistogramTest, BasicOperation) {
    std::vector<uint8_t> bins;
    for (int i = 0; i < 10; ++i) {
        bins.push_back(HistogramScale::getBinIndex(static_cast<int64_t>(i) * 1000));
    }
    ASSERT_EQ(10u, bins.size());
    for (int i = 0; i < 10; ++i) {
        ASSERT_EQ(HistogramScale::getBinIndex(static_cast<int64_t>(i) * 1000), bins[i]);
    }
}

TEST(HistogramTest, GetBinIndexBoundaries) {
    // Factor is 1.2
    // Bin 0: [0, 5000) nanos
    EXPECT_EQ(0u, HistogramScale::getBinIndex(0LL));
    EXPECT_EQ(0u, HistogramScale::getBinIndex(4999LL));

    // Bin 1: [5000, 6000) nanos
    EXPECT_EQ(1u, HistogramScale::getBinIndex(5000LL));
    EXPECT_EQ(1u, HistogramScale::getBinIndex(5999LL));

    // Bin 2: [6000, 7200) nanos
    EXPECT_EQ(2u, HistogramScale::getBinIndex(6000LL));
    EXPECT_EQ(2u, HistogramScale::getBinIndex(7199LL));

    // Values far beyond the threshold should be clamped to 99
    EXPECT_EQ(99u, HistogramScale::getBinIndex(1000000000000000000LL));
}
