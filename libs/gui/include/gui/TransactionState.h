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

#include <android/gui/FrameTimelineInfo.h>
#include <binder/Parcelable.h>
#include <gui/LayerState.h>
#include <gui/SimpleTransactionState.h>

namespace android {

struct TransactionListenerCallbacks {
    std::vector<ListenerCallbacks> mFlattenedListenerCallbacks;
    // Note: mHasListenerCallbacks can be true even if mFlattenedListenerCallbacks is
    // empty because it reflects the unflattened map.
    bool mHasListenerCallbacks = false;

    status_t writeToParcel(Parcel* parcel) const;
    status_t readFromParcel(const Parcel* parcel);
    void clear();
    bool operator==(const TransactionListenerCallbacks& rhs) const = default;
    bool operator!=(const TransactionListenerCallbacks& rhs) const = default;
};

// State components that are containers or aggregates and are passed by const ref to
// setTransactionState.
struct ComplexTransactionState {
    // The vsync id provided by Choreographer.getVsyncId and the input event id
    gui::FrameTimelineInfo mFrameTimelineInfo = {};
    std::vector<client_cache_t> mUncacheBuffers = {};
    std::vector<uint64_t> mMergedTransactionIds = {};
    TransactionListenerCallbacks mCallbacks = TransactionListenerCallbacks();
    InputWindowCommands mInputWindowCommands = {};
    // Tracks the client setting the early wakeup request
    std::vector<gui::EarlyWakeupInfo> mEarlyWakeupInfos = {};

    ComplexTransactionState() = default;
    status_t writeToParcel(Parcel* parcel) const;
    status_t readFromParcel(const Parcel* parcel);

    void clear();
    void merge(ComplexTransactionState& other);
    bool operator==(const ComplexTransactionState& rhs) const = default;
    bool operator!=(const ComplexTransactionState& rhs) const = default;

    // copied from FrameTimelineInfo::merge()
    void mergeFrameTimelineInfo(const FrameTimelineInfo& other);

private:
    // We keep track of the last MAX_MERGE_HISTORY_LENGTH merged transaction ids.
    // Ordered most recently merged to least recently merged.
    static constexpr size_t MAX_MERGE_HISTORY_LENGTH = 10u;
};

struct MutableTransactionState {
    Vector<ComposerState> mComposerStates;
    Vector<DisplayState> mDisplayStates;

    void clear();
    status_t writeToParcel(Parcel* parcel) const;
    status_t readFromParcel(const Parcel* parcel);
    void merge(const MutableTransactionState& other,
               const std::function<void(const layer_state_t&)>& onBufferOverwrite);
    layer_state_t* getLayerState(const sp<SurfaceControl>& sc);
    DisplayState& getDisplayState(const sp<IBinder>& token);
};

// Class to store all the transaction data and the parcelling logic
class TransactionState {
public:
    explicit TransactionState() = default;
    TransactionState(TransactionState const& other) = default;
    status_t writeToParcel(Parcel* parcel) const;
    status_t readFromParcel(const Parcel* parcel);
    layer_state_t* getLayerState(const sp<SurfaceControl>& sc);
    DisplayState& getDisplayState(const sp<IBinder>& token);

    // Returns the current id of the transaction.
    // The id is updated every time the transaction is applied.
    uint64_t getId() const { return mSimpleState.mId; }
    std::vector<uint64_t> getMergedTransactionIds() const {
        return mComplexState.mMergedTransactionIds;
    }
    void enableDebugLogCallPoints() { mLogCallPoints = true; }
    void merge(TransactionState&& other,
               const std::function<void(layer_state_t&)>& onBufferOverwrite);

    void clear();
    bool operator==(const TransactionState& rhs) const = default;
    bool operator!=(const TransactionState& rhs) const = default;

    SimpleTransactionState mSimpleState;
    ComplexTransactionState mComplexState;

    // If not null, transactions will be queued up using this token otherwise a common token
    // per process will be used.
    sp<IBinder> mApplyToken;
    // Indicates that the Transaction may contain buffers that should be cached. The reason this
    // is only a guess is that buffers can be removed before cache is called. This is only a
    // hint that at some point a buffer was added to this transaction before apply was called.
    bool mMayContainBuffer = false;
    // Prints debug logs when enabled.
    bool mLogCallPoints = false;

    std::vector<DisplayState> mDisplayStates;
    std::vector<ComposerState> mComposerStates;
};

}; // namespace android
