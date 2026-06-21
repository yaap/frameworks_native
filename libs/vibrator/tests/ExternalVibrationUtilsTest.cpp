/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *            http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>
#include <vibrator/ExternalVibrationUtils.h>

#include "test_utils.h"

using namespace android;
using namespace testing;

using HapticScale = os::HapticScale;
using HapticLevel = os::HapticLevel;

static constexpr float TEST_TOLERANCE = 1e-2f;
static constexpr size_t TEST_BUFFER_LENGTH = 4;
static float TEST_BUFFER[TEST_BUFFER_LENGTH] = { 1, -1, 0.5f, -0.2f };

class ExternalVibrationUtilsTest : public Test {
public:
    void SetUp() override {
        std::copy(std::begin(TEST_BUFFER), std::end(TEST_BUFFER), std::begin(mBuffer));
    }

protected:
    void scaleBuffer(HapticScale hapticScale) {
        scaleBuffer(hapticScale, 0 /* limit */);
    }

    void scaleBuffer(HapticScale hapticScale, float limit) {
        std::copy(std::begin(TEST_BUFFER), std::end(TEST_BUFFER), std::begin(mBuffer));
        os::scaleHapticData(&mBuffer[0], TEST_BUFFER_LENGTH, hapticScale, limit);
    }

    float mBuffer[TEST_BUFFER_LENGTH];
};

TEST_F(ExternalVibrationUtilsTest, TestScaleMute) {
    float expected[TEST_BUFFER_LENGTH];
    std::fill(std::begin(expected), std::end(expected), 0);

    scaleBuffer(HapticScale::mute());
    EXPECT_FLOATS_NEARLY_EQ(expected, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);
}

TEST_F(ExternalVibrationUtilsTest, TestScaleNone) {
    float expected[TEST_BUFFER_LENGTH];
    std::copy(std::begin(TEST_BUFFER), std::end(TEST_BUFFER), std::begin(expected));

    scaleBuffer(HapticScale::none());
    EXPECT_FLOATS_NEARLY_EQ(expected, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);
}

TEST_F(ExternalVibrationUtilsTest, TestScaleHapticLevel) {
    float expectedVeryHigh[TEST_BUFFER_LENGTH] = { 1, -1, 0.8f, -0.38f };
    scaleBuffer(HapticScale(HapticLevel::VERY_HIGH));
    EXPECT_FLOATS_NEARLY_EQ(expectedVeryHigh, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);

    float expectedHigh[TEST_BUFFER_LENGTH] = { 1, -1, 0.63f, -0.27f };
    scaleBuffer(HapticScale(HapticLevel::HIGH));
    EXPECT_FLOATS_NEARLY_EQ(expectedHigh, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);

    float expectedLow[TEST_BUFFER_LENGTH] = { 0.71f, -0.71f, 0.35f, -0.14f };
    scaleBuffer(HapticScale(HapticLevel::LOW));
    EXPECT_FLOATS_NEARLY_EQ(expectedLow, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);

    float expectedVeryLow[TEST_BUFFER_LENGTH] = { 0.51f, -0.51f, 0.25f, -0.1f };
    scaleBuffer(HapticScale(HapticLevel::VERY_LOW));
    EXPECT_FLOATS_NEARLY_EQ(expectedVeryLow, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);
}

TEST_F(ExternalVibrationUtilsTest, TestScaleFactorUndefinedUsesHapticLevel) {
    constexpr float adaptiveScaleNone = 1.0f;
    float expectedVeryHigh[TEST_BUFFER_LENGTH] = { 1, -1, 0.8f, -0.38f };

    scaleBuffer(HapticScale(HapticLevel::VERY_HIGH, -1.0f /* scaleFactor */, adaptiveScaleNone));
    EXPECT_FLOATS_NEARLY_EQ(expectedVeryHigh, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);
}

TEST_F(ExternalVibrationUtilsTest, TestScaleFactor) {
    constexpr float adaptiveScaleNone = 1.0f;

    float expectedVeryHigh[TEST_BUFFER_LENGTH] = { 1, -1, 1, -0.55f };
    scaleBuffer(HapticScale(HapticLevel::VERY_HIGH, 3.0f /* scaleFactor */, adaptiveScaleNone));
    EXPECT_FLOATS_NEARLY_EQ(expectedVeryHigh, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);

    float expectedHigh[TEST_BUFFER_LENGTH] = { 1, -1, 0.66f, -0.29f };
    scaleBuffer(HapticScale(HapticLevel::HIGH, 1.5f /* scaleFactor */, adaptiveScaleNone));
    EXPECT_FLOATS_NEARLY_EQ(expectedHigh, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);

    float expectedLow[TEST_BUFFER_LENGTH] = {0.8f, -0.8f, 0.4f, -0.16f};
    scaleBuffer(HapticScale(HapticLevel::LOW, 0.8f /* scaleFactor */, adaptiveScaleNone));
    EXPECT_FLOATS_NEARLY_EQ(expectedLow, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);

    float expectedVeryLow[TEST_BUFFER_LENGTH] = {0.4f, -0.4f, 0.2f, -0.08f};
    scaleBuffer(HapticScale(HapticLevel::VERY_LOW, 0.4f /* scaleFactor */, adaptiveScaleNone));
    EXPECT_FLOATS_NEARLY_EQ(expectedVeryLow, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);
}

TEST_F(ExternalVibrationUtilsTest, TestAdaptiveScaleFactorUndefinedIgnored) {
    constexpr float adaptiveScaleInvalid = -1.0f;

    float expectedVeryHigh[TEST_BUFFER_LENGTH] = {1, -1, 1, -0.55f};
    scaleBuffer(HapticScale(HapticLevel::VERY_HIGH, 3.0f /* scaleFactor */, adaptiveScaleInvalid));
    EXPECT_FLOATS_NEARLY_EQ(expectedVeryHigh, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);
}

TEST_F(ExternalVibrationUtilsTest, TestAdaptiveScaleFactorBoundsAppliedAfterScale) {
    // Adaptive scale mutes vibration
    float expectedMuted[TEST_BUFFER_LENGTH];
    std::fill(std::begin(expectedMuted), std::end(expectedMuted), 0);
    scaleBuffer(HapticScale(HapticLevel::VERY_HIGH, 3.0f /* scaleFactor */,
                            0.0f /* adaptiveScaleFactor */));
    EXPECT_FLOATS_NEARLY_EQ(expectedMuted, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);

    // Haptic level scale up then adaptive scale down
    float expectedVeryHigh[TEST_BUFFER_LENGTH] = {0.2, -0.2, 0.2f, -0.11f};
    scaleBuffer(HapticScale(HapticLevel::VERY_HIGH, 3.0f /* scaleFactor */,
                            0.2f /* adaptiveScaleFactor */));
    EXPECT_FLOATS_NEARLY_EQ(expectedVeryHigh, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);

    // Haptic level scale up then adaptive scale up
    float expectedHigh[TEST_BUFFER_LENGTH] = {1, -1, 1, -0.44f};
    scaleBuffer(
            HapticScale(HapticLevel::HIGH, 1.5f /* scaleFactor */, 1.5f /* adaptiveScaleFactor */));
    EXPECT_FLOATS_NEARLY_EQ(expectedHigh, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);

    // Haptic level scale down then adaptive scale down
    float expectedLow[TEST_BUFFER_LENGTH] = {0.48f, -0.48f, 0.24f, -0.09f};
    scaleBuffer(
            HapticScale(HapticLevel::LOW, 0.8f /* scaleFactor */, 0.6f /* adaptiveScaleFactor */));
    EXPECT_FLOATS_NEARLY_EQ(expectedLow, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);

    // Haptic level scale down then adaptive scale up
    float expectedVeryLow[TEST_BUFFER_LENGTH] = {0.8f, -0.8f, 0.4f, -0.16f};
    scaleBuffer(HapticScale(HapticLevel::VERY_LOW, 0.4f /* scaleFactor */,
                            2.0f /* adaptiveScaleFactor */));
    EXPECT_FLOATS_NEARLY_EQ(expectedVeryLow, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);
}

TEST_F(ExternalVibrationUtilsTest, TestLimitAppliedAfterScale) {
    // Scaled = { 0.2, -0.2, 0.2, -0.11 };
    float expectedClippedVeryHigh[TEST_BUFFER_LENGTH] = {0.15f, -0.15f, 0.15f, -0.11f};
    scaleBuffer(HapticScale(HapticLevel::VERY_HIGH, 3.0f, 0.2f), 0.15f /* limit */);
    EXPECT_FLOATS_NEARLY_EQ(expectedClippedVeryHigh, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);

    // Scaled = { 0.8, -0.8, 0.4, -0.16 }
    float expectedClippedVeryLow[TEST_BUFFER_LENGTH] = {0.7f, -0.7f, 0.4f, -0.16f};
    scaleBuffer(HapticScale(HapticLevel::VERY_LOW, 0.4f, 2.0f), 0.7f /* limit */);
    EXPECT_FLOATS_NEARLY_EQ(expectedClippedVeryLow, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);
}

TEST_F(ExternalVibrationUtilsTest, TestInvalidLimitIgnored) {
    float expectedClippedVeryHigh[TEST_BUFFER_LENGTH] = {0.2f, -0.2f, 0.2f, -0.11f};
    scaleBuffer(HapticScale(HapticLevel::VERY_HIGH, 3.0f, 0.2f), 0.0f /* limit */);
    EXPECT_FLOATS_NEARLY_EQ(expectedClippedVeryHigh, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);

    scaleBuffer(HapticScale(HapticLevel::VERY_HIGH, 3.0f, 0.2f), NAN /* limit */);
    EXPECT_FLOATS_NEARLY_EQ(expectedClippedVeryHigh, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);

    scaleBuffer(HapticScale(HapticLevel::VERY_HIGH, 3.0f, 0.2f), -0.5f /* limit */);
    EXPECT_FLOATS_NEARLY_EQ(expectedClippedVeryHigh, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);

    scaleBuffer(HapticScale(HapticLevel::VERY_HIGH, 3.0f, 0.2f), 1.5f /* limit */);
    EXPECT_FLOATS_NEARLY_EQ(expectedClippedVeryHigh, mBuffer, TEST_BUFFER_LENGTH, TEST_TOLERANCE);
}
