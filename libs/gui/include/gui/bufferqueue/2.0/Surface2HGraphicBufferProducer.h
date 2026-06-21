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

#pragma once

#include <android/hardware/graphics/bufferqueue/2.0/IGraphicBufferProducer.h>
#include <gui/Surface.h>
#include <gui/bufferqueue/2.0/types.h>
#include <hidl/HidlSupport.h>

#include <mutex>

namespace android::hardware::graphics::bufferqueue::V2_0::utils {

using HGraphicBufferProducer =
        ::android::hardware::graphics::bufferqueue::V2_0::IGraphicBufferProducer;
using BGraphicBufferProducer = ::android::IGraphicBufferProducer;
using HProducerListener = ::android::hardware::graphics::bufferqueue::V2_0::IProducerListener;

using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::graphics::common::V1_2::HardwareBuffer;

/**
 * A wrapper around IGraphicBufferProducer that is backed by a Surface.
 */
class Surface2HGraphicBufferProducer : public HGraphicBufferProducer {
public:
    Surface2HGraphicBufferProducer(const sp<Surface>& base);

    status_t getConsumerUsage(uint64_t* outUsage);
    status_t attachGraphicBuffer(int* outSlot, const sp<GraphicBuffer>& buffer);
    status_t queueGraphicBuffer(int slot, const SurfaceQueueBufferInput& input,
                                SurfaceQueueBufferOutput* output);
    status_t cancelBufferSimple(int slot, const sp<::android::Fence>& fence);
    void enableFrameTimestamps(bool enable);

    struct FrameTimestamps {
        nsecs_t requestedPresentTime;
        nsecs_t acquireTime;
        nsecs_t latchTime;
        nsecs_t firstRefreshStartTime;
        nsecs_t lastRefreshStartTime;
        nsecs_t gpuCompositionDoneTime;
        nsecs_t displayPresentTime;
        nsecs_t dequeueReadyTime;
        nsecs_t releaseTime;
    };
    status_t getFrameTimestamps(uint64_t frameNumber, FrameTimestamps* info);

    // HGraphicBufferProducer:
    virtual Return<HStatus> setMaxDequeuedBufferCount(int32_t maxDequeuedBuffers) override;

    virtual Return<void> requestBuffer(int32_t slot, requestBuffer_cb _hidl_cb) override;

    virtual Return<HStatus> setAsyncMode(bool async) override;

    virtual Return<void> dequeueBuffer(DequeueBufferInput const& input,
                                       dequeueBuffer_cb _hidl_cb) override;

    virtual Return<HStatus> detachBuffer(int32_t slot) override;

    virtual Return<void> detachNextBuffer(detachNextBuffer_cb _hidl_cb) override;

    virtual Return<void> attachBuffer(HardwareBuffer const& buffer, uint32_t generationNumber,
                                      attachBuffer_cb _hidl_cb) override;

    virtual Return<void> queueBuffer(int32_t slot, QueueBufferInput const& input,
                                     queueBuffer_cb _hidl_cb) override;

    virtual Return<HStatus> cancelBuffer(int32_t slot, hidl_handle const& fence) override;

    virtual Return<void> query(int32_t what, query_cb _hidl_cb) override;

    virtual Return<void> connect(const sp<HProducerListener>& listener, HConnectionType api,
                                 bool producerControlledByApp, connect_cb _hidl_cb) override;

    virtual Return<HStatus> disconnect(HConnectionType api) override;

    virtual Return<HStatus> allocateBuffers(uint32_t width, uint32_t height, uint32_t format,
                                            uint64_t usage) override;

    virtual Return<HStatus> allowAllocation(bool allow) override;

    virtual Return<HStatus> setGenerationNumber(uint32_t generationNumber) override;

    virtual Return<HStatus> setDequeueTimeout(int64_t timeoutNs) override;

    virtual Return<uint64_t> getUniqueId() override;

    virtual Return<void> getConsumerName(getConsumerName_cb _hidl_cb) override;

private:
    class HidlBridgeListener;
    struct Obituary;

    struct SlotInfo {
        bool isDequeued;
        sp<GraphicBuffer> buffer;
    };

    void onBuffersDiscarded(const std::vector<sp<GraphicBuffer>>& buffers);
    void onRemoteDied();

    int getSlotForBufferLocked(const sp<GraphicBuffer>& buffer) REQUIRES(mMutex);
    int getFreeOrEvictableSlotLocked() REQUIRES(mMutex);
    void resetSlotsLocked() REQUIRES(mMutex);
    void clearSlotLocked(int slot) REQUIRES(mMutex);

    mutable std::mutex mMutex;

    // HGBPs can only manage 64 buffers at a time. Instead of juggling some nicer datastructures,
    // we'll save space and cache hits by just iterating over a small array for all our operations.
    SlotInfo mSlotToInfo[BufferQueueDefs::NUM_BUFFER_SLOTS] GUARDED_BY(mMutex);

    // Stored on connect or when fetched. Used for debugging.
    std::string mConsumerName GUARDED_BY(mMutex) = "unknown-not-connected";
    bool mEnableFrameTimestamps GUARDED_BY(mMutex) = false;

    sp<Surface> mBase;
    sp<Obituary> mObituary GUARDED_BY(mMutex);
};

} // namespace android::hardware::graphics::bufferqueue::V2_0::utils