/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace android {

/**
 * A fixed-size bitset similar to std::bitset but with a few differences:
 * No constructors from or conversions to an unsigned long long
 * Allows exposing the underlying storage object for easy serialization & deserialization
 * Has an implicit bool that behaves the same as `any()`
 * Is constexpr on all standards (we're not quite C++23 everywhere yet)
 */
template <size_t N>
class BitSet {
private:
    static constexpr size_t kBitsPerWord = 64;
    static constexpr size_t kNumWords = (N == 0) ? 0 : (N - 1) / kBitsPerWord + 1;
    using WordT = uint64_t;
    std::array<WordT, kNumWords> mBits = {};

    constexpr void clean_unused_bits() noexcept {
        if constexpr (N > 0 && N % kBitsPerWord != 0) {
            constexpr WordT mask = (WordT(1) << (N % kBitsPerWord)) - 1;
            mBits[kNumWords - 1] &= mask;
        }
    }

public:
    class reference {
        friend class BitSet;
        BitSet* mBitSet;
        size_t mPos;

        constexpr reference(BitSet* bitset, size_t pos) : mBitSet(bitset), mPos(pos) {}

    public:
        constexpr reference& operator=(bool x) noexcept {
            mBitSet->set(mPos, x);
            return *this;
        }
        constexpr reference& operator=(const reference& x) noexcept {
            mBitSet->set(mPos, x.mBitSet->test(x.mPos));
            return *this;
        }
        constexpr bool operator~() const noexcept { return !mBitSet->test(mPos); }
        constexpr operator bool() const noexcept { return mBitSet->test(mPos); }
        constexpr reference& flip() noexcept {
            mBitSet->flip(mPos);
            return *this;
        }
    };

    explicit constexpr BitSet() = default;
    explicit constexpr BitSet(std::initializer_list<size_t> list) {
        for (size_t pos : list) {
            set(pos);
        }
    }

    constexpr BitSet(const BitSet&) = default;
    constexpr BitSet(BitSet&&) = default;
    BitSet& operator=(const BitSet&) = default;
    BitSet& operator=(BitSet&&) = default;

    constexpr const std::byte* data() const noexcept {
        return reinterpret_cast<const std::byte*>(mBits.data());
    }
    constexpr std::byte* data() noexcept { return reinterpret_cast<std::byte*>(mBits.data()); }
    constexpr size_t dataSize() const noexcept { return mBits.size() * sizeof(WordT); }

    constexpr bool operator==(const BitSet& other) const noexcept {
        for (size_t i = 0; i < kNumWords; ++i) {
            if (mBits[i] != other.mBits[i]) return false;
        }
        return true;
    }

    constexpr bool operator!=(const BitSet& other) const noexcept { return !(*this == other); }

    constexpr bool test(size_t pos) const {
        if (pos >= N) std::__throw_overflow_error("BitSet::test");
        return (mBits[pos / kBitsPerWord] & (WordT(1) << (pos % kBitsPerWord))) != 0;
    }

    constexpr bool all() const noexcept {
        if constexpr (N == 0) return true;
        for (size_t i = 0; i < kNumWords - 1; ++i) {
            if (mBits[i] != ~WordT(0)) return false;
        }
        constexpr WordT mask =
                (N % kBitsPerWord == 0) ? ~WordT(0) : (WordT(1) << (N % kBitsPerWord)) - 1;
        return (mBits[kNumWords - 1] & mask) == mask;
    }

    constexpr bool any() const noexcept {
        for (size_t i = 0; i < kNumWords; ++i) {
            if (mBits[i] != 0) return true;
        }
        return false;
    }

    constexpr bool none() const noexcept { return !any(); }

    constexpr operator bool() const noexcept { return any(); }

    // This is a bit of a hack to prevent implicit conversion to integral types, while
    // still allowing implicit conversion to bool. The implicit conversion to bool is used
    // much too broadly to switch to explicit, and without this there would be a silent implicit
    // conversion to uint64_t by converting to bool -> uint64_t, which is definitely wrong
    template <typename T,
              typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
    constexpr operator T() const noexcept = delete;

    constexpr size_t count() const noexcept {
        size_t c = 0;
        for (size_t i = 0; i < kNumWords; ++i) {
            c += __builtin_popcountll(mBits[i]);
        }
        return c;
    }

    constexpr size_t size() const noexcept { return N; }

    constexpr bool operator[](size_t pos) const noexcept {
        return (mBits[pos / kBitsPerWord] & (WordT(1) << (pos % kBitsPerWord))) != 0;
    }

    constexpr reference operator[](size_t pos) noexcept { return reference(this, pos); }

    constexpr BitSet& set() noexcept {
        for (size_t i = 0; i < kNumWords; ++i) {
            mBits[i] = ~WordT(0);
        }
        clean_unused_bits();
        return *this;
    }

    constexpr BitSet& set(size_t pos, bool value = true) {
        if (pos >= N) std::__throw_overflow_error("BitSet::set");
        if (value) {
            mBits[pos / kBitsPerWord] |= (WordT(1) << (pos % kBitsPerWord));
        } else {
            mBits[pos / kBitsPerWord] &= ~(WordT(1) << (pos % kBitsPerWord));
        }
        return *this;
    }

    constexpr BitSet& reset() noexcept {
        for (size_t i = 0; i < kNumWords; ++i) {
            mBits[i] = 0;
        }
        return *this;
    }

    constexpr BitSet& reset(size_t pos) {
        if (pos >= N) std::__throw_overflow_error("BitSet::reset");
        mBits[pos / kBitsPerWord] &= ~(WordT(1) << (pos % kBitsPerWord));
        return *this;
    }

    constexpr BitSet& flip() noexcept {
        for (size_t i = 0; i < kNumWords; ++i) {
            mBits[i] = ~mBits[i];
        }
        clean_unused_bits();
        return *this;
    }

    constexpr BitSet& flip(size_t pos) {
        if (pos >= N) std::__throw_overflow_error("BitSet::flip");
        mBits[pos / kBitsPerWord] ^= (WordT(1) << (pos % kBitsPerWord));
        return *this;
    }

    constexpr BitSet& operator&=(const BitSet& other) noexcept {
        for (size_t i = 0; i < kNumWords; ++i) {
            mBits[i] &= other.mBits[i];
        }
        return *this;
    }

    constexpr BitSet& operator|=(const BitSet& other) noexcept {
        for (size_t i = 0; i < kNumWords; ++i) {
            mBits[i] |= other.mBits[i];
        }
        return *this;
    }

    constexpr BitSet& operator^=(const BitSet& other) noexcept {
        for (size_t i = 0; i < kNumWords; ++i) {
            mBits[i] ^= other.mBits[i];
        }
        return *this;
    }

    constexpr BitSet operator~() const noexcept {
        BitSet result(*this);
        result.flip();
        return result;
    }

    std::string to_string(char zero = '0', char one = '1') const {
        std::string str(N, zero);
        for (size_t i = 0; i < N; ++i) {
            if (test(i)) {
                str[N - 1 - i] = one;
            }
        }
        return str;
    }

    constexpr BitSet& operator<<=(size_t shift) noexcept {
        if (shift == 0) return *this;
        if (shift >= N) {
            reset();
            return *this;
        }
        const size_t word_shift = shift / kBitsPerWord;
        const size_t bit_shift = shift % kBitsPerWord;

        if (bit_shift == 0) {
            for (size_t i = kNumWords; i-- > word_shift;) {
                mBits[i] = mBits[i - word_shift];
            }
        } else {
            for (size_t i = kNumWords; i-- > word_shift + 1;) {
                mBits[i] = (mBits[i - word_shift] << bit_shift) |
                        (mBits[i - word_shift - 1] >> (kBitsPerWord - bit_shift));
            }
            mBits[word_shift] = mBits[0] << bit_shift;
        }
        for (size_t i = 0; i < word_shift; ++i) {
            mBits[i] = 0;
        }
        clean_unused_bits();
        return *this;
    }

    constexpr BitSet& operator>>=(size_t shift) noexcept {
        if (shift == 0) return *this;
        if (shift >= N) {
            reset();
            return *this;
        }
        const size_t word_shift = shift / kBitsPerWord;
        const size_t bit_shift = shift % kBitsPerWord;

        if (bit_shift == 0) {
            for (size_t i = 0; i < kNumWords - word_shift; ++i) {
                mBits[i] = mBits[i + word_shift];
            }
        } else {
            for (size_t i = 0; i < kNumWords - word_shift - 1; ++i) {
                mBits[i] = (mBits[i + word_shift] >> bit_shift) |
                        (mBits[i + word_shift + 1] << (kBitsPerWord - bit_shift));
            }
            mBits[kNumWords - word_shift - 1] = mBits[kNumWords - 1] >> bit_shift;
        }
        for (size_t i = kNumWords - word_shift; i < kNumWords; ++i) {
            mBits[i] = 0;
        }
        return *this;
    }

    template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    constexpr BitSet operator<<(T pos) const noexcept {
        BitSet result(*this);
        result <<= static_cast<size_t>(pos);
        return result;
    }

    template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
    constexpr BitSet operator>>(T pos) const noexcept {
        BitSet result(*this);
        result >>= static_cast<size_t>(pos);
        return result;
    }

    friend constexpr BitSet operator&(const BitSet& lhs, const BitSet& rhs) noexcept {
        BitSet result(lhs);
        result &= rhs;
        return result;
    }

    friend constexpr BitSet operator|(const BitSet& lhs, const BitSet& rhs) noexcept {
        BitSet result(lhs);
        result |= rhs;
        return result;
    }

    friend constexpr BitSet operator^(const BitSet& lhs, const BitSet& rhs) noexcept {
        BitSet result(lhs);
        result ^= rhs;
        return result;
    }
};

} // namespace android