/*
 * Copyright (C) 2007 The Android Open Source Project
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

#ifndef ANDROID_SF_FRAMEBUFFER_SURFACE_H
#define ANDROID_SF_FRAMEBUFFER_SURFACE_H

#include <stdint.h>
#include <sys/types.h>

#include <com_android_graphics_libgui_flags.h>
#include <compositionengine/DisplaySurface.h>
#include <gui/BufferItemConsumer.h>
#include <gui/BufferQueue.h>
#include <ui/DisplayId.h>
#include <ui/Size.h>
#include <utils/Mutex.h>

#include "DisplayHardware/HwcSlotTracker.h"

// ---------------------------------------------------------------------------
namespace android {
// ---------------------------------------------------------------------------

class Rect;
class String8;
class HWComposer;

// ---------------------------------------------------------------------------

class FramebufferSurface final : public compositionengine::DisplaySurface,
                                 public BufferItemConsumer::BufferFreedListener {
public:
    virtual status_t beginFrame(bool mustRecompose);
    virtual status_t prepareFrame(CompositionType compositionType);
    virtual status_t advanceFrame(float hdrSdrRatio);
    virtual void onFrameCommitted();
    virtual void dumpAsString(String8& result) const;

    virtual void onFirstRef() override;

    virtual void resizeBuffers(const ui::Size&) override;

    const sp<Surface>& getSurface() { return mRendererSurface; }

    virtual const sp<Fence>& getClientTargetAcquireFence() const override;

private:
    friend class FramebufferSurfaceTest;
    friend class sp<FramebufferSurface>;

    FramebufferSurface(HWComposer& hwc, PhysicalDisplayId displayId, const ui::Size& size,
                       const ui::Size& maxSize);

    // BufferFreedListener interface
    void onBufferFreed(const sp<GraphicBuffer>& graphicBuffer) override;
    void onBufferFreedLocked(const sp<GraphicBuffer>& graphicBuffer) REQUIRES(mMutex);

    // Limits the width and height by the maximum width specified.
    ui::Size limitSize(const ui::Size&);

    // Used for testing purposes.
    static ui::Size limitSizeInternal(const ui::Size&, const ui::Size& maxSize);

    mutable Mutex mMutex;
    const PhysicalDisplayId mDisplayId;

    // This surface is used by CompositionEngine for GPU rendering. It's sent to HWC as the client
    // target for the frame.
    sp<Surface> mRendererSurface;
    sp<BufferItemConsumer> mRendererConsumer;

    // Framebuffer size has a dimension limitation in pixels based on the graphics capabilities of
    // the device.
    const ui::Size mMaxSize;
    const ui::Size mLimitedSize;

    // mDataSpace is the dataspace of the current composition buffer for
    // this FramebufferSurface. It will be 0 when HWC is doing the
    // compositing. Otherwise it will display the dataspace of the buffer
    // use for compositing which can change as wide-color content is
    // on/off.
    ui::Dataspace mDataspace;

    struct FrameData {
        // Buffer for a particular frame.
        sp<GraphicBuffer> mBuffer;
        // Acquire fence for the above buffer.
        sp<Fence> mFence;
    };

    std::optional<FrameData> mFrameData;
    std::optional<FrameData> mPreviousFrameData;

    // Hardware composer, owned by SurfaceFlinger.
    HWComposer& mHwc;

    // Slot tracker to map buffers to HWC slot IDs
    HwcSlotTracker mHwcSlotTracker GUARDED_BY(mMutex);
};

// ---------------------------------------------------------------------------
}; // namespace android
// ---------------------------------------------------------------------------

#endif // ANDROID_SF_FRAMEBUFFER_SURFACE_H
