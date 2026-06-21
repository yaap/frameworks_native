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

#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <android-base/thread_annotations.h>
#include <binder/IBinder.h>
#include <ftl/future.h>
#include <ftl/small_map.h>
#include <ftl/small_vector.h>
#include <gui/BufferReleaseChannel.h>
#include <gui/CornerRadii.h>
#include <gui/ITransactionCompletedListener.h>
#include <renderengine/ExternalTexture.h>
#include <ui/Fence.h>
#include <ui/FenceResult.h>

#include "Utils/FenceUtils.h"

namespace android {

class CallbackHandle {
public:
    CallbackHandle(const sp<IBinder>& transactionListener, const std::vector<CallbackId>& ids,
                   const sp<IBinder>& sc);

    sp<IBinder> listener;
    std::vector<CallbackId> callbackIds;
    wp<IBinder> surfaceControl;

    bool releasePreviousBuffer = false;
    std::string name;
    std::variant<nsecs_t, sp<Fence>> acquireTimeOrFence = -1;
    nsecs_t latchTime = -1;
    std::optional<uint32_t> transformHint = std::nullopt;
    uint32_t currentMaxAcquiredBufferCount = 0;
    std::optional<gui::CornerRadii> cornerRadii = std::nullopt;
    std::shared_ptr<FenceTime> gpuCompositionDoneFence{FenceTime::NO_FENCE};
    CompositorTiming compositorTiming;
    nsecs_t refreshStartTime = 0;
    nsecs_t dequeueReadyTime = 0;
    uint64_t frameNumber = 0;
    uint64_t previousFrameNumber = 0;
    ReleaseCallbackId previousReleaseCallbackId = ReleaseCallbackId::INVALID_ID;
    std::shared_ptr<gui::BufferReleaseChannel::ProducerEndpoint> bufferReleaseChannel;
    std::weak_ptr<renderengine::ExternalTexture> previousBuffer;
    FenceMerger fenceMerger;
};

class TransactionCallbackInvoker {
public:
    void addCallbackHandles(std::vector<CallbackHandle>&& handles);
    void addOnCommitCallbackHandles(std::vector<CallbackHandle>& handles);

    void addEmptyTransaction(const ListenerCallbacks& listenerCallbacks);

    void addPresentFence(sp<Fence>);

    void sendCallbacks(bool onCommitOnly);
    void clearCompletedTransactions() {
        mCompletedTransactions.clear();
    }

    void addCallbackHandle(CallbackHandle&& handle);

private:
    TransactionStats& findOrCreateTransactionStats(const sp<IBinder>& listener,
                                                   const std::vector<CallbackId>& callbackIds);

    ftl::SmallVector<std::pair<sp<IBinder>, TransactionStats>, 10> mCompletedTransactions;

    struct BufferRelease {
        std::string layerName;
        std::shared_ptr<gui::BufferReleaseChannel::ProducerEndpoint> channel;
        ReleaseCallbackId callbackId;
        sp<Fence> fence;
        uint32_t currentMaxAcquiredBufferCount;
        std::weak_ptr<renderengine::ExternalTexture> previousBuffer;
    };

    ftl::SmallMap<ReleaseCallbackId, FenceMerger, 20> mReleaseIdToFenceMerger;
    std::vector<BufferRelease> mBufferReleases;

    sp<Fence> mPresentFence;
};

} // namespace android
