/*
 * Copyright 2019 The Android Open Source Project
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

#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <gui/BufferQueue.h>
#include <ui/GraphicBuffer.h>

#include <surfacetexture/ImageConsumer.h>
#include <surfacetexture/SurfaceTexture.h>

// Macro for including the SurfaceTexture name in log messages
#define IMG_LOGE(x, ...) ALOGE("[%s] " x, st.mName.c_str(), ##__VA_ARGS__)
#define IMG_LOGV(x, ...) ALOGV("[%s] " x, st.mName.c_str(), ##__VA_ARGS__)

namespace android {

sp<GraphicBuffer> ImageConsumer::dequeueBuffer(int* outSlotid, android_dataspace* outDataspace,
                                               HdrMetadata* outHdrMetadata, bool* outQueueEmpty,
                                               SurfaceTexture& st,
                                               SurfaceTexture_createReleaseFence createFence,
                                               SurfaceTexture_fenceWait fenceWait,
                                               void* fencePassThroughHandle) {
    BufferItem item;
    status_t err;
    err = st.acquireBufferLocked(&item, 0);
    if (err != OK) {
        if (err != BufferQueue::NO_BUFFER_AVAILABLE) {
            IMG_LOGE("Error acquiring buffer: %s (%d)", strerror(-err), err);
        } else {
            int slot = getDequeueBufferSlot(st.mCurrentTexture);
            if (slot != BufferItem::INVALID_BUFFER_SLOT) {
                *outQueueEmpty = true;
                *outDataspace = st.mCurrentDataSpace;
                *outSlotid = slot;
                return st.mCurrentTexture;
            }
        }
        return nullptr;
    }

    const sp<GraphicBuffer> buffer = item.mGraphicBuffer;
    if (!mImageData.contains(buffer)) {
        int slotId;
        if (mRecycledSlots.empty()) {
            slotId = mNextSlot++;
        } else {
            slotId = mRecycledSlots.back();
            mRecycledSlots.pop_back();
        }
        mImageData.emplace_or_replace(buffer, ImageData(slotId));
    }

    *outQueueEmpty = false;
    if (item.mFence->isValid()) {
        // If fence is not signaled, that means frame is not ready and
        // outQueueEmpty is set to true. By the time the fence is signaled,
        // there may be a new buffer queued. This is a proper detection for an
        // empty queue and it is needed to avoid infinite loop in
        // ASurfaceTexture_dequeueBuffer (see b/159921224).
        *outQueueEmpty = item.mFence->getStatus() == Fence::Status::Unsignaled;

        // Wait on the producer fence for the buffer to be ready.
        err = fenceWait(item.mFence->get(), fencePassThroughHandle);
        if (err != OK) {
            st.releaseBufferLocked(buffer);
            return nullptr;
        }
    }

    // Release old buffer.
    if (st.mCurrentTexture) {
        // If needed, set the released slot's fence to guard against a producer
        // accessing the buffer before the outstanding accesses have completed.
        int releaseFenceId = -1;
        EGLDisplay display = EGL_NO_DISPLAY;
        EGLSyncKHR eglFence = EGL_NO_SYNC_KHR;
        err = createFence(st.mUseFenceSync, &eglFence, &display, &releaseFenceId,
                          fencePassThroughHandle);
        if (OK != err) {
            IMG_LOGE("Error creating release fence: %s (%d)", strerror(-err), err);
            st.releaseBufferLocked(buffer);
            return nullptr;
        }
        mImageData.get(buffer)->get().eglFence() = eglFence;

        if (releaseFenceId != -1) {
            sp<Fence> releaseFence(new Fence(releaseFenceId));
            st.mCurrentFence =
                    Fence::merge("ImageConsumer::dequeueBuffer", releaseFence, st.mCurrentFence);
        }

        // Finally release the old buffer.
        EGLSyncKHR previousFence = mImageData.get(st.mCurrentTexture)->get().eglFence();
        if (previousFence != EGL_NO_SYNC_KHR) {
            // Most platforms will be using native fences, so it's unlikely that we'll ever have to
            // process an eglFence. Ideally we can remove this code eventually. In the mean time, do
            // our best to wait for it so the buffer stays valid, otherwise return an error to the
            // caller.
            //
            // EGL_SYNC_FLUSH_COMMANDS_BIT_KHR so that we don't wait forever on a fence that hasn't
            // shown up on the GPU yet.
            EGLint result = eglClientWaitSyncKHR(display, previousFence,
                                                 EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, 1000000000);
            if (result == EGL_FALSE) {
                IMG_LOGE("dequeueBuffer: error %#x waiting for fence", eglGetError());
            } else if (result == EGL_TIMEOUT_EXPIRED_KHR) {
                IMG_LOGE("dequeueBuffer: timeout waiting for fence");
            }
            eglDestroySyncKHR(display, previousFence);
        }

        status_t status = st.releaseBufferLocked(st.mCurrentTexture);
        if (status < NO_ERROR) {
            IMG_LOGE("dequeueImage: failed to release buffer: %s (%d)", strerror(-status), status);
            err = status;
            // Keep going, with error raised.
        }
    }

    // Update the state.
    st.mCurrentTexture = buffer;
    st.mCurrentCrop = item.mCrop;
    st.mCurrentTransform = item.mTransform;
    st.mCurrentScalingMode = item.mScalingMode;
    st.mCurrentTimestamp = item.mTimestamp;
    st.mCurrentDataSpace = item.mDataSpace;
    st.mCurrentFence = item.mFence;
    st.mCurrentFenceTime = item.mFenceTime;
    st.mCurrentFrameNumber = item.mFrameNumber;
    st.computeCurrentTransformMatrixLocked();

    *outDataspace = item.mDataSpace;
    *outHdrMetadata = item.mHdrMetadata;
    *outSlotid = getDequeueBufferSlot(buffer);
    return buffer;
}

void ImageConsumer::onAbandonLocked() {
    mImageData = {};
    mNextSlot = 0;
    mRecycledSlots = {};
}

void ImageConsumer::onReleaseBufferLocked(const sp<GraphicBuffer>& graphicBuffer) {
    if (mImageData.contains(graphicBuffer)) {
        mImageData.get(graphicBuffer)->get().eglFence() = EGL_NO_SYNC_KHR;
    }
}

void ImageConsumer::onFreeBufferLocked(const SurfaceTexture& st, const sp<GraphicBuffer>& buffer) {
    IMG_LOGV("onFreeBufferLocked buffer=%" PRIu64, buffer ? buffer->getId() : 0);

    int slot = getDequeueBufferSlot(buffer);
    if (slot != BufferItem::INVALID_BUFFER_SLOT) {
        mRecycledSlots.push_back(slot);
    }
    mImageData.erase(buffer);
}

int ImageConsumer::getDequeueBufferSlot(const sp<GraphicBuffer>& buffer) const {
    const auto imageData = mImageData.get(buffer);
    if (imageData) {
        return imageData->get().slot();
    }
    return BufferItem::INVALID_BUFFER_SLOT;
}

} /* namespace android */
