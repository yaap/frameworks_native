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

#include <android/hardware/graphics/bufferqueue/1.0/IGraphicBufferProducer.h>
#include <android/hardware/graphics/bufferqueue/1.0/IProducerListener.h>
#include <android/hardware/graphics/common/1.0/types.h>
#include <gui/Surface.h>
#include <hidl/HidlSupport.h>

#include <mutex>

namespace android {
namespace hardware {
namespace graphics {
namespace bufferqueue {
namespace V1_0 {
namespace utils {

using HGraphicBufferProducer =
        ::android::hardware::graphics::bufferqueue::V1_0::IGraphicBufferProducer;
using BGraphicBufferProducer = ::android::IGraphicBufferProducer;
using HProducerListener = ::android::hardware::graphics::bufferqueue::V1_0::IProducerListener;
using HConnectionType =
        ::android::hardware::graphics::bufferqueue::V1_0::IGraphicBufferProducer::DisconnectMode;

using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;

using ::android::hardware::media::V1_0::AnwBuffer;

/**
 * A wrapper around IGraphicBufferProducer that is backed by a Surface.
 */
class Surface2HGraphicBufferProducer final : public HGraphicBufferProducer {
public:
    Surface2HGraphicBufferProducer(const sp<Surface>& base);

    ::android::hardware::Return<void> requestBuffer(int32_t slot,
                                                    requestBuffer_cb _hidl_cb) override;

    ::android::hardware::Return<int32_t> setMaxDequeuedBufferCount(
            int32_t maxDequeuedBuffers) override;

    ::android::hardware::Return<int32_t> setAsyncMode(bool async) override;

    ::android::hardware::Return<void> dequeueBuffer(
            uint32_t width, uint32_t height,
            ::android::hardware::graphics::common::V1_0::PixelFormat format, uint32_t usage,
            bool getFrameTimestamps, dequeueBuffer_cb _hidl_cb) override;

    ::android::hardware::Return<int32_t> detachBuffer(int32_t slot) override;

    ::android::hardware::Return<void> detachNextBuffer(detachNextBuffer_cb _hidl_cb) override;

    ::android::hardware::Return<void> attachBuffer(
            const ::android::hardware::media::V1_0::AnwBuffer& buffer,
            attachBuffer_cb _hidl_cb) override;

    ::android::hardware::Return<void> queueBuffer(
            int32_t slot,
            const ::android::hardware::graphics::bufferqueue::V1_0::IGraphicBufferProducer::
                    QueueBufferInput& input,
            queueBuffer_cb _hidl_cb) override;

    ::android::hardware::Return<int32_t> cancelBuffer(
            int32_t slot, const ::android::hardware::hidl_handle& fence) override;

    ::android::hardware::Return<void> query(int32_t what, query_cb _hidl_cb) override;

    ::android::hardware::Return<void> connect(
            const ::android::sp<
                    ::android::hardware::graphics::bufferqueue::V1_0::IProducerListener>& listener,
            int32_t api, bool producerControlledByApp, connect_cb _hidl_cb) override;

    ::android::hardware::Return<int32_t> disconnect(
            int32_t api,
            ::android::hardware::graphics::bufferqueue::V1_0::IGraphicBufferProducer::DisconnectMode
                    mode) override;

    ::android::hardware::Return<int32_t> setSidebandStream(
            const ::android::hardware::hidl_handle& stream) override;

    ::android::hardware::Return<void> allocateBuffers(
            uint32_t width, uint32_t height,
            ::android::hardware::graphics::common::V1_0::PixelFormat format,
            uint32_t usage) override;

    ::android::hardware::Return<int32_t> allowAllocation(bool allow) override;

    ::android::hardware::Return<int32_t> setGenerationNumber(uint32_t generationNumber) override;

    ::android::hardware::Return<void> getConsumerName(getConsumerName_cb _hidl_cb) override;

    ::android::hardware::Return<int32_t> setSharedBufferMode(bool sharedBufferMode) override;

    ::android::hardware::Return<int32_t> setAutoRefresh(bool autoRefresh) override;

    ::android::hardware::Return<int32_t> setDequeueTimeout(int64_t timeoutNs) override;

    ::android::hardware::Return<void> getLastQueuedBuffer(getLastQueuedBuffer_cb _hidl_cb) override;

    ::android::hardware::Return<void> getFrameTimestamps(getFrameTimestamps_cb _hidl_cb) override;

    ::android::hardware::Return<void> getUniqueId(getUniqueId_cb _hidl_cb) override;

private:
    class HidlBridgeListener;
    struct Obituary;

    struct SlotInfo {
        bool needsRefresh;

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

    // Stored on connect or when fetched. Used for debugging.
    std::string mConsumerName GUARDED_BY(mMutex) = "unknown-not-connected";

    // HGBPs can only manage 64 buffers at a time. Instead of juggling some nicer datastructures,
    // we'll save space and cache hits by just iterating over a small array for all our operations.
    SlotInfo mSlotToInfo[BufferQueueDefs::NUM_BUFFER_SLOTS] GUARDED_BY(mMutex);

    sp<Surface> mBase;
    sp<Obituary> mObituary GUARDED_BY(mMutex);
};

} // namespace utils
} // namespace V1_0
} // namespace bufferqueue
} // namespace graphics
} // namespace hardware
} // namespace android