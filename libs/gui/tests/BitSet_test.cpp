/*
 * Copyright 2026 The Android Open Source Project
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

#include <gui/BitSet.h>

namespace android {

TEST(BitSet, DefaultConstructor) {
    BitSet<10> bs;
    ASSERT_EQ(bs.size(), 10u);
    ASSERT_TRUE(bs.none());
    ASSERT_FALSE(bs.any());
    ASSERT_FALSE(bs);
}

TEST(BitSet, InitializerListConstructor) {
    BitSet<64> bs{1, 10, 63};
    ASSERT_EQ(bs.count(), 3u);
    ASSERT_TRUE(bs.test(1));
    ASSERT_TRUE(bs.test(10));
    ASSERT_TRUE(bs.test(63));
    ASSERT_FALSE(bs.test(0));
    ASSERT_FALSE(bs.test(2));

    // Out of bounds in initializer list
    EXPECT_ANY_THROW((BitSet<5>{5}));
}

TEST(BitSet, SetAndTest) {
    BitSet<64> bs;
    bs.set(10);
    ASSERT_TRUE(bs.test(10));
    ASSERT_TRUE(bs[10]);
    ASSERT_FALSE(bs.test(11));
    ASSERT_TRUE(bs.any());
    ASSERT_TRUE(bs);
    ASSERT_EQ(bs.count(), 1u);

    bs.set(63);
    ASSERT_TRUE(bs.test(63));
    ASSERT_EQ(bs.count(), 2u);
}

TEST(BitSet, SetAll) {
    BitSet<128> bs;
    bs.set();
    ASSERT_TRUE(bs.all());
    ASSERT_EQ(bs.count(), 128u);
    ASSERT_TRUE(bs.test(0));
    ASSERT_TRUE(bs.test(127));
}

TEST(BitSet, Reset) {
    BitSet<32> bs;
    bs.set();
    bs.reset(5);
    ASSERT_FALSE(bs.test(5));
    ASSERT_EQ(bs.count(), 31u);

    bs.reset();
    ASSERT_TRUE(bs.none());
    ASSERT_EQ(bs.count(), 0u);
}

TEST(BitSet, Flip) {
    BitSet<16> bs;
    bs.flip(3);
    ASSERT_TRUE(bs.test(3));
    ASSERT_EQ(bs.count(), 1u);

    bs.flip();
    ASSERT_FALSE(bs.test(3));
    ASSERT_TRUE(bs.test(0));
    ASSERT_TRUE(bs.test(15));
    ASSERT_EQ(bs.count(), 15u);
}

TEST(BitSet, OperatorBool) {
    BitSet<8> bs;
    ASSERT_FALSE(bool(bs));
    bs.set(0);
    ASSERT_TRUE(bool(bs));
    bs.reset(0);
    ASSERT_FALSE(bool(bs));
}

TEST(BitSet, OperatorEquals) {
    BitSet<100> bs1;
    BitSet<100> bs2;
    ASSERT_TRUE(bs1 == bs2);
    ASSERT_FALSE(bs1 != bs2);

    bs1.set(50);
    ASSERT_FALSE(bs1 == bs2);
    ASSERT_TRUE(bs1 != bs2);

    bs2.set(50);
    ASSERT_TRUE(bs1 == bs2);
}

TEST(BitSet, CopyConstructorAndAssignment) {
    BitSet<64> bs1;
    bs1.set(10).set(20);

    // Copy constructor test
    BitSet<64> bs2(bs1);
    ASSERT_EQ(bs1.count(), bs2.count());
    ASSERT_TRUE(bs2.test(10));
    ASSERT_TRUE(bs2.test(20));
    ASSERT_TRUE(bs1 == bs2);

    // Assignment operator test
    BitSet<64> bs3;
    bs3.set(5);
    bs3 = bs1;
    ASSERT_EQ(bs1.count(), bs3.count());
    ASSERT_TRUE(bs3.test(10));
    ASSERT_TRUE(bs3.test(20));
    ASSERT_FALSE(bs3.test(5));
    ASSERT_TRUE(bs1 == bs3);
}

TEST(BitSet, BitwiseOperators) {
    BitSet<32> bs1;
    bs1.set(1).set(2);

    BitSet<32> bs2;
    bs2.set(2).set(3);

    BitSet<32> and_res = bs1 & bs2;
    ASSERT_FALSE(and_res.test(1));
    ASSERT_TRUE(and_res.test(2));
    ASSERT_FALSE(and_res.test(3));

    BitSet<32> or_res = bs1 | bs2;
    ASSERT_TRUE(or_res.test(1));
    ASSERT_TRUE(or_res.test(2));
    ASSERT_TRUE(or_res.test(3));

    BitSet<32> xor_res = bs1 ^ bs2;
    ASSERT_TRUE(xor_res.test(1));
    ASSERT_FALSE(xor_res.test(2));
    ASSERT_TRUE(xor_res.test(3));

    BitSet<32> not_res = ~bs1;
    ASSERT_FALSE(not_res.test(1));
    ASSERT_FALSE(not_res.test(2));
    ASSERT_TRUE(not_res.test(0));
    ASSERT_TRUE(not_res.test(31));
}

TEST(BitSet, BitwiseAssignmentOperators) {
    BitSet<32> bs1;
    bs1.set(1).set(2);

    BitSet<32> bs2;
    bs2.set(2).set(3);

    bs1 &= bs2;
    ASSERT_FALSE(bs1.test(1));
    ASSERT_TRUE(bs1.test(2));
    ASSERT_FALSE(bs1.test(3));

    bs1 |= bs2;
    ASSERT_FALSE(bs1.test(1));
    ASSERT_TRUE(bs1.test(2));
    ASSERT_TRUE(bs1.test(3));

    bs1 ^= bs2;
    ASSERT_FALSE(bs1.test(1));
    ASSERT_FALSE(bs1.test(2));
    ASSERT_FALSE(bs1.test(3));
}

TEST(BitSet, ShiftOperators) {
    BitSet<128> bs;
    bs.set(10).set(70);

    BitSet<128> left = bs << 5;
    ASSERT_TRUE(left.test(15));
    ASSERT_TRUE(left.test(75));
    ASSERT_FALSE(left.test(10));
    ASSERT_FALSE(left.test(70));

    BitSet<128> right = bs >> 5;
    ASSERT_TRUE(right.test(5));
    ASSERT_TRUE(right.test(65));
    ASSERT_FALSE(right.test(10));
    ASSERT_FALSE(right.test(70));

    BitSet<128> bs_shift;
    bs_shift.set(127);
    ASSERT_EQ((bs_shift << 1).count(), 0u);

    bs_shift.reset().set(0);
    ASSERT_EQ((bs_shift >> 1).count(), 0u);
}

TEST(BitSet, ShiftAssignmentOperators) {
    BitSet<128> bs;
    bs.set(10).set(70);

    bs <<= 5;
    ASSERT_TRUE(bs.test(15));
    ASSERT_TRUE(bs.test(75));
    ASSERT_FALSE(bs.test(10));
    ASSERT_FALSE(bs.test(70));

    bs >>= 10;
    ASSERT_TRUE(bs.test(5));
    ASSERT_TRUE(bs.test(65));
    ASSERT_FALSE(bs.test(15));
    ASSERT_FALSE(bs.test(75));

    bs <<= 1000;
    ASSERT_EQ(bs.count(), 0u);
}

TEST(BitSet, ReferenceProxy) {
    BitSet<20> bs;
    bs[5] = true;
    ASSERT_TRUE(bs.test(5));

    bs[5] = false;
    ASSERT_FALSE(bs.test(5));

    bs[10] = true;
    bs[15] = bs[10];
    ASSERT_TRUE(bs.test(15));

    bs[10] = ~bs[15];
    ASSERT_FALSE(bs.test(10));

    bool val = bs[15];
    ASSERT_TRUE(val);

    bs[15].flip();
    ASSERT_FALSE(bs.test(15));
}

TEST(BitSet, ToString) {
    BitSet<5> bs;
    bs.set(1).set(3);
    // position 0 is rightmost bit (index 4 in string length 5)
    // 43210
    // 01010
    ASSERT_EQ(bs.to_string(), "01010");
    ASSERT_EQ(bs.to_string('F', 'T'), "FTFTF");
}

TEST(BitSet, GetArray) {
    BitSet<65> bs;
    bs.set(0).set(64);

    const auto* arr = bs.data();
    ASSERT_EQ(bs.dataSize(), 16u);
    ASSERT_EQ(arr[0], std::byte{1}); // bit 0
    ASSERT_EQ(arr[8], std::byte{1}); // bit 64

    auto* mut_arr = bs.data();
    mut_arr[0] = std::byte{0};
    ASSERT_FALSE(bs.test(0));
}

TEST(BitSet, OutOfBounds) {
    BitSet<10> bs;
    EXPECT_ANY_THROW(bs.set(10));
    EXPECT_ANY_THROW(bs.reset(10));
    EXPECT_ANY_THROW(bs.flip(10));
    EXPECT_ANY_THROW(bs.test(10));
}

TEST(BitSet, CleanUnusedBits) {
    BitSet<10> bs;
    bs.set(); // should only set bottom 10 bits of the 64-bit word

    ASSERT_EQ(bs.dataSize(), 8u);
    const uint64_t* arr = reinterpret_cast<const uint64_t*>(bs.data());
    ASSERT_EQ(arr[0], (1ull << 10) - 1);

    bs.flip(); // should clear bottom 10 bits, upper 54 bits should remain 0
    ASSERT_EQ(arr[0], 0ull);
    ASSERT_TRUE(bs.none());

    bs.set();                // set 10 bits again
    BitSet<10> not_bs = ~bs; // complement should flip bottom 10 bits, keep upper 54 clean
    ASSERT_TRUE(not_bs.none());
}

} // namespace android