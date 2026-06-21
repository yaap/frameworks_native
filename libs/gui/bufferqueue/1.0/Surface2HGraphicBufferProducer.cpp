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
#define LOG_TAG "Surface2HGraphicBufferProducer@1.0"

#include <android-base/logging.h>
#include <utils/Log.h>

#include <android/hardware/graphics/common/1.0/types.h>
#include <gui/BufferQueue.h>
#include <gui/Surface.h>
#include <gui/bufferqueue/1.0/Conversion.h>
#include <gui/bufferqueue/1.0/H2BProducerListener.h>
#include <ui/BufferQueueDefs.h>
#include <ui/GraphicBuffer.h>
#include <ui/Rect.h>
#include <ui/Region.h>
#include <vndk/hardware_buffer.h>

#include <gui/bufferqueue/1.0/Surface2HGraphicBufferProducer.h>

namespace android {
namespace hardware {
namespace graphics {
namespace bufferqueue {
namespace V1_0 {
namespace utils {

namespace /* unnamed */ {

using BQueueBufferInput = ::android::IGraphicBufferProducer::QueueBufferInput;
using HQueueBufferInput =
        ::android::hardware::graphics::bufferqueue::V1_0::IGraphicBufferProducer::QueueBufferInput;
using BQueueBufferOutput = ::android::IGraphicBufferProducer::QueueBufferOutput;
using HQueueBufferOutput =
        ::android::hardware::graphics::bufferqueue::V1_0::IGraphicBufferProducer::QueueBufferOutput;

} // unnamed namespace

class Surface2HGraphicBufferProducer::HidlBridgeListener final : public SurfaceListener {
public:
    HidlBridgeListener(const sp<Surface2HGraphicBufferProducer>& producer,
                       const sp<HProducerListener>& listener)
          : mProducer(producer), mListener(listener) {}

    virtual bool needsReleaseNotify() override { return true; }
    virtual void onBufferReleased() override { mListener->onBufferReleased(); }

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
    int32_t apiType;

    Obituary(const wp<Surface2HGraphicBufferProducer>& p, const sp<HProducerListener>& l, int32_t t)
          : producer(p), listener(l), apiType(t) {}

    void serviceDied(uint64_t /* cookie */,
                     const wp<::android::hidl::base::V1_0::IBase>& /* who */) override {
        sp<Surface2HGraphicBufferProducer> dr = producer.promote();
        if (dr != nullptr) {
            (void)dr->disconnect(apiType, HGraphicBufferProducer::DisconnectMode::API);
        }
    }
};

Surface2HGraphicBufferProducer::Surface2HGraphicBufferProducer(
        const sp<::android::Surface>& surface)
      : mBase(surface) {
    resetSlotsLocked();
}

::android::hardware::Return<void> Surface2HGraphicBufferProducer::requestBuffer(
        int32_t slot, requestBuffer_cb _hidl_cb) {
    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        ALOGE("Surface2HGraphicBufferProducer::requestBuffer Invalid slot: %d", slot);
        _hidl_cb(static_cast<int32_t>(BAD_VALUE), AnwBuffer{});
        return {};
    }

    std::scoped_lock _l(mMutex);
    sp<GraphicBuffer> buffer = mSlotToInfo[slot].buffer;
    if (buffer == nullptr) {
        ALOGE("Surface2HGraphicBufferProducer::requestBuffer Invalid slot: %d", slot);
        _hidl_cb(static_cast<int32_t>(BAD_VALUE), AnwBuffer{});
        return {};
    }

    AnwBuffer hBuffer{};
    ::android::conversion::wrapAs(&hBuffer, *buffer);
    _hidl_cb(static_cast<int32_t>(OK), hBuffer);
    return {};
}

::android::hardware::Return<int32_t> Surface2HGraphicBufferProducer::setMaxDequeuedBufferCount(
        int32_t maxDequeuedBuffers) {
    // Since we only provide NUM_BUFFER_SLOTS buffers, we can't allow more than that.
    if (maxDequeuedBuffers < 0 || maxDequeuedBuffers > BufferQueueDefs::NUM_BUFFER_SLOTS) {
        ALOGE("Surface2HGraphicBufferProducer::setMaxDequeuedBufferCount Invalid "
              "maxDequeuedBuffers: %d",
              maxDequeuedBuffers);
        return {static_cast<int32_t>(BAD_VALUE)};
    }

    return {mBase->setMaxDequeuedBufferCount(static_cast<int>(maxDequeuedBuffers))};
}

::android::hardware::Return<int32_t> Surface2HGraphicBufferProducer::setAsyncMode(bool async) {
    return {mBase->setAsyncMode(async)};
}

::android::hardware::Return<void> Surface2HGraphicBufferProducer::dequeueBuffer(
        uint32_t width, uint32_t height,
        ::android::hardware::graphics::common::V1_0::PixelFormat format, uint32_t usage,
        bool getFrameTimestamps, dequeueBuffer_cb _hidl_cb) {
    (void)getFrameTimestamps;
    std::scoped_lock _l(mMutex);

    status_t status = NO_ERROR;

    status = mBase->setBuffersDimensions(width, height);
    if (status != NO_ERROR) {
        ALOGE("Surface2HGraphicBufferProducer::dequeueBuffer setBuffersDimensions failed: %d",
              status);
        _hidl_cb(static_cast<int32_t>(status), -1, hidl_handle{}, {});
        return {};
    }
    status = mBase->setBuffersFormat(static_cast<android::PixelFormat>(format));
    if (status != NO_ERROR) {
        ALOGE("Surface2HGraphicBufferProducer::dequeueBuffer setBuffersFormat failed: %d", status);
        _hidl_cb(static_cast<int32_t>(status), -1, hidl_handle{}, {});
        return {};
    }
    status = mBase->setUsage(usage);
    if (status != NO_ERROR) {
        ALOGE("Surface2HGraphicBufferProducer::dequeueBuffer setUsage failed: %d", status);
        _hidl_cb(static_cast<int32_t>(status), -1, hidl_handle{}, {});
        return {};
    }

    sp<GraphicBuffer> buffer;
    sp<Fence> fence;
    status = mBase->dequeueBuffer(&buffer, &fence);

    if (status < NO_ERROR) {
        if (status != WOULD_BLOCK) {
            ALOGE("Surface2HGraphicBufferProducer::dequeueBuffer dequeue failed: %d", status);
        }
        _hidl_cb(static_cast<int32_t>(status), -1, hidl_handle{}, {});
        return {};
    }

    int slot = getSlotForBufferLocked(buffer);
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

            _hidl_cb(static_cast<int32_t>(NO_MEMORY), -1, hidl_handle{}, {});
            return {};
        }

        status |= android::IGraphicBufferProducer::BUFFER_NEEDS_REALLOCATION;
        mSlotToInfo[slot] = {.needsRefresh = true, .isDequeued = true, .buffer = buffer};
    } else {
        if (mSlotToInfo[slot].needsRefresh) {
            status |= android::IGraphicBufferProducer::BUFFER_NEEDS_REALLOCATION;
        }
        mSlotToInfo[slot].isDequeued = true;
        mSlotToInfo[slot].needsRefresh = false;
    }

    hidl_handle hFence;
    native_handle_t* nh = nullptr;
    if (fence != nullptr) {
        ::android::conversion::wrapAs(&hFence, &nh, *fence);
    }

    // TODO: support frame timestamps if needed
    _hidl_cb(static_cast<int32_t>(status), slot, hFence, {});

    if (nh != nullptr) {
        native_handle_delete(nh);
    }

    return {};
}

::android::hardware::Return<int32_t> Surface2HGraphicBufferProducer::detachBuffer(int32_t slot) {
    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        ALOGE("Surface2HGraphicBufferProducer::detachBuffer Invalid slot: %d", slot);
        return {static_cast<int32_t>(BAD_VALUE)};
    }

    std::scoped_lock _l(mMutex);
    sp<GraphicBuffer> buffer = mSlotToInfo[slot].buffer;
    if (!buffer) {
        ALOGE("Surface2HGraphicBufferProducer::detachBuffer Invalid slot: %d", slot);
        return {static_cast<int32_t>(BAD_VALUE)};
    }

    status_t status = mBase->detachBuffer(buffer);

    mSlotToInfo[slot].isDequeued = false;
    clearSlotLocked(slot);

    return {static_cast<int32_t>(status)};
}

::android::hardware::Return<void> Surface2HGraphicBufferProducer::detachNextBuffer(
        detachNextBuffer_cb _hidl_cb) {
    sp<GraphicBuffer> bBuffer;
    sp<Fence> bFence;

    std::scoped_lock _l(mMutex);
    status_t status = mBase->detachNextBuffer(&bBuffer, &bFence);

    if (status != NO_ERROR) {
        _hidl_cb(static_cast<int32_t>(status), AnwBuffer{}, hidl_handle{});
        return {};
    }

    AnwBuffer hBuffer{};
    ::android::conversion::wrapAs(&hBuffer, *bBuffer);

    hidl_handle hFence;
    native_handle_t* nh = nullptr;
    if (bFence != nullptr) {
        ::android::conversion::wrapAs(&hFence, &nh, *bFence);
    }

    _hidl_cb(static_cast<int32_t>(status), hBuffer, hFence);

    if (status == OK) {
        int slot = getSlotForBufferLocked(bBuffer);
        if (slot != BufferQueue::INVALID_BUFFER_SLOT) {
            clearSlotLocked(slot);
        }
    }

    if (nh != nullptr) {
        native_handle_delete(nh);
    }

    return {};
}

::android::hardware::Return<void> Surface2HGraphicBufferProducer::attachBuffer(
        const ::android::hardware::media::V1_0::AnwBuffer& buffer, attachBuffer_cb _hidl_cb) {
    std::scoped_lock _l(mMutex);
    int slot = getFreeOrEvictableSlotLocked();
    if (slot == BufferQueue::INVALID_BUFFER_SLOT) {
        _hidl_cb(static_cast<int32_t>(NO_MEMORY), -1);
        return {};
    }

    sp<GraphicBuffer> bBuffer = sp<GraphicBuffer>::make();
    if (!::android::conversion::convertTo(bBuffer.get(), buffer) || !bBuffer) {
        _hidl_cb(static_cast<int32_t>(UNKNOWN_ERROR), -1);
        return {};
    }

    // HGBPs are required to fail if the buffer's generation number doesn't match that of the HGBP.
    mBase->setAutoGenerationUpdate(false);
    status_t ret = mBase->attachBuffer(bBuffer);

    if (ret == OK) {
        mSlotToInfo[slot] = {.needsRefresh = false, .isDequeued = true, .buffer = bBuffer};
    }

    _hidl_cb(static_cast<int32_t>(ret), slot);
    return {};
}

::android::hardware::Return<void> Surface2HGraphicBufferProducer::queueBuffer(
        int32_t slot,
        const ::android::hardware::graphics::bufferqueue::V1_0::IGraphicBufferProducer::
                QueueBufferInput& input,
        queueBuffer_cb _hidl_cb) {
    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        _hidl_cb(static_cast<int32_t>(BAD_VALUE), {});
        return {};
    }

    SurfaceQueueBufferInput sInput{
            .dataSpace = static_cast<android_dataspace>(input.dataSpace),
            .crop = Rect(input.crop.left, input.crop.top, input.crop.right, input.crop.bottom),
            .scalingMode = NATIVE_WINDOW_SCALING_MODE_FREEZE,
            .transform = static_cast<uint32_t>(input.transform),
            .stickyTransform = static_cast<uint32_t>(input.stickyTransform),
            .timestamp = input.timestamp,
            .isAutoTimestamp = input.isAutoTimestamp,
            .getFrameTimestamps = input.getFrameTimestamps,
    };

    sp<Fence> bFence = sp<Fence>::make();
    if (!::android::conversion::convertTo(bFence.get(), input.fence)) {
        _hidl_cb(static_cast<int32_t>(UNKNOWN_ERROR), {});
        return {};
    }
    sInput.fence = bFence;

    if (input.surfaceDamage.size() > 0) {
        if (!::android::conversion::convertTo(&sInput.surfaceDamage, input.surfaceDamage)) {
            _hidl_cb(static_cast<int32_t>(UNKNOWN_ERROR), {});
            return {};
        }
    }

    std::scoped_lock _l(mMutex);
    sp<GraphicBuffer> buffer = mSlotToInfo[slot].buffer;
    if (!buffer) {
        _hidl_cb(static_cast<int32_t>(BAD_VALUE), {});
        return {};
    }

    SurfaceQueueBufferOutput bOutput{};
    status_t result = mBase->queueBuffer(buffer, sInput, &bOutput);
    if (result == NO_ERROR) {
        mSlotToInfo[slot].isDequeued = false;
    }

    HQueueBufferOutput hOutput{};
    BQueueBufferOutput bqOutput{};
    bqOutput.width = bOutput.width;
    bqOutput.height = bOutput.height;
    bqOutput.transformHint = bOutput.transformHint;
    bqOutput.numPendingBuffers = bOutput.numPendingBuffers;
    bqOutput.nextFrameNumber = bOutput.nextFrameNumber;
    bqOutput.bufferReplaced = bOutput.bufferReplaced;

    std::vector<std::vector<native_handle_t*>> nh;
    ::android::conversion::wrapAs(&hOutput, &nh, bqOutput);

    _hidl_cb(static_cast<int32_t>(result), hOutput);

    for (auto& v : nh) {
        for (auto h : v) {
            if (h != nullptr) {
                native_handle_delete(h);
            }
        }
    }

    return {};
}

::android::hardware::Return<int32_t> Surface2HGraphicBufferProducer::cancelBuffer(
        int32_t slot, const ::android::hardware::hidl_handle& fence) {
    if (slot < 0 || slot >= BufferQueueDefs::NUM_BUFFER_SLOTS) {
        ALOGE("Surface2HGraphicBufferProducer::cancelBuffer Invalid slot: %d", slot);
        return {static_cast<int32_t>(BAD_VALUE)};
    }

    std::scoped_lock _l(mMutex);
    sp<GraphicBuffer> bBuffer = mSlotToInfo[slot].buffer;
    if (!bBuffer) {
        ALOGE("Surface2HGraphicBufferProducer::cancelBuffer Invalid slot: %d", slot);
        return {static_cast<int32_t>(BAD_VALUE)};
    }

    sp<Fence> bFence = sp<Fence>::make();
    if (!::android::conversion::convertTo(bFence.get(), fence)) {
        return {static_cast<int32_t>(UNKNOWN_ERROR)};
    }
    status_t status = mBase->cancelBuffer(bBuffer, bFence);
    if (status == NO_ERROR) {
        mSlotToInfo[slot].isDequeued = false;
    }
    return {static_cast<int32_t>(status)};
}

::android::hardware::Return<void> Surface2HGraphicBufferProducer::query(int32_t what,
                                                                        query_cb _hidl_cb) {
    int value{};
    int result = mBase->query(static_cast<int>(what), &value);
    _hidl_cb(static_cast<int32_t>(result), static_cast<int32_t>(value));
    return {};
}

::android::hardware::Return<void> Surface2HGraphicBufferProducer::connect(
        const ::android::sp<::android::hardware::graphics::bufferqueue::V1_0::IProducerListener>&
                listener,
        int32_t api, bool producerControlledByApp, connect_cb _hidl_cb) {
    std::scoped_lock _l(mMutex);
    sp<SurfaceListener> bListener =
            sp<HidlBridgeListener>::make(sp<Surface2HGraphicBufferProducer>::fromExisting(this),
                                         listener);
    if (!bListener) {
        _hidl_cb(static_cast<int32_t>(UNKNOWN_ERROR), {});
        return {};
    }

    mBase->setProducerControlledByApp(producerControlledByApp);

    status_t result = mBase->connect(api, bListener, true);
    if (result == OK) {
        mObituary = sp<Obituary>::make(sp<Surface2HGraphicBufferProducer>::fromExisting(this),
                                       listener, api);
        if (listener != nullptr) {
            listener->linkToDeath(mObituary, 0);
        }
    }

    resetSlotsLocked();

    BQueueBufferOutput bOutput{};
    mBase->query(NATIVE_WINDOW_WIDTH, reinterpret_cast<int*>(&bOutput.width));
    mBase->query(NATIVE_WINDOW_HEIGHT, reinterpret_cast<int*>(&bOutput.height));
    int transformHint = 0;
    mBase->query(NATIVE_WINDOW_TRANSFORM_HINT, &transformHint);
    bOutput.transformHint = static_cast<uint32_t>(transformHint);
    bOutput.nextFrameNumber = mBase->getNextFrameNumber();

    HQueueBufferOutput hOutput{};
    std::vector<std::vector<native_handle_t*>> nh;
    ::android::conversion::wrapAs(&hOutput, &nh, bOutput);

    _hidl_cb(static_cast<int32_t>(result), hOutput);

    for (auto& v : nh) {
        for (auto h : v) {
            if (h != nullptr) {
                native_handle_delete(h);
            }
        }
    }

    return {};
}

::android::hardware::Return<int32_t> Surface2HGraphicBufferProducer::disconnect(
        int32_t api,
        ::android::hardware::graphics::bufferqueue::V1_0::IGraphicBufferProducer::DisconnectMode
                mode) {
    std::scoped_lock _l(mMutex);
    status_t result = mBase->disconnect(api, ::android::conversion::toGuiDisconnectMode(mode));
    resetSlotsLocked();

    if (mObituary != nullptr) {
        if (mObituary->listener != nullptr) {
            mObituary->listener->unlinkToDeath(mObituary);
        }
        mObituary.clear();
    }
    return {static_cast<int32_t>(result)};
}

::android::hardware::Return<int32_t> Surface2HGraphicBufferProducer::setSidebandStream(
        const ::android::hardware::hidl_handle& stream) {
    mBase->setSidebandStream(
            NativeHandle::create(stream.getNativeHandle()
                                         ? native_handle_clone(stream.getNativeHandle())
                                         : nullptr,
                                 true));
    return {static_cast<int32_t>(OK)};
}

::android::hardware::Return<void> Surface2HGraphicBufferProducer::allocateBuffers(
        uint32_t width, uint32_t height,
        ::android::hardware::graphics::common::V1_0::PixelFormat format, uint32_t usage) {
    std::scoped_lock _l(mMutex);

    mBase->setBuffersDimensions(width, height);
    mBase->setBuffersFormat(static_cast<android::PixelFormat>(format));
    mBase->setUsage(usage);

    mBase->allocateBuffers();

    return {};
}

::android::hardware::Return<int32_t> Surface2HGraphicBufferProducer::allowAllocation(bool allow) {
    return {mBase->allowAllocation(allow)};
}

::android::hardware::Return<int32_t> Surface2HGraphicBufferProducer::setGenerationNumber(
        uint32_t generationNumber) {
    return {mBase->setGenerationNumber(generationNumber)};
}

::android::hardware::Return<void> Surface2HGraphicBufferProducer::getConsumerName(
        getConsumerName_cb _hidl_cb) {
    _hidl_cb(hidl_string{mBase->getConsumerName().c_str()});
    return {};
}

::android::hardware::Return<int32_t> Surface2HGraphicBufferProducer::setSharedBufferMode(
        bool sharedBufferMode) {
    return {mBase->setSharedBufferMode(sharedBufferMode)};
}

::android::hardware::Return<int32_t> Surface2HGraphicBufferProducer::setAutoRefresh(
        bool autoRefresh) {
    return {mBase->setAutoRefresh(autoRefresh)};
}

::android::hardware::Return<int32_t> Surface2HGraphicBufferProducer::setDequeueTimeout(
        int64_t timeoutNs) {
    return {mBase->setDequeueTimeout(static_cast<nsecs_t>(timeoutNs))};
}

::android::hardware::Return<void> Surface2HGraphicBufferProducer::getLastQueuedBuffer(
        getLastQueuedBuffer_cb _hidl_cb) {
    sp<GraphicBuffer> buffer;
    sp<Fence> fence;
    float matrix[16];
    status_t result = mBase->getLastQueuedBuffer(&buffer, &fence, matrix);

    if (result != NO_ERROR) {
        _hidl_cb(static_cast<int32_t>(result), AnwBuffer{}, hidl_handle{}, hidl_array<float, 16>{});
        return {};
    }

    AnwBuffer hBuffer{};
    if (buffer != nullptr) {
        ::android::conversion::wrapAs(&hBuffer, *buffer);
    }

    hidl_handle hFence;
    native_handle_t* nh = nullptr;
    if (fence != nullptr) {
        ::android::conversion::wrapAs(&hFence, &nh, *fence);
    }

    hidl_array<float, 16> hMatrix;
    std::copy(matrix, matrix + 16, hMatrix.data());

    _hidl_cb(static_cast<int32_t>(result), hBuffer, hFence, hMatrix);

    if (nh != nullptr) {
        native_handle_delete(nh);
    }

    return {};
}

::android::hardware::Return<void> Surface2HGraphicBufferProducer::getFrameTimestamps(
        getFrameTimestamps_cb _hidl_cb) {
    ::android::FrameEventHistoryDelta bDelta;
    mBase->getFrameEventHistoryDelta(&bDelta);

    HGraphicBufferProducer::FrameEventHistoryDelta hDelta;
    std::vector<std::vector<native_handle_t*>> nh;
    ::android::conversion::wrapAs(&hDelta, &nh, bDelta);

    _hidl_cb(hDelta);

    for (auto& v : nh) {
        for (auto h : v) {
            if (h != nullptr) {
                native_handle_delete(h);
            }
        }
    }

    return {};
}

::android::hardware::Return<void> Surface2HGraphicBufferProducer::getUniqueId(
        getUniqueId_cb _hidl_cb) {
    uint64_t outId = 0;
    status_t result = mBase->getUniqueId(&outId);
    _hidl_cb(static_cast<int32_t>(result), outId);
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
        mSlotToInfo[i] = {.needsRefresh = false, .isDequeued = false, .buffer = nullptr};
    }
}

void Surface2HGraphicBufferProducer::clearSlotLocked(int slot) {
    if (mSlotToInfo[slot].buffer == nullptr || mSlotToInfo[slot].isDequeued) {
        return;
    }

    mSlotToInfo[slot] = {.needsRefresh = false, .isDequeued = false, .buffer = nullptr};
}

} // namespace utils
} // namespace V1_0
} // namespace bufferqueue
} // namespace graphics
} // namespace hardware
} // namespace android