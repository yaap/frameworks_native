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

// #define LOG_NDEBUG 0
#define LOG_TAG "Surface2HGraphicBufferProducer@2.0"

#include <android-base/logging.h>

#include <android/hardware/graphics/bufferqueue/2.0/types.h>
#include <android/hardware/graphics/common/1.2/types.h>
#include <gui/BufferQueue.h>
#include <gui/Surface.h>
#include <gui/bufferqueue/2.0/H2BProducerListener.h>
#include <gui/bufferqueue/2.0/types.h>
#include <ui/BufferQueueDefs.h>
#include <ui/GraphicBuffer.h>
#include <ui/Rect.h>
#include <ui/Region.h>
#include <vndk/hardware_buffer.h>

#include <mutex>

#include <gui/bufferqueue/2.0/Surface2HGraphicBufferProducer.h>

namespace android::hardware::graphics::bufferqueue::V2_0::utils {

namespace {
using ::android::hardware::graphics::bufferqueue::V2_0::utils::b2h;
using ::android::hardware::graphics::bufferqueue::V2_0::utils::h2b;
} // unnamed namespace

class Surface2HGraphicBufferProducer::HidlBridgeListener final : public SurfaceListener {
public:
    HidlBridgeListener(const sp<Surface2HGraphicBufferProducer>& producer,
                       const sp<HProducerListener>& listener)
          : mProducer(producer), mListener(listener) {}

    virtual bool needsReleaseNotify() override { return true; }
    virtual void onBufferReleased() override { mListener->onBuffersReleased(1); }

    virtual void onBufferDetached(uint64_t /*bufferId*/) override {}

    virtual void onBuffersDiscarded(const std::vector<sp<GraphicBuffer>>& buffers) override {
        if (auto producer = mProducer.promote()) {
            producer->onBuffersDiscarded(buffers);
        }
    }

    virtual void onRemoteDied() override {
        if (auto producer = mProducer.promote()) {
            producer->onRemoteDied();
        }
    }
    virtual bool needsDeathNotify() override { return true; }

private:
    wp<Surface2HGraphicBufferProducer> mProducer;
    const sp<HProducerListener> mListener;
};

struct Surface2HGraphicBufferProducer::Obituary : public hardware::hidl_death_recipient {
    wp<Surface2HGraphicBufferProducer> producer;
    sp<HProducerListener> listener;
    HConnectionType apiType;

    Obituary(const wp<Surface2HGraphicBufferProducer>& p, const sp<HProducerListener>& l,
             HConnectionType t)
          : producer(p), listener(l), apiType(t) {}

    void serviceDied(uint64_t /* cookie */,
                     const wp<::android::hidl::base::V1_0::IBase>& /* who */) override {
        sp<Surface2HGraphicBufferProducer> dr = producer.promote();
        if (dr != nullptr) {
            (void)dr->disconnect(apiType);
        }
    }
};

Surface2HGraphicBufferProducer::Surface2HGraphicBufferProducer(const sp<Surface>& base)
      : mBase(base) {
    resetSlotsLocked();
}

status_t Surface2HGraphicBufferProducer::getConsumerUsage(uint64_t* outUsage) {
    if (outUsage == nullptr) {
        ALOGE("Surface2HGraphicBufferProducer::getConsumerUsage outUsage is null");
        return BAD_VALUE;
    }
    return mBase->getConsumerUsage(outUsage);
}

status_t Surface2HGraphicBufferProducer::attachGraphicBuffer(int* outSlot,
                                                             const sp<GraphicBuffer>& buffer) {
    if (outSlot == nullptr) {
        ALOGE("Surface2HGraphicBufferProducer::attachGraphicBuffer outSlot is null");
        return BAD_VALUE;
    }
    if (buffer == nullptr) {
        ALOGE("Surface2HGraphicBufferProducer::attachGraphicBuffer buffer is null");
        return BAD_VALUE;
    }

    std::scoped_lock _l(mMutex);

    int slot = getFreeOrEvictableSlotLocked();
    if (slot == BufferQueue::INVALID_BUFFER_SLOT) {
        return NO_MEMORY;
    }

    status_t ret = mBase->attachBuffer(buffer);
    if (ret != NO_ERROR) {
        ALOGE("Surface2HGraphicBufferProducer::attachGraphicBuffer failed: %d", ret);
        return ret;
    }

    mSlotToInfo[slot] = {.isDequeued = true, .buffer = buffer};
    *outSlot = slot;
    return ret;
}

status_t Surface2HGraphicBufferProducer::queueGraphicBuffer(int slot,
                                                            const SurfaceQueueBufferInput& input,
                                                            SurfaceQueueBufferOutput* output) {
    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        ALOGE("Surface2HGraphicBufferProducer::queueGraphicBuffer Invalid slot: %d", slot);
        return BAD_VALUE;
    }

    std::scoped_lock _l(mMutex);

    if (!mSlotToInfo[slot].isDequeued) {
        ALOGE("Surface2HGraphicBufferProducer::queueGraphicBuffer slot %d is not dequeued", slot);
        return BAD_VALUE;
    }
    if (!mSlotToInfo[slot].buffer) {
        ALOGE("Surface2HGraphicBufferProducer::queueGraphicBuffer slot %d has no buffer", slot);
        return BAD_VALUE;
    }

    status_t ret = mBase->queueBuffer(mSlotToInfo[slot].buffer, input, output);
    if (ret != NO_ERROR) {
        ALOGE("Surface2HGraphicBufferProducer::queueGraphicBuffer failed: %d", ret);
        return ret;
    }
    mSlotToInfo[slot].isDequeued = false;
    return ret;
}

status_t Surface2HGraphicBufferProducer::cancelBufferSimple(int slot,
                                                            const sp<::android::Fence>& fence) {
    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        ALOGE("Surface2HGraphicBufferProducer::queueGraphicBuffer Invalid slot: %d", slot);
        return BAD_VALUE;
    }

    std::scoped_lock _l(mMutex);

    if (!mSlotToInfo[slot].isDequeued) {
        ALOGE("Surface2HGraphicBufferProducer::queueGraphicBuffer slot %d is not dequeued", slot);
        return BAD_VALUE;
    }
    if (!mSlotToInfo[slot].buffer) {
        ALOGE("Surface2HGraphicBufferProducer::queueGraphicBuffer slot %d has no buffer", slot);
        return BAD_VALUE;
    }

    status_t ret = mBase->cancelBuffer(mSlotToInfo[slot].buffer, fence);
    if (ret != NO_ERROR) {
        ALOGE("Surface2HGraphicBufferProducer::cancelBufferSimple failed: %d", ret);
        return ret;
    }
    mSlotToInfo[slot].isDequeued = false;

    return ret;
}

void Surface2HGraphicBufferProducer::enableFrameTimestamps(bool enable) {
    std::scoped_lock _l(mMutex);
    mEnableFrameTimestamps = enable;
    mBase->enableFrameTimestamps(enable);
}

status_t Surface2HGraphicBufferProducer::getFrameTimestamps(uint64_t frameNumber,
                                                            FrameTimestamps* frameTimestamps) {
    if (frameTimestamps == NULL) {
        ALOGE("Surface2HGraphicBufferProducer::getFrameTimestamps frameTimestamps was null");
        return BAD_VALUE;
    }
    return mBase->getFrameTimestamps(frameNumber, &(frameTimestamps->requestedPresentTime),
                                     &(frameTimestamps->acquireTime), &(frameTimestamps->latchTime),
                                     &(frameTimestamps->firstRefreshStartTime),
                                     &(frameTimestamps->lastRefreshStartTime),
                                     &(frameTimestamps->gpuCompositionDoneTime),
                                     &(frameTimestamps->displayPresentTime),
                                     &(frameTimestamps->dequeueReadyTime),
                                     &(frameTimestamps->releaseTime));
}

Return<HStatus> Surface2HGraphicBufferProducer::setMaxDequeuedBufferCount(
        int32_t maxDequeuedBuffers) {
    // Since we only provide NUM_BUFFER_SLOTS buffers, we can't allow more than that.
    if (maxDequeuedBuffers < 0 || maxDequeuedBuffers > BufferQueueDefs::NUM_BUFFER_SLOTS) {
        ALOGE("Surface2HGraphicBufferProducer::setMaxDequeuedBufferCount Invalid "
              "maxDequeuedBuffers: %d",
              maxDequeuedBuffers);
        return {HStatus::BAD_VALUE};
    }

    HStatus hStatus{};
    bool converted =
            b2h(mBase->setMaxDequeuedBufferCount(static_cast<int>(maxDequeuedBuffers)), &hStatus);
    return {converted ? hStatus : HStatus::UNKNOWN_ERROR};
}

// TODO(b/476142579): This is a little funky because slots only make sense within the context of a
// single Surface2HGraphicBufferProducer instance. We should consider having a 1:1 map between
// Surfaces and Surface2HGraphicBufferProducer instances. However, this breaks on IPC usecases.
Return<void> Surface2HGraphicBufferProducer::requestBuffer(int32_t slot,
                                                           requestBuffer_cb _hidl_cb) {
    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        ALOGE("Surface2HGraphicBufferProducer::requestBuffer Invalid slot: %d", slot);
        _hidl_cb(HStatus::BAD_VALUE, HardwareBuffer{}, 0);
        return {};
    }

    std::scoped_lock _l(mMutex);
    sp<GraphicBuffer> buffer = mSlotToInfo[slot].buffer;
    if (buffer == nullptr) {
        ALOGE("Surface2HGraphicBufferProducer::requestBuffer Invalid slot: %d", slot);
        _hidl_cb(HStatus::BAD_VALUE, HardwareBuffer{}, 0);
        return {};
    }

    HStatus hStatus{};
    HardwareBuffer hBuffer{};
    uint32_t hGenerationNumber{};
    bool converted = b2h(buffer, &hBuffer, &hGenerationNumber);
    _hidl_cb(converted ? hStatus : HStatus::UNKNOWN_ERROR, hBuffer, hGenerationNumber);
    return {};
}

Return<HStatus> Surface2HGraphicBufferProducer::setAsyncMode(bool async) {
    HStatus hStatus{};
    bool converted = b2h(mBase->setAsyncMode(async), &hStatus);
    return {converted ? hStatus : HStatus::UNKNOWN_ERROR};
}

Return<void> Surface2HGraphicBufferProducer::dequeueBuffer(DequeueBufferInput const& input,
                                                           dequeueBuffer_cb _hidl_cb) {
    std::scoped_lock _l(mMutex);

    status_t status = NO_ERROR;
    HStatus hStatus{};

    status = mBase->setBuffersDimensions(input.height, input.width);
    if (status != NO_ERROR) {
        ALOGE("Surface2HGraphicBufferProducer::dequeueBuffer setBuffersDimensions failed: %d",
              status);
        _hidl_cb(b2h(status, &hStatus) ? hStatus : HStatus::UNKNOWN_ERROR, -1,
                 DequeueBufferOutput{});
        return {};
    }
    status = mBase->setBuffersFormat(static_cast<PixelFormat>(input.format));
    if (status != NO_ERROR) {
        ALOGE("Surface2HGraphicBufferProducer::dequeueBuffer setBuffersFormat failed: %d", status);
        _hidl_cb(b2h(status, &hStatus) ? hStatus : HStatus::UNKNOWN_ERROR, -1,
                 DequeueBufferOutput{});
        return {};
    }
    status = mBase->setUsage(input.usage);
    if (status != NO_ERROR) {
        ALOGE("Surface2HGraphicBufferProducer::dequeueBuffer setUsage failed: %d", status);
        _hidl_cb(b2h(status, &hStatus) ? hStatus : HStatus::UNKNOWN_ERROR, -1,
                 DequeueBufferOutput{});
        return {};
    }

    sp<GraphicBuffer> buffer;
    sp<BFence> fence;
    status = mBase->dequeueBuffer(&buffer, &fence);
    bool statusConverted = b2h(status, &hStatus);

    if (status != NO_ERROR) {
        ALOGE("Surface2HGraphicBufferProducer::dequeueBuffer dequeue failed: %d", status);
        _hidl_cb(statusConverted ? hStatus : HStatus::UNKNOWN_ERROR, -1, DequeueBufferOutput{});
        return {};
    }

    int slot = getSlotForBufferLocked(buffer);
    bool needsRefresh = false;
    if (slot == BufferQueue::INVALID_BUFFER_SLOT) {
        slot = getFreeOrEvictableSlotLocked();
        if (slot == BufferQueue::INVALID_BUFFER_SLOT) {
            ALOGE("SBHGBP::dequeueBuffer No available slots for buffer %" PRIu64, buffer->getId());
            status_t ret = mBase->cancelBuffer(buffer, fence);
            if (ret != NO_ERROR) {
                ALOGE("SBHGBP::dequeueBuffer cancelBuffer failed: %d, we're in a potentially bad "
                      "state",
                      ret);
            }

            _hidl_cb(HStatus::NO_MEMORY, -1, DequeueBufferOutput{});
            return {};
        }

        needsRefresh = true;
        mSlotToInfo[slot] = {.isDequeued = true, .buffer = buffer};
    }

    sp<BFence> bFence;
    HFenceWrapper hFenceWrapper;
    bool converted = b2h(status, &hStatus) && b2h(bFence, &hFenceWrapper);

    DequeueBufferOutput hOutput{
            .bufferNeedsReallocation = needsRefresh,
            .releaseAllBuffers = false,
            .fence = hFenceWrapper.getHandle(),
    };
    _hidl_cb(converted ? hStatus : HStatus::UNKNOWN_ERROR, slot, hOutput);
    return {};
}

Return<HStatus> Surface2HGraphicBufferProducer::detachBuffer(int32_t slot) {
    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        ALOGE("Surface2HGraphicBufferProducer::detachBuffer Invalid slot: %d", slot);
        return HStatus::BAD_VALUE;
    }

    std::scoped_lock _l(mMutex);
    sp<GraphicBuffer> buffer = mSlotToInfo[slot].buffer;
    if (!buffer) {
        ALOGE("Surface2HGraphicBufferProducer::detachBuffer Invalid slot: %d", slot);
        return HStatus::BAD_VALUE;
    }

    HStatus hStatus{};
    bool converted = b2h(mBase->detachBuffer(buffer), &hStatus);

    mSlotToInfo[slot].isDequeued = false;
    clearSlotLocked(slot);

    return {converted ? hStatus : HStatus::UNKNOWN_ERROR};
}

Return<void> Surface2HGraphicBufferProducer::detachNextBuffer(detachNextBuffer_cb _hidl_cb) {
    sp<GraphicBuffer> bBuffer;
    sp<BFence> bFence;
    HStatus hStatus{};
    HardwareBuffer hBuffer{};
    HFenceWrapper hFenceWrapper;

    std::scoped_lock _l(mMutex);
    status_t status = mBase->detachNextBuffer(&bBuffer, &bFence);

    // b2h for graphic buffers returns false if the buffer is null, which sends the wrong error
    // code to the client. Check for errors first:
    if (status != NO_ERROR) {
        bool converted = b2h(status, &hStatus);
        _hidl_cb(converted ? hStatus : HStatus::UNKNOWN_ERROR, hBuffer, hFenceWrapper.getHandle());
        return {};
    }

    bool converted = b2h(status, &hStatus) && b2h(bBuffer, &hBuffer) && b2h(bFence, &hFenceWrapper);
    _hidl_cb(converted ? hStatus : HStatus::UNKNOWN_ERROR, hBuffer, hFenceWrapper.getHandle());

    if (status == OK) {
        int slot = getSlotForBufferLocked(bBuffer);
        clearSlotLocked(slot);
    }

    return {};
}

Return<void> Surface2HGraphicBufferProducer::attachBuffer(HardwareBuffer const& hBuffer,
                                                          uint32_t generationNumber,
                                                          attachBuffer_cb _hidl_cb) {
    std::scoped_lock _l(mMutex);
    std::optional<int> freeSlot;
    for (int i = 0; i < BufferQueueDefs::NUM_BUFFER_SLOTS; ++i) {
        if (!mSlotToInfo[i].buffer) {
            freeSlot = i;
            break;
        }
    }

    if (!freeSlot.has_value()) {
        _hidl_cb(HStatus::NO_MEMORY, -1, false);
        return {};
    }

    sp<GraphicBuffer> bBuffer;
    if (!h2b(hBuffer, &bBuffer) || !bBuffer) {
        _hidl_cb(HStatus::UNKNOWN_ERROR, static_cast<int32_t>(SlotIndex::INVALID), false);
        return {};
    }
    bBuffer->setGenerationNumber(generationNumber);

    // HGBPs are required to fail if the buffer's generation number doesn't match that of the HGBP.
    mBase->setAutoGenerationUpdate(false);
    status_t ret = mBase->attachBuffer(bBuffer);
    HStatus hStatus{};
    bool converted = b2h(ret, &hStatus);

    if (ret == OK) {
        LOG_ALWAYS_FATAL_IF(!converted,
                            "SBHGBP::attachBuffer: Failed to convert status_t %d to HStatus, but "
                            "the attach was successful. This should be impossible.",
                            ret);
        mSlotToInfo[freeSlot.value()] = {.isDequeued = true, .buffer = bBuffer};
    }

    _hidl_cb(converted ? hStatus : HStatus::UNKNOWN_ERROR, static_cast<int32_t>(freeSlot.value()),
             false);
    return {};
}

Return<void> Surface2HGraphicBufferProducer::queueBuffer(int32_t slot,
                                                         QueueBufferInput const& hInput,
                                                         queueBuffer_cb _hidl_cb) {
    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        _hidl_cb(HStatus::BAD_VALUE, QueueBufferOutput{});
        return {};
    }

    std::scoped_lock _l(mMutex);

    SurfaceQueueBufferInput sInput{
            .dataSpace = static_cast<android_dataspace>(hInput.dataSpace),
            .crop = {},
            .scalingMode = NATIVE_WINDOW_SCALING_MODE_FREEZE,
            .transform = static_cast<uint32_t>(hInput.transform),
            .stickyTransform = static_cast<uint32_t>(hInput.stickyTransform),
            .timestamp = hInput.timestamp,
            .isAutoTimestamp = hInput.isAutoTimestamp,
            .getFrameTimestamps = mEnableFrameTimestamps,
    };
    // Convert crop.
    if (!h2b(hInput.crop, &sInput.crop)) {
        _hidl_cb(HStatus::UNKNOWN_ERROR, QueueBufferOutput{});
        return {};
    }

    // Convert surfaceDamage.
    if (!h2b(hInput.surfaceDamage, &sInput.surfaceDamage)) {
        _hidl_cb(HStatus::UNKNOWN_ERROR, QueueBufferOutput{});
        return {};
    }

    // Convert fence.
    if (!h2b(hInput.fence, &sInput.fence)) {
        _hidl_cb(HStatus::UNKNOWN_ERROR, QueueBufferOutput{});
        return {};
    }

    sp<GraphicBuffer> buffer = mSlotToInfo[slot].buffer;
    if (!buffer) {
        _hidl_cb(HStatus::BAD_VALUE, QueueBufferOutput{});
        return {};
    }

    SurfaceQueueBufferOutput bOutput{};
    HStatus hStatus{};
    QueueBufferOutput hOutput{};
    status_t result = mBase->queueBuffer(buffer, sInput, &bOutput);
    if (result == NO_ERROR) {
        mSlotToInfo[slot].isDequeued = false;
    }
    bool converted = b2h(result, &hStatus);

    hOutput.width = bOutput.width;
    hOutput.height = bOutput.height;
    hOutput.transformHint = bOutput.transformHint;
    hOutput.numPendingBuffers = bOutput.numPendingBuffers;
    hOutput.nextFrameNumber = bOutput.nextFrameNumber;
    hOutput.bufferReplaced = bOutput.bufferReplaced;

    _hidl_cb(converted ? hStatus : HStatus::UNKNOWN_ERROR, hOutput);
    return {};
}

Return<HStatus> Surface2HGraphicBufferProducer::cancelBuffer(int32_t slot,
                                                             hidl_handle const& fence) {
    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        ALOGE("Surface2HGraphicBufferProducer::cancelBuffer Invalid slot: %d", slot);
        return HStatus::BAD_VALUE;
    }

    std::scoped_lock _l(mMutex);
    sp<GraphicBuffer> bBuffer = mSlotToInfo[slot].buffer;
    if (!bBuffer) {
        ALOGE("Surface2HGraphicBufferProducer::cancelBuffer Invalid slot: %d", slot);
        return HStatus::BAD_VALUE;
    }

    sp<BFence> bFence;
    if (!h2b(fence.getNativeHandle(), &bFence)) {
        return {HStatus::UNKNOWN_ERROR};
    }
    status_t status = mBase->cancelBuffer(bBuffer, bFence);
    if (status == NO_ERROR) {
        mSlotToInfo[slot].isDequeued = false;
    }
    HStatus hStatus{};
    bool converted = b2h(status, &hStatus);
    return {converted ? hStatus : HStatus::UNKNOWN_ERROR};
}

Return<void> Surface2HGraphicBufferProducer::query(int32_t what, query_cb _hidl_cb) {
    int value{};
    int result = mBase->query(static_cast<int>(what), &value);
    _hidl_cb(static_cast<int32_t>(result), static_cast<int32_t>(value));
    return {};
}

Return<void> Surface2HGraphicBufferProducer::connect(const sp<HProducerListener>& hListener,
                                                     HConnectionType hConnectionType,
                                                     bool producerControlledByApp,
                                                     connect_cb _hidl_cb) {
    std::scoped_lock _l(mMutex);
    sp<SurfaceListener> bListener =
            sp<HidlBridgeListener>::make(sp<Surface2HGraphicBufferProducer>::fromExisting(this),
                                         hListener);
    int bConnectionType{};
    if (!bListener || !h2b(hConnectionType, &bConnectionType)) {
        _hidl_cb(HStatus::UNKNOWN_ERROR, QueueBufferOutput{});
        return {};
    }

    mBase->setProducerControlledByApp(producerControlledByApp);

    HStatus hStatus{};
    bool converted = b2h(mBase->connect(bConnectionType, bListener), &hStatus);
    // TODO: what to do about this
#ifdef NO_BINDER
    if (converted && hListener != nullptr) {
        mObituary = new Obituary(this, hListener, hConnectionType);
        hListener->linkToDeath(mObituary, 0);
    }
#endif // NO_BINDER

    resetSlotsLocked();
    int32_t transformHint;
    ANativeWindow_query(mBase.get(), ANATIVEWINDOW_QUERY_TRANSFORM_HINT, &transformHint);
    QueueBufferOutput hOutput{
            .width = static_cast<uint32_t>(ANativeWindow_getWidth(mBase.get())),
            .height = static_cast<uint32_t>(ANativeWindow_getHeight(mBase.get())),
            .transformHint = transformHint,
            .numPendingBuffers = 0,
            .nextFrameNumber = mBase->getNextFrameNumber(),
            .bufferReplaced = false,
    };
    _hidl_cb(converted ? hStatus : HStatus::UNKNOWN_ERROR, hOutput);
    return {};
}

Return<HStatus> Surface2HGraphicBufferProducer::disconnect(HConnectionType hConnectionType) {
    std::scoped_lock _l(mMutex);
    int bConnectionType;
    if (!h2b(hConnectionType, &bConnectionType)) {
        return {HStatus::UNKNOWN_ERROR};
    }
    HStatus hStatus{};
    bool converted = b2h(mBase->disconnect(bConnectionType), &hStatus);
    resetSlotsLocked();

#ifdef NO_BINDER
    if (mObituary != nullptr) {
        mObituary->listener->unlinkToDeath(mObituary);
        mObituary.clear();
    }
#endif // NO_BINDER
    return {converted ? hStatus : HStatus::UNKNOWN_ERROR};
}

Return<HStatus> Surface2HGraphicBufferProducer::allocateBuffers(uint32_t width, uint32_t height,
                                                                uint32_t format, uint64_t usage) {
    std::scoped_lock _l(mMutex);

    status_t ret = mBase->setBuffersDimensions(width, height);
    HStatus hStatus{};
    if (ret != NO_ERROR) {
        ALOGE("Surface2HGraphicBufferProducer::allocateBuffers setBuffersDimensions failed: %d",
              ret);
        return {b2h(ret, &hStatus) ? hStatus : HStatus::UNKNOWN_ERROR};
    }
    ret = mBase->setBuffersFormat(static_cast<PixelFormat>(format));
    if (ret != NO_ERROR) {
        ALOGE("Surface2HGraphicBufferProducer::allocateBuffers setBuffersFormat failed: %d", ret);
        return {b2h(ret, &hStatus) ? hStatus : HStatus::UNKNOWN_ERROR};
    }
    ret = mBase->setUsage(usage);
    if (ret != NO_ERROR) {
        ALOGE("Surface2HGraphicBufferProducer::allocateBuffers setUsage failed: %d", ret);
        return {b2h(ret, &hStatus) ? hStatus : HStatus::UNKNOWN_ERROR};
    }

    mBase->allocateBuffers();

    return {HStatus::OK};
}

Return<HStatus> Surface2HGraphicBufferProducer::allowAllocation(bool allow) {
    HStatus hStatus{};
    bool converted = b2h(mBase->allowAllocation(allow), &hStatus);
    return {converted ? hStatus : HStatus::UNKNOWN_ERROR};
}

Return<HStatus> Surface2HGraphicBufferProducer::setGenerationNumber(uint32_t generationNumber) {
    HStatus hStatus{};
    bool converted = b2h(mBase->setGenerationNumber(generationNumber), &hStatus);
    return {converted ? hStatus : HStatus::UNKNOWN_ERROR};
}

Return<HStatus> Surface2HGraphicBufferProducer::setDequeueTimeout(int64_t timeoutNs) {
    HStatus hStatus{};
    bool converted = b2h(mBase->setDequeueTimeout(static_cast<nsecs_t>(timeoutNs)), &hStatus);
    return {converted ? hStatus : HStatus::UNKNOWN_ERROR};
}

Return<uint64_t> Surface2HGraphicBufferProducer::getUniqueId() {
    uint64_t outId{};
    HStatus hStatus{};
    bool converted = b2h(mBase->getUniqueId(&outId), &hStatus);
    return {converted ? outId : 0};
}

Return<void> Surface2HGraphicBufferProducer::getConsumerName(getConsumerName_cb _hidl_cb) {
    _hidl_cb(hidl_string{mBase->getConsumerName().c_str()});
    return {};
}

void Surface2HGraphicBufferProducer::onBuffersDiscarded(
        const std::vector<sp<GraphicBuffer>>& buffers) {
    std::scoped_lock _l(mMutex);
    for (const auto& buffer : buffers) {
        int slot = getSlotForBufferLocked(buffer);
        if (slot != BufferQueue::INVALID_BUFFER_SLOT) {
            clearSlotLocked(slot);
        }
    }
}

void Surface2HGraphicBufferProducer::onRemoteDied() {
    std::scoped_lock _l(mMutex);
    ALOGE("Surface2HGraphicBufferProducer::onRemoteDie [%s] called. The underlying surface's "
          "remote "
          "process has died.",
          mConsumerName.c_str());
}

int Surface2HGraphicBufferProducer::getSlotForBufferLocked(const sp<GraphicBuffer>& buffer) {
    for (int i = 0; i < BufferQueueDefs::NUM_BUFFER_SLOTS; ++i) {
        if (mSlotToInfo[i].buffer == buffer) {
            return i;
        }
    }
    return BufferQueue::INVALID_BUFFER_SLOT;
}

int Surface2HGraphicBufferProducer::getFreeOrEvictableSlotLocked() {
    // First, if we have any free slots, use one of them.
    for (int i = 0; i < BufferQueueDefs::NUM_BUFFER_SLOTS; ++i) {
        if (!mSlotToInfo[i].buffer) {
            return i;
        }
    }

    // But if we don't, replace a previously dequeued slot.
    for (int i = 0; i < BufferQueueDefs::NUM_BUFFER_SLOTS; ++i) {
        if (mSlotToInfo[i].buffer && !mSlotToInfo[i].isDequeued) {
            return i;
        }
    }
    return BufferQueue::INVALID_BUFFER_SLOT;
}

void Surface2HGraphicBufferProducer::resetSlotsLocked() {
    for (int i = 0; i < BufferQueueDefs::NUM_BUFFER_SLOTS; ++i) {
        mSlotToInfo[i] = {.isDequeued = false, .buffer = nullptr};
    }
}

void Surface2HGraphicBufferProducer::clearSlotLocked(int slot) {
    if (mSlotToInfo[slot].buffer == nullptr || mSlotToInfo[slot].isDequeued) {
        return;
    }

    mSlotToInfo[slot] = {.isDequeued = false, .buffer = nullptr};
}

} // namespace android::hardware::graphics::bufferqueue::V2_0::utils
