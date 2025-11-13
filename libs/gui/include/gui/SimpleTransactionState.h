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

#include <cstdint>

#include <gui/LayerState.h>

namespace android {

// Defined in its own header to avoid circular dependencies in includes.
struct SimpleTransactionState {
    uint64_t mId = 0;
    uint32_t mFlags = 0;
    // mDesiredPresentTime is the time in nanoseconds that the client would like the transaction
    // to be presented. When it is not possible to present at exactly that time, it will be
    // presented after the time has passed.
    //
    // If the client didn't pass a desired presentation time, mDesiredPresentTime will be
    // populated to the time setBuffer was called, and mIsAutoTimestamp will be set to true.
    //
    // Desired present times that are more than 1 second in the future may be ignored.
    // When a desired present time has already passed, the transaction will be presented as soon
    // as possible.
    //
    // Transactions from the same process are presented in the same order that they are applied.
    // The desired present time does not affect this ordering.
    int64_t mDesiredPresentTime = 0;
    bool mIsAutoTimestamp = true;

    SimpleTransactionState() = default;
    SimpleTransactionState(uint64_t id, uint32_t flags, int64_t desiredPresentTime,
                           bool isAutoTimestamp)
          : mId(id),
            mFlags(flags),
            mDesiredPresentTime(desiredPresentTime),
            mIsAutoTimestamp(isAutoTimestamp) {}
    bool operator==(const SimpleTransactionState& rhs) const = default;
    bool operator!=(const SimpleTransactionState& rhs) const = default;

    status_t writeToParcel(Parcel* parcel) const;
    status_t readFromParcel(const Parcel* parcel);
    void merge(const SimpleTransactionState& other);
    void clear();
};

} // namespace android
