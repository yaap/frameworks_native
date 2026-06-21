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

#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cutils/compiler.h>
#include <ftl/small_map.h>
#include <gui/BufferItem.h>
#include <gui/BufferQueueDefs.h>
#include <sys/cdefs.h>
#include <system/graphics.h>
#include <ui/BufferQueueDefs.h>
#include <ui/GraphicBuffer.h>

#include <vector>

namespace android {

class SurfaceTexture;
class DequeueBufferCallbacks;

/*
 * ImageConsumer implements the parts of SurfaceTexture that deal with
 * images consumed by HWUI view system.
 */
class ImageConsumer {
public:
    typedef status_t (*SurfaceTexture_createReleaseFence)(bool useFenceSync, EGLSyncKHR* eglFence,
                                                          EGLDisplay* display, int* releaseFence,
                                                          void* fencePassThroughHandle);

    typedef status_t (*SurfaceTexture_fenceWait)(int fence, void* fencePassThroughHandle);

    sp<GraphicBuffer> dequeueBuffer(int* outSlotid, android_dataspace* outDataspace,
                                    HdrMetadata* outHdrMetadata, bool* outQueueEmpty,
                                    SurfaceTexture& cb,
                                    SurfaceTexture_createReleaseFence createFence,
                                    SurfaceTexture_fenceWait fenceWait,
                                    void* fencePassThroughHandle);

    void onAbandonLocked();
    void onReleaseBufferLocked(const sp<GraphicBuffer>& buffer);
    void onFreeBufferLocked(const SurfaceTexture& st, const sp<GraphicBuffer>& buffer);

private:
    /**
     * ImageData contains the information and object references that
     * ImageConsumer maintains about a BufferQueue buffer slot.
     */
    class ImageData {
    public:
        ImageData(int slot) : mSlot(slot), mEglFence(EGL_NO_SYNC_KHR) {}

        inline int slot() const { return mSlot; }
        inline EGLSyncKHR& eglFence() { return mEglFence; }

    private:
        /**
         * mSlot is returned by dequeueBuffer as a way of tracking buffers.
         */
        int mSlot;

        /**
         * mEglFence is the EGL sync object that must signal before the buffer
         * associated with this buffer slot may be dequeued.
         */
        EGLSyncKHR mEglFence;
    };

    int getDequeueBufferSlot(const sp<GraphicBuffer>& buffer) const;

    /**
     * ImageConsumer stores the SkImages that have been allocated by the BufferQueue
     * for each buffer. It is filled in with the result of BufferQueue::acquire when the
     * client dequeues a new buffer.
     */
    ftl::SmallMap<sp<GraphicBuffer>, ImageData, BufferQueueDefs::NUM_BUFFER_SLOTS> mImageData;

    int mNextSlot = 0;
    std::vector<int> mRecycledSlots;
};

} /* namespace android */
