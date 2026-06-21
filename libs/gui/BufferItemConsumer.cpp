/*
 * Copyright (C) 2012 The Android Open Source Project
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
#define LOG_TAG "BufferItemConsumer"
// #define ATRACE_TAG ATRACE_TAG_GRAPHICS
#include <utils/Errors.h>
#include <utils/Log.h>

#include <inttypes.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <com_android_graphics_libgui_flags.h>
#include <ftl/small_vector.h>
#include <gui/BufferItem.h>
#include <gui/BufferItemConsumer.h>
#include <gui/Surface.h>
#include <ui/BufferQueueDefs.h>
#include <ui/GraphicBuffer.h>

#include <optional>

#define BI_LOGV(x, ...) ALOGV("[%s] " x, mName.c_str(), ##__VA_ARGS__)
// #define BI_LOGD(x, ...) ALOGD("[%s] " x, mName.c_str(), ##__VA_ARGS__)
// #define BI_LOGI(x, ...) ALOGI("[%s] " x, mName.c_str(), ##__VA_ARGS__)
// #define BI_LOGW(x, ...) ALOGW("[%s] " x, mName.c_str(), ##__VA_ARGS__)
#define BI_LOGE(x, ...) ALOGE("[%s] " x, mName.c_str(), ##__VA_ARGS__)

namespace android {

/**
 * RAII class to handle unlocking a mutex and notifying listeners of freed buffers.
 *
 * Ensures that the notification callbacks are called WITHOUT the locks, to help avoid deadlocks.
 */
class DeferredFreedBufferNotifierAutoLock {
public:
    explicit DeferredFreedBufferNotifierAutoLock(
            Mutex& mutex, const wp<BufferItemConsumer::BufferFreedListener>& listener)
          : mMutex(mutex), mOnBufferFreed(listener) {
        mMutex.lock();
    }
    explicit DeferredFreedBufferNotifierAutoLock(Mutex& mutex,
                                                 BufferItemConsumer::BufferFreedCallback callback)
          : mMutex(mutex), mOnBufferFreed(callback) {
        mMutex.lock();
    }

    ~DeferredFreedBufferNotifierAutoLock() {
        mMutex.unlock();

        if (std::holds_alternative<wp<BufferItemConsumer::BufferFreedListener>>(mOnBufferFreed)) {
            sp<BufferItemConsumer::BufferFreedListener> bufferFreedListener =
                    std::get<0>(mOnBufferFreed).promote();
            if (!bufferFreedListener) {
                return;
            }

            for (const auto& buffer : mBuffers) {
                bufferFreedListener->onBufferFreed(buffer);
            }
        } else {
            for (const auto& buffer : mBuffers) {
                std::get<1>(mOnBufferFreed)(buffer);
            }
        }
    }

    // Returns a callback that accumulates buffers into this accumulator.
    BufferItemConsumer::BufferFreedCallback getConsumerCallback() {
        return [&](const sp<GraphicBuffer>& buffer) { mBuffers.push_back(buffer); };
    }

private:
    Mutex& mMutex;
    std::variant<wp<BufferItemConsumer::BufferFreedListener>,
                 BufferItemConsumer::BufferFreedCallback>
            mOnBufferFreed;
    ftl::SmallVector<sp<GraphicBuffer>, 16> mBuffers;
};

std::tuple<sp<BufferItemConsumer>, sp<Surface>> BufferItemConsumer::create(
        uint64_t consumerUsage, int bufferCount, bool controlledByApp,
        bool isConsumerSurfaceFlinger) {
    sp<BufferItemConsumer> bufferItemConsumer =
            sp<BufferItemConsumer>::make(consumerUsage, bufferCount, controlledByApp,
                                         isConsumerSurfaceFlinger);
    return {bufferItemConsumer, bufferItemConsumer->getSurface()};
}

sp<BufferItemConsumer> BufferItemConsumer::create(const sp<IGraphicBufferConsumer>& consumer,
                                                  uint64_t consumerUsage, int bufferCount,
                                                  bool controlledByApp) {
    return sp<BufferItemConsumer>::make(consumer, consumerUsage, bufferCount, controlledByApp);
}

BufferItemConsumer::BufferItemConsumer(uint64_t consumerUsage, int bufferCount,
                                       bool controlledByApp, bool isConsumerSurfaceFlinger)
      : ConsumerBase(controlledByApp, isConsumerSurfaceFlinger),
        mConsumerUsage(consumerUsage),
        mBufferCount(bufferCount) {}

BufferItemConsumer::BufferItemConsumer(const sp<IGraphicBufferProducer>& producer,
                                       const sp<IGraphicBufferConsumer>& consumer,
                                       uint64_t consumerUsage, int bufferCount,
                                       bool controlledByApp)
      : ConsumerBase(producer, consumer, controlledByApp),
        mConsumerUsage(consumerUsage),
        mBufferCount(bufferCount) {}

BufferItemConsumer::BufferItemConsumer(const sp<IGraphicBufferConsumer>& consumer,
                                       uint64_t consumerUsage, int bufferCount,
                                       bool controlledByApp)
      : ConsumerBase(consumer, controlledByApp),
        mConsumerUsage(consumerUsage),
        mBufferCount(bufferCount) {}

void BufferItemConsumer::onFirstRef() {
    ConsumerBase::onFirstRef();
    initializeConsumer();
}

void BufferItemConsumer::initializeConsumer() {
    status_t err = mConsumer->setConsumerUsageBits(mConsumerUsage);
    LOG_ALWAYS_FATAL_IF(err != OK, "Failed to set consumer usage bits to %#" PRIx64,
                        mConsumerUsage);
    if (mBufferCount != DEFAULT_MAX_BUFFERS) {
        err = mConsumer->setMaxAcquiredBufferCount(mBufferCount);
        LOG_ALWAYS_FATAL_IF(err != OK, "Failed to set max acquired buffer count to %d",
                            mBufferCount);
    }
}

BufferItemConsumer::~BufferItemConsumer() {}

void BufferItemConsumer::abandon() {
    DeferredFreedBufferNotifierAutoLock acc(mMutex, getBufferFreedListener());
    ConsumerBase::abandonLocked(acc.getConsumerCallback());
}

void BufferItemConsumer::abandon(BufferFreedCallback onBufferFreed) {
    DeferredFreedBufferNotifierAutoLock acc(mMutex, onBufferFreed);
    ConsumerBase::abandonLocked(acc.getConsumerCallback());
}

status_t BufferItemConsumer::setMaxAcquiredBufferCount(int maxAcquiredBuffers) {
    DeferredFreedBufferNotifierAutoLock acc(mMutex, getBufferFreedListener());
    return ConsumerBase::setMaxAcquiredBufferCountLocked(maxAcquiredBuffers,
                                                         acc.getConsumerCallback());
}

status_t BufferItemConsumer::setMaxAcquiredBufferCount(int maxAcquiredBuffers,
                                                       BufferFreedCallback onBufferFreed) {
    DeferredFreedBufferNotifierAutoLock acc(mMutex, onBufferFreed);
    return ConsumerBase::setMaxAcquiredBufferCountLocked(maxAcquiredBuffers,
                                                         acc.getConsumerCallback());
}

status_t BufferItemConsumer::discardFreeBuffers() {
    DeferredFreedBufferNotifierAutoLock acc(mMutex, getBufferFreedListener());
    return ConsumerBase::discardFreeBuffersLocked(acc.getConsumerCallback());
}

status_t BufferItemConsumer::discardFreeBuffers(BufferFreedCallback onBufferFreed) {
    DeferredFreedBufferNotifierAutoLock acc(mMutex, onBufferFreed);
    return ConsumerBase::discardFreeBuffersLocked(acc.getConsumerCallback());
}

status_t BufferItemConsumer::detachBuffer(int slot) {
    DeferredFreedBufferNotifierAutoLock acc(mMutex, getBufferFreedListener());
    return ConsumerBase::detachBufferLocked(slot, acc.getConsumerCallback());
}

status_t BufferItemConsumer::detachBuffer(const sp<GraphicBuffer>& buffer) {
    DeferredFreedBufferNotifierAutoLock acc(mMutex, getBufferFreedListener());
    return ConsumerBase::detachBufferLocked(buffer, acc.getConsumerCallback());
}

status_t BufferItemConsumer::detachBuffer(const sp<GraphicBuffer>& buffer,
                                          BufferFreedCallback onBufferFreed) {
    DeferredFreedBufferNotifierAutoLock acc(mMutex, onBufferFreed);
    return ConsumerBase::detachBufferLocked(buffer, acc.getConsumerCallback());
}

void BufferItemConsumer::onBuffersReleased() {
    DeferredFreedBufferNotifierAutoLock acc(mMutex, getBufferFreedListener());
    ConsumerBase::onBuffersReleasedLocked(acc.getConsumerCallback());
}

void BufferItemConsumer::setBufferFreedListener(
        const wp<BufferFreedListener>& listener) {
    Mutex::Autolock _l(mMutex);
    mBufferFreedListener = listener;
}

status_t BufferItemConsumer::acquireBuffer(BufferItem* item, nsecs_t presentWhen, bool waitForFence,
                                           std::optional<BufferFreedCallback> onBufferFreed) {
    status_t err;

    if (!item) return BAD_VALUE;

    auto acc = onBufferFreed
            ? DeferredFreedBufferNotifierAutoLock(mMutex, *onBufferFreed)
            : DeferredFreedBufferNotifierAutoLock(mMutex, getBufferFreedListener());
    err = acquireBufferLocked(item, presentWhen, /*maxFrameNumber*/ 0, acc.getConsumerCallback());
    if (err != OK) {
        if (err != NO_BUFFER_AVAILABLE) {
            BI_LOGE("Error acquiring buffer: %s (%d)", strerror(err), err);
        }
        return err;
    }

    if (waitForFence) {
        err = item->mFence->waitForever("BufferItemConsumer::acquireBuffer");
        if (err != OK) {
            BI_LOGE("Failed to wait for fence of acquired buffer: %s (%d)", strerror(-err), err);
            return err;
        }
    }

    item->mGraphicBuffer = mSlots[item->mSlot].mGraphicBuffer;

    return OK;
}

status_t BufferItemConsumer::acquireBuffer(BufferItem* item, nsecs_t presentWhen,
                                           uint64_t maxFrameNumber,
                                           std::optional<BufferFreedCallback> onBufferFreed) {
    if (!item) return BAD_VALUE;

    status_t err;
    auto acc = onBufferFreed
            ? DeferredFreedBufferNotifierAutoLock(mMutex, *onBufferFreed)
            : DeferredFreedBufferNotifierAutoLock(mMutex, getBufferFreedListener());
    err = acquireBufferLocked(item, presentWhen, maxFrameNumber, acc.getConsumerCallback());
    if (err != OK) {
        if (err != NO_BUFFER_AVAILABLE) {
            BI_LOGE("Error acquiring buffer: %s (%d)", strerror(err), err);
        }
        return err;
    }

    item->mGraphicBuffer = mSlots[item->mSlot].mGraphicBuffer;

    return OK;
}

status_t BufferItemConsumer::attachBuffer(const sp<GraphicBuffer>& buffer) {
    if (!buffer) {
        BI_LOGE("BufferItemConsumer::attachBuffer no input buffer specified.");
        return BAD_VALUE;
    }

    Mutex::Autolock _l(mMutex);

    int slot = INVALID_BUFFER_SLOT;
    status_t status = mConsumer->attachBuffer(&slot, buffer);
    if (status != OK) {
        BI_LOGE("BufferItemConsumer::attachBuffer unable to attach buffer %d", status);
        return status;
    }

    mSlots[slot] = {
            .mGraphicBuffer = buffer,
            .mFence = nullptr,
            .mFrameNumber = 0,
    };

    return OK;
}

status_t BufferItemConsumer::releaseBuffer(const BufferItem& item, const sp<Fence>& releaseFence,
                                           std::optional<BufferFreedCallback> onBufferFreed) {
    auto acc = onBufferFreed
            ? DeferredFreedBufferNotifierAutoLock(mMutex, *onBufferFreed)
            : DeferredFreedBufferNotifierAutoLock(mMutex, getBufferFreedListener());
    return releaseBufferSlotLocked(item.mSlot, item.mGraphicBuffer, releaseFence,
                                   acc.getConsumerCallback());
}

status_t BufferItemConsumer::releaseBuffer(const sp<GraphicBuffer>& buffer,
                                           const sp<Fence>& releaseFence,
                                           std::optional<BufferFreedCallback> onBufferFreed) {
    auto acc = onBufferFreed
            ? DeferredFreedBufferNotifierAutoLock(mMutex, *onBufferFreed)
            : DeferredFreedBufferNotifierAutoLock(mMutex, getBufferFreedListener());
    if (buffer == nullptr) {
        return BAD_VALUE;
    }

    int slotIndex = getSlotForBufferLocked(buffer);
    if (slotIndex == INVALID_BUFFER_SLOT) {
        return BAD_VALUE;
    }

    return releaseBufferSlotLocked(slotIndex, buffer, releaseFence, acc.getConsumerCallback());
}

status_t BufferItemConsumer::releaseBufferSlotLocked(int slotIndex, const sp<GraphicBuffer>& buffer,
                                                     const sp<Fence>& releaseFence,
                                                     BufferFreedCallback onBufferFreed) {
    status_t err;

    err = addReleaseFenceLocked(slotIndex, buffer, releaseFence);
    if (err != OK) {
        BI_LOGE("Failed to addReleaseFenceLocked");
    }

#if COM_ANDROID_GRAPHICS_LIBGUI_FLAGS(BQ_GL_FENCE_CLEANUP)
    err = releaseBufferLocked(slotIndex, buffer, onBufferFreed);
#else
    err = releaseBufferLocked(slotIndex, buffer, EGL_NO_DISPLAY, EGL_NO_SYNC_KHR, onBufferFreed);
#endif

    if (err != OK && err != IGraphicBufferConsumer::STALE_BUFFER_SLOT) {
        BI_LOGE("Failed to release buffer: %s (%d)", strerror(-err), err);
    }
    return err;
}

wp<BufferItemConsumer::BufferFreedListener> BufferItemConsumer::getBufferFreedListener() {
    Mutex::Autolock _l(mMutex);
    return mBufferFreedListener;
}

} // namespace android
