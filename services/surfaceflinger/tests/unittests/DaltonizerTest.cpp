/*
 * Copyright 2018 The Android Open Source Project
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
#include <math/mat4.h>
#include <cmath>
#include "Effects/Daltonizer.h"

namespace android {

class DaltonizerTest {
private:
    Daltonizer& mDaltonizer;

public:
    DaltonizerTest(Daltonizer& daltonizer) : mDaltonizer(daltonizer) {}

    bool isDirty() const { return mDaltonizer.mDirty; }

    float getLevel() const { return mDaltonizer.mLevel; }

    ColorBlindnessType getType() const { return mDaltonizer.mType; }
};

constexpr float TOLERANCE = 0.001f;

static bool isIdentityMatrix(mat4& matrix) {
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = 0; j < 4; ++j) {
            if (i == j) {
                // Check diagonal elements
                if (std::fabs(matrix[i][j] - 1.0f) > TOLERANCE) {
                    return false;
                }
            } else {
                // Check off-diagonal elements
                if (std::fabs(matrix[i][j]) > TOLERANCE) {
                    return false;
                }
            }
        }
    }
    return true;
}

// Test Suite Name : DaltonizerTest, Test name: ConstructionDefaultValues
TEST(DaltonizerTest, ConstructionDefaultValues) {
    Daltonizer daltonizer;
    DaltonizerTest test(daltonizer);

    EXPECT_EQ(test.getLevel(), 0.7f);
    ASSERT_TRUE(test.isDirty());
    EXPECT_EQ(test.getType(), ColorBlindnessType::None);
    mat4 matrix = daltonizer();
    ASSERT_TRUE(isIdentityMatrix(matrix));
}

TEST(DaltonizerTest, NotDirtyAfterColorMatrixReturned) {
    Daltonizer daltonizer;

    mat4 matrix = daltonizer();
    DaltonizerTest test(daltonizer);

    ASSERT_FALSE(test.isDirty());
    ASSERT_TRUE(isIdentityMatrix(matrix));
}

TEST(DaltonizerTest, LevelOutOfRangeTooLowIgnored) {
    Daltonizer daltonizer;
    // Get matrix to reset isDirty == false.
    mat4 matrix = daltonizer();

    daltonizer.setLevel(-1);
    DaltonizerTest test(daltonizer);

    EXPECT_EQ(test.getLevel(), 0.7f);
    ASSERT_FALSE(test.isDirty());
}

TEST(DaltonizerTest, LevelOutOfRangeTooHighIgnored) {
    Daltonizer daltonizer;
    // Get matrix to reset isDirty == false.
    mat4 matrix = daltonizer();

    daltonizer.setLevel(11);
    DaltonizerTest test(daltonizer);

    EXPECT_EQ(test.getLevel(), 0.7f);
    ASSERT_FALSE(test.isDirty());
}

TEST(DaltonizerTest, ColorCorrectionMatrixNonIdentical) {
    Daltonizer daltonizer;
    daltonizer.setType(ColorBlindnessType::Protanomaly);
    daltonizer.setMode(ColorBlindnessMode::Correction);

    mat4 matrix = daltonizer();

    ASSERT_FALSE(isIdentityMatrix(matrix));
}

TEST(DaltonizerTest, LevelZeroColorMatrixEqIdentityMatrix) {
    Daltonizer daltonizer;
    daltonizer.setType(ColorBlindnessType::Protanomaly);
    daltonizer.setMode(ColorBlindnessMode::Correction);
    daltonizer.setLevel(0);

    mat4 matrix = daltonizer();

    ASSERT_TRUE(isIdentityMatrix(matrix));
}

TEST(DaltonizerTest, UpdatedAlgorithmConstructionDefaultValues) {
    Daltonizer daltonizer;
    daltonizer.setUseUpdatedAlgorithm(true);
    DaltonizerTest test(daltonizer);

    EXPECT_EQ(test.getLevel(), 0.7f);
    ASSERT_TRUE(test.isDirty());
    EXPECT_EQ(test.getType(), ColorBlindnessType::None);
    mat4 matrix = daltonizer();
    ASSERT_TRUE(isIdentityMatrix(matrix));
}

TEST(DaltonizerTest, UpdatedAlgorithmNotDirtyAfterColorMatrixReturned) {
    Daltonizer daltonizer;
    daltonizer.setUseUpdatedAlgorithm(true);

    mat4 matrix = daltonizer();
    DaltonizerTest test(daltonizer);

    ASSERT_FALSE(test.isDirty());
    ASSERT_TRUE(isIdentityMatrix(matrix));
}

TEST(DaltonizerTest, UpdatedAlgorithmLevelOutOfRangeTooLowIgnored) {
    Daltonizer daltonizer;
    daltonizer.setUseUpdatedAlgorithm(true);

    // Get matrix to reset isDirty == false.
    mat4 matrix = daltonizer();

    daltonizer.setLevel(-1);
    DaltonizerTest test(daltonizer);

    EXPECT_EQ(test.getLevel(), 0.7f);
    ASSERT_FALSE(test.isDirty());
}

TEST(DaltonizerTest, UpdatedAlgorithmLevelOutOfRangeTooHighIgnored) {
    Daltonizer daltonizer;
    daltonizer.setUseUpdatedAlgorithm(true);

    // Get matrix to reset isDirty == false.
    mat4 matrix = daltonizer();

    daltonizer.setLevel(11);
    DaltonizerTest test(daltonizer);

    EXPECT_EQ(test.getLevel(), 0.7f);
    ASSERT_FALSE(test.isDirty());
}

TEST(DaltonizerTest, UpdatedAlgorithmColorCorrectionMatrixNonIdentical) {
    Daltonizer daltonizer;
    daltonizer.setUseUpdatedAlgorithm(true);

    daltonizer.setType(ColorBlindnessType::Protanomaly);
    daltonizer.setMode(ColorBlindnessMode::Correction);

    mat4 matrix = daltonizer();

    ASSERT_FALSE(isIdentityMatrix(matrix));
}

TEST(DaltonizerTest, UpdatedAlgorithmLevelZeroColorMatrixEqIdentityMatrix) {
    Daltonizer daltonizer;
    daltonizer.setUseUpdatedAlgorithm(true);

    daltonizer.setType(ColorBlindnessType::Protanomaly);
    daltonizer.setMode(ColorBlindnessMode::Correction);
    daltonizer.setLevel(0);

    mat4 matrix = daltonizer();

    ASSERT_TRUE(isIdentityMatrix(matrix));
}

TEST(DaltonizerTest, TestProtanopiaCorrectionKeepsRed) {
    Daltonizer daltonizer;
    daltonizer.setUseUpdatedAlgorithm(true);
    daltonizer.setType(ColorBlindnessType::Protanomaly);
    daltonizer.setMode(ColorBlindnessMode::Correction);
    daltonizer.setLevel(10);

    mat4 matrix = daltonizer();

    // Expect a matrix with 1, 0, 0 in the top row, which multiplies against the red channel.
    //   |  1.0, 0.0, 0.0, |
    //   |  x.x, x.x, x.x, |
    //   |  x.x, x.x, x.x  |

    size_t row = 0;
    ASSERT_EQ(matrix[0][row], 1);
    ASSERT_EQ(matrix[1][row], 0);
    ASSERT_EQ(matrix[2][row], 0);
}

TEST(DaltonizerTest, TestDeuteranopiaCorrectionKeepsGreen) {
    Daltonizer daltonizer;
    daltonizer.setUseUpdatedAlgorithm(true);
    daltonizer.setType(ColorBlindnessType::Deuteranomaly);
    daltonizer.setMode(ColorBlindnessMode::Correction);
    daltonizer.setLevel(10);

    mat4 matrix = daltonizer();

    // Expect a matrix with 0, 1, 0 in the second row, which multiplies against the green channel.
    //   |  x.x, x.x, x.x, |
    //   |  0.0, 1.0, 0.0, |
    //   |  x.x, x.x, x.x  |

    size_t row = 1;
    ASSERT_EQ(matrix[0][row], 0);
    ASSERT_EQ(matrix[1][row], 1);
    ASSERT_EQ(matrix[2][row], 0);
}

TEST(DaltonizerTest, TestTritanopiaCorrectionKeepsBlue) {
    Daltonizer daltonizer;
    daltonizer.setUseUpdatedAlgorithm(true);
    daltonizer.setType(ColorBlindnessType::Tritanomaly);
    daltonizer.setMode(ColorBlindnessMode::Correction);
    daltonizer.setLevel(10);

    mat4 matrix = daltonizer();

    // Expect a matrix with 0, 0, 1 in the third row, which multiplies against the blue channel.
    //   |  x.x, x.x, x.x, |
    //   |  x.x, x.x, x.x  |
    //   |  0.0, 0.0, 1.0, |

    size_t row = 2;
    ASSERT_EQ(matrix[0][row], 0);
    ASSERT_EQ(matrix[1][row], 0);
    ASSERT_EQ(matrix[2][row], 1);
}

} /* namespace android */
