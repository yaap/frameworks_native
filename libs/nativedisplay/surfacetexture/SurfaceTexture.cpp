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

#include <cutils/compiler.h>
#include <ftl/small_vector.h>
#include <gui/BufferItemConsumer.h>
#include <gui/BufferQueue.h>
#include <gui/Surface.h>
#include <math/mat4.h>
#include <surfacetexture/ImageConsumer.h>
#include <surfacetexture/SurfaceTexture.h>
#include <surfacetexture/surface_texture_platform.h>
#include <system/window.h>
#include <ui/GraphicBuffer.h>
#include <utils/Trace.h>

#include <com_android_graphics_libgui_flags.h>
#include <atomic>
#include <mutex>
#include <string>

namespace android {

// Macros for including the SurfaceTexture name in log messages
#define SFT_LOGV(x, ...) ALOGV("[%s] " x, mName.c_str(), ##__VA_ARGS__)
#define SFT_LOGD(x, ...) ALOGD("[%s] " x, mName.c_str(), ##__VA_ARGS__)
#define SFT_LOGW(x, ...) ALOGW("[%s] " x, mName.c_str(), ##__VA_ARGS__)
#define SFT_LOGE(x, ...) ALOGE("[%s] " x, mName.c_str(), ##__VA_ARGS__)

static const mat4 mtxIdentity;

SurfaceTexture::SurfaceTexture(uint32_t tex, uint32_t texTarget, bool useFenceSync,
                               bool isControlledByApp)
      : mCurrentCrop(Rect::EMPTY_RECT),
        mCurrentTransform(0),
        mCurrentScalingMode(NATIVE_WINDOW_SCALING_MODE_FREEZE),
        mCurrentFence(Fence::NO_FENCE),
        mCurrentTimestamp(0),
        mCurrentDataSpace(HAL_DATASPACE_UNKNOWN),
        mCurrentFrameNumber(0),
        mDefaultWidth(1),
        mDefaultHeight(1),
        mFilteringEnabled(true),
        mTexName(tex),
        mUseFenceSync(useFenceSync),
        mTexTarget(texTarget),
        mCurrentTexture(nullptr),
        mOpMode(OpMode::attachedToGL) {
    std::tie(mConsumer, mSurface) =
            BufferItemConsumer::create(DEFAULT_USAGE_FLAGS, BufferItemConsumer::DEFAULT_MAX_BUFFERS,
                                       isControlledByApp);
}

SurfaceTexture::SurfaceTexture(uint32_t texTarget, bool useFenceSync, bool isControlledByApp)
      : mCurrentCrop(Rect::EMPTY_RECT),
        mCurrentTransform(0),
        mCurrentScalingMode(NATIVE_WINDOW_SCALING_MODE_FREEZE),
        mCurrentFence(Fence::NO_FENCE),
        mCurrentTimestamp(0),
        mCurrentDataSpace(HAL_DATASPACE_UNKNOWN),
        mCurrentFrameNumber(0),
        mDefaultWidth(1),
        mDefaultHeight(1),
        mFilteringEnabled(true),
        mTexName(0),
        mUseFenceSync(useFenceSync),
        mTexTarget(texTarget),
        mCurrentTexture(nullptr),
        mOpMode(OpMode::detached) {
    std::tie(mConsumer, mSurface) =
            BufferItemConsumer::create(DEFAULT_USAGE_FLAGS, BufferItemConsumer::DEFAULT_MAX_BUFFERS,
                                       isControlledByApp);
}

status_t SurfaceTexture::setDefaultBufferSize(uint32_t w, uint32_t h) {
    std::scoped_lock _l(mMutex);
    if (mAbandoned) {
        SFT_LOGE("setDefaultBufferSize: SurfaceTexture is abandoned!");
        return NO_INIT;
    }
    mDefaultWidth = w;
    mDefaultHeight = h;
    return mConsumer->setDefaultBufferSize(w, h);
}

status_t SurfaceTexture::updateTexImage() {
    ATRACE_CALL();
    SFT_LOGV("updateTexImage");
    std::scoped_lock _l(mMutex);

    if (mAbandoned) {
        SFT_LOGE("updateTexImage: SurfaceTexture is abandoned!");
        return NO_INIT;
    }

    clearPendingFreedBuffersLocked();

#if COM_ANDROID_GRAPHICS_LIBGUI_FLAGS(SURFACE_TEXTURE_UPDATETEXIMAGE_STOP_EARLY)
    if (mOpMode != OpMode::attachedToGL) {
        SFT_LOGE("updateTexImage: SurfaceTexture is not attached!");
        return INVALID_OPERATION;
    }
#endif
    return mEGLConsumer.updateTexImage(*this);
}

status_t SurfaceTexture::releaseTexImage() {
    // releaseTexImage can be invoked even when not attached to a GL context.
    ATRACE_CALL();
    SFT_LOGV("releaseTexImage");
    std::scoped_lock _l(mMutex);

    if (mAbandoned) {
        SFT_LOGE("releaseTexImage: SurfaceTexture is abandoned!");
        return NO_INIT;
    }

    clearPendingFreedBuffersLocked();

    return mEGLConsumer.releaseTexImage(*this);
}

status_t SurfaceTexture::acquireBufferLocked(BufferItem* item, nsecs_t presentWhen,
                                             uint64_t maxFrameNumber) {
    status_t err =
            mConsumer->acquireBuffer(item, presentWhen, maxFrameNumber,
                                     [&](auto& maybeBuffer) { onBufferFreedLocked(maybeBuffer); });
    if (err != NO_ERROR) {
        return err;
    }

    switch (mOpMode) {
        case OpMode::attachedToConsumer:
            break;
        case OpMode::attachedToGL:
            mEGLConsumer.onAcquireBufferLocked(item);
            break;
        case OpMode::detached:
            break;
    }

    return NO_ERROR;
}

status_t SurfaceTexture::releaseBufferLocked(const sp<GraphicBuffer>& graphicBuffer) {
    // release the buffer if it hasn't already been discarded by the
    // BufferQueue. This can happen, for example, when the producer of this
    // buffer has reallocated the original buffer slot after this buffer
    // was acquired.
    status_t err = mConsumer->releaseBuffer(graphicBuffer, Fence::NO_FENCE, [&](auto& maybeBuffer) {
        onBufferFreedLocked(maybeBuffer);
    });

    // We could be releasing an EGL/Vulkan buffer, even if not currently
    // attached to a GL context.
    mImageConsumer.onReleaseBufferLocked(graphicBuffer);
    return err;
}

status_t SurfaceTexture::detachFromContext() {
    ATRACE_CALL();
    SFT_LOGV("detachFromContext");
    std::scoped_lock _l(mMutex);

    if (mAbandoned) {
        SFT_LOGE("detachFromContext: abandoned SurfaceTexture");
        return NO_INIT;
    }

    clearPendingFreedBuffersLocked();

    if (mOpMode != OpMode::attachedToGL) {
        SFT_LOGE("detachFromContext: SurfaceTexture is not attached to a GL context");
        return INVALID_OPERATION;
    }

    status_t err = mEGLConsumer.detachFromContext(*this);
    if (err == OK) {
        mOpMode = OpMode::detached;
    }

    return err;
}

status_t SurfaceTexture::attachToContext(uint32_t tex) {
    ATRACE_CALL();
    SFT_LOGV("attachToContext");
    std::scoped_lock _l(mMutex);

    if (mAbandoned) {
        SFT_LOGE("attachToContext: abandoned SurfaceTexture");
        return NO_INIT;
    }

    clearPendingFreedBuffersLocked();

    if (mOpMode != OpMode::detached) {
        SFT_LOGE("attachToContext: SurfaceTexture is already attached to a "
                 "context");
        return INVALID_OPERATION;
    }

    return mEGLConsumer.attachToContext(tex, *this);
}

void SurfaceTexture::takeConsumerOwnership() {
    ATRACE_CALL();
    std::scoped_lock _l(mMutex);
    if (mAbandoned) {
        SFT_LOGE("attachToView: abandoned SurfaceTexture");
        return;
    }

    clearPendingFreedBuffersLocked();

    if (mOpMode == OpMode::detached) {
        mOpMode = OpMode::attachedToConsumer;

        if (!mCurrentTexture) {
            // release possible EGLConsumer texture cache
            mEGLConsumer.onFreeBufferLocked(*this, mCurrentTexture);
            mEGLConsumer.onAbandonLocked();
        }
    } else {
        SFT_LOGE("attachToView: already attached");
    }
}

void SurfaceTexture::releaseConsumerOwnership() {
    ATRACE_CALL();
    std::scoped_lock _l(mMutex);

    if (mAbandoned) {
        SFT_LOGE("detachFromView: abandoned SurfaceTexture");
        return;
    }

    clearPendingFreedBuffersLocked();

    if (mOpMode == OpMode::attachedToConsumer) {
        mOpMode = OpMode::detached;
    } else {
        SFT_LOGE("detachFromView: not attached to View");
    }
}

uint32_t SurfaceTexture::getCurrentTextureTarget() const {
    return mTexTarget;
}

void SurfaceTexture::getTransformMatrix(float mtx[16]) {
    std::scoped_lock _l(mMutex);
    memcpy(mtx, mCurrentTransformMatrix, sizeof(mCurrentTransformMatrix));
}

void SurfaceTexture::setFilteringEnabled(bool enabled) {
    std::scoped_lock _l(mMutex);
    if (mAbandoned) {
        SFT_LOGE("setFilteringEnabled: SurfaceTexture is abandoned!");
        return;
    }
    bool needsRecompute = mFilteringEnabled != enabled;
    mFilteringEnabled = enabled;

    if (needsRecompute && mCurrentTexture) {
        SFT_LOGD("setFilteringEnabled called with no current item");
    }

    if (needsRecompute && mCurrentTexture) {
        computeCurrentTransformMatrixLocked();
    }
}

status_t SurfaceTexture::setConsumerIsProtected(bool isProtected) {
    SFT_LOGV("computeCurrentTransformMatrixLocked");
    return mConsumer->setConsumerIsProtected(isProtected);
}

void SurfaceTexture::computeCurrentTransformMatrixLocked() {
    SFT_LOGV("computeCurrentTransformMatrixLocked");
    if (mCurrentTexture == nullptr) {
        SFT_LOGD("computeCurrentTransformMatrixLocked: no current item");
    }
    computeTransformMatrix(mCurrentTransformMatrix, mCurrentTexture, mCurrentCrop,
                           mCurrentTransform, mFilteringEnabled);
}

void SurfaceTexture::computeTransformMatrix(float outTransform[16], const sp<GraphicBuffer>& buf,
                                            const Rect& cropRect, uint32_t transform,
                                            bool filtering) {
    // Transform matrices
    static const mat4 mtxFlipH(-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1);
    static const mat4 mtxFlipV(1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1);
    static const mat4 mtxRot90(0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1);

    mat4 xform;
    if (transform & NATIVE_WINDOW_TRANSFORM_FLIP_H) {
        xform *= mtxFlipH;
    }
    if (transform & NATIVE_WINDOW_TRANSFORM_FLIP_V) {
        xform *= mtxFlipV;
    }
    if (transform & NATIVE_WINDOW_TRANSFORM_ROT_90) {
        xform *= mtxRot90;
    }

    if (!cropRect.isEmpty() && buf.get()) {
        float tx = 0.0f, ty = 0.0f, sx = 1.0f, sy = 1.0f;
        float bufferWidth = buf->getWidth();
        float bufferHeight = buf->getHeight();
        float shrinkAmount = 0.0f;
        if (filtering) {
            // In order to prevent bilinear sampling beyond the edge of the
            // crop rectangle we may need to shrink it by 2 texels in each
            // dimension.  Normally this would just need to take 1/2 a texel
            // off each end, but because the chroma channels of YUV420 images
            // are subsampled we may need to shrink the crop region by a whole
            // texel on each side.
            switch (buf->getPixelFormat()) {
                case PIXEL_FORMAT_RGBA_8888:
                case PIXEL_FORMAT_RGBX_8888:
                case PIXEL_FORMAT_RGBA_FP16:
                case PIXEL_FORMAT_RGBA_1010102:
                case PIXEL_FORMAT_RGB_888:
                case PIXEL_FORMAT_RGB_565:
                case PIXEL_FORMAT_BGRA_8888:
                    // We know there's no subsampling of any channels, so we
                    // only need to shrink by a half a pixel.
                    shrinkAmount = 0.5;
                    break;

                default:
                    // If we don't recognize the format, we must assume the
                    // worst case (that we care about), which is YUV420.
                    shrinkAmount = 1.0;
                    break;
            }
        }

        // Only shrink the dimensions that are not the size of the buffer.
        if (cropRect.width() < bufferWidth) {
            tx = (float(cropRect.left) + shrinkAmount) / bufferWidth;
            sx = (float(cropRect.width()) - (2.0f * shrinkAmount)) / bufferWidth;
        }
        if (cropRect.height() < bufferHeight) {
            ty = (float(bufferHeight - cropRect.bottom) + shrinkAmount) / bufferHeight;
            sy = (float(cropRect.height()) - (2.0f * shrinkAmount)) / bufferHeight;
        }

        mat4 crop(sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, 1, 0, tx, ty, 0, 1);
        xform = crop * xform;
    }

    // SurfaceFlinger expects the top of its window textures to be at a Y
    // coordinate of 0, so SurfaceTexture must behave the same way.  We don't
    // want to expose this to applications, however, so we must add an
    // additional vertical flip to the transform after all the other transforms.
    xform = mtxFlipV * xform;

    memcpy(outTransform, xform.asArray(), sizeof(xform));
}

Rect SurfaceTexture::scaleDownCrop(const Rect& crop, uint32_t bufferWidth, uint32_t bufferHeight) {
    Rect outCrop = crop;

    uint32_t newWidth = static_cast<uint32_t>(crop.width());
    uint32_t newHeight = static_cast<uint32_t>(crop.height());

    if (newWidth * bufferHeight > newHeight * bufferWidth) {
        newWidth = newHeight * bufferWidth / bufferHeight;
        ALOGV("too wide: newWidth = %d", newWidth);
    } else if (newWidth * bufferHeight < newHeight * bufferWidth) {
        newHeight = newWidth * bufferHeight / bufferWidth;
        ALOGV("too tall: newHeight = %d", newHeight);
    }

    uint32_t currentWidth = static_cast<uint32_t>(crop.width());
    uint32_t currentHeight = static_cast<uint32_t>(crop.height());

    // The crop is too wide
    if (newWidth < currentWidth) {
        uint32_t dw = currentWidth - newWidth;
        auto halfdw = dw / 2;
        outCrop.left += halfdw;
        // Not halfdw because it would subtract 1 too few when dw is odd
        outCrop.right -= (dw - halfdw);
        // The crop is too tall
    } else if (newHeight < currentHeight) {
        uint32_t dh = currentHeight - newHeight;
        auto halfdh = dh / 2;
        outCrop.top += halfdh;
        // Not halfdh because it would subtract 1 too few when dh is odd
        outCrop.bottom -= (dh - halfdh);
    }

    ALOGV("getCurrentCrop final crop [%d,%d,%d,%d]", outCrop.left, outCrop.top, outCrop.right,
          outCrop.bottom);

    return outCrop;
}

nsecs_t SurfaceTexture::getTimestamp() {
    SFT_LOGV("getTimestamp");
    std::scoped_lock _l(mMutex);
    return mCurrentTimestamp;
}

android_dataspace SurfaceTexture::getCurrentDataSpace() {
    SFT_LOGV("getCurrentDataSpace");
    std::scoped_lock _l(mMutex);
    return mCurrentDataSpace;
}

uint64_t SurfaceTexture::getFrameNumber() {
    SFT_LOGV("getFrameNumber");
    std::scoped_lock _l(mMutex);
    return mCurrentFrameNumber;
}

Rect SurfaceTexture::getCurrentCrop() const {
    std::scoped_lock _l(mMutex);
    return (mCurrentScalingMode == NATIVE_WINDOW_SCALING_MODE_SCALE_CROP)
            ? scaleDownCrop(mCurrentCrop, mDefaultWidth, mDefaultHeight)
            : mCurrentCrop;
}

uint32_t SurfaceTexture::getCurrentTransform() const {
    std::scoped_lock _l(mMutex);
    return mCurrentTransform;
}

uint32_t SurfaceTexture::getCurrentScalingMode() const {
    std::scoped_lock _l(mMutex);
    return mCurrentScalingMode;
}

sp<Fence> SurfaceTexture::getCurrentFence() const {
    std::scoped_lock _l(mMutex);
    return mCurrentFence;
}

std::shared_ptr<FenceTime> SurfaceTexture::getCurrentFenceTime() const {
    std::scoped_lock _l(mMutex);
    return mCurrentFenceTime;
}

void SurfaceTexture::onBufferFreed(const sp<GraphicBuffer>& maybeBuffer) {
    std::unique_lock lock(mMutex, std::try_to_lock);
    if (lock.owns_lock()) {
        onBufferFreedLocked(maybeBuffer);
    } else {
        std::scoped_lock _l(mPendingFreedBuffersLock);
        mPendingFreedBuffers.push_back(maybeBuffer);
    }
}

void SurfaceTexture::clearPendingFreedBuffersLocked() {
    PendingFreedBuffers pendingFreedBuffers;
    {
        std::scoped_lock _l(mPendingFreedBuffersLock);
        pendingFreedBuffers.swap(mPendingFreedBuffers);
    }
    for (const auto& buffer : pendingFreedBuffers) {
        onBufferFreedLocked(buffer);
    }
}

void SurfaceTexture::onBufferFreedLocked(const sp<GraphicBuffer>& buffer) {
    if (buffer == nullptr) {
        return;
    }

    SFT_LOGV("onBufferFreed: bufferId=%" PRIu64, buffer->getId());
    if (buffer == mCurrentTexture) {
        mCurrentTexture = nullptr;
    }
    // The slotIndex buffer could have EGL cache, but there is no way to tell
    // for sure. Buffers can be freed after SurfaceTexture has detached from GL
    // context or View.
    mEGLConsumer.onFreeBufferLocked(*this, buffer);
    mImageConsumer.onFreeBufferLocked(*this, buffer);
}

void SurfaceTexture::onFrameAvailable(const BufferItem& item) {
    // NOTE: This can be called in a deadlock-y way, in certain circumstancs, from EGL. Do NOT take
    // mMutex here.
    sp<SurfaceTextureListener> listener;
    {
        std::scoped_lock _l(mListenerMutex);
        listener = mSurfaceTextureListener;
    }

    if (listener) {
        listener->onFrameAvailable(item);
    }
}

void SurfaceTexture::onSetFrameRate(float frameRate, int8_t compatibility,
                                    int8_t changeFrameRateStrategy) {
    sp<SurfaceTextureListener> listener;
    {
        std::scoped_lock _l(mListenerMutex);
        listener = mSurfaceTextureListener;
    }

    if (listener) {
        listener->onSetFrameRate(frameRate, compatibility, changeFrameRateStrategy);
    }
}
status_t SurfaceTexture::setConsumerUsageBits(uint64_t usage) {
    return mConsumer->setConsumerUsageBits(usage | DEFAULT_USAGE_FLAGS);
}

void SurfaceTexture::dumpState(String8& result, const char* prefix) {
    std::scoped_lock _l(mMutex);
    auto currentTextureId = mCurrentTexture ? std::to_string(mCurrentTexture->getId()) : "n/a";
    result.appendFormat("%smTexName=%d mCurrentTexture=%s\n"
                        "%smCurrentCrop=[%d,%d,%d,%d] mCurrentTransform=%#x\n",
                        prefix, mTexName, currentTextureId.c_str(), prefix, mCurrentCrop.left,
                        mCurrentCrop.top, mCurrentCrop.right, mCurrentCrop.bottom,
                        mCurrentTransform);

    mConsumer->dumpState(result, prefix);
}

sp<GraphicBuffer> SurfaceTexture::dequeueBuffer(int* outSlotid, android_dataspace* outDataspace,
                                                HdrMetadata* outHdrMetadata,
                                                float* outTransformMatrix, uint32_t* outTransform,
                                                bool* outQueueEmpty,
                                                SurfaceTexture_createReleaseFence createFence,
                                                SurfaceTexture_fenceWait fenceWait,
                                                void* fencePassThroughHandle, ARect* currentCrop) {
    std::scoped_lock _l(mMutex);
    sp<GraphicBuffer> buffer;

    if (mAbandoned) {
        SFT_LOGE("dequeueImage: SurfaceTexture is abandoned!");
        return buffer;
    }

    if (mOpMode != OpMode::attachedToConsumer) {
        SFT_LOGE("dequeueImage: SurfaceTexture is not attached to a View");
        return buffer;
    }

    buffer = mImageConsumer.dequeueBuffer(outSlotid, outDataspace, outHdrMetadata, outQueueEmpty,
                                          *this, createFence, fenceWait, fencePassThroughHandle);
    memcpy(outTransformMatrix, mCurrentTransformMatrix, sizeof(mCurrentTransformMatrix));
    *outTransform = mCurrentTransform;
    *currentCrop = mCurrentCrop;
    return buffer;
}

void SurfaceTexture::setSurfaceTextureListener(
        const sp<android::SurfaceTexture::SurfaceTextureListener>& listener) {
    SFT_LOGV("setSurfaceTextureListener");

    std::scoped_lock _l(mListenerMutex);
    mSurfaceTextureListener = listener;
}

status_t SurfaceTexture::setMaxBufferCount(int bufferCount) {
    return mConsumer->setMaxBufferCount(bufferCount);
}

void SurfaceTexture::setName(String8 name) {
    mConsumer->setName(name);
}

void SurfaceTexture::abandon() {
    SFT_LOGV("abandon");
    std::scoped_lock _l(mMutex);

    if (mAbandoned) {
        SFT_LOGE("abandon: SurfaceTexture is already abandoned!");
        return;
    }

    mConsumer->abandon([&](const sp<GraphicBuffer>& buffer) { onBufferFreedLocked(buffer); });
    mEGLConsumer.onAbandonLocked();
    mImageConsumer.onAbandonLocked();
    clearPendingFreedBuffersLocked();
    mAbandoned = true;
}

bool SurfaceTexture::isAbandoned() const {
    std::scoped_lock _l(mMutex);
    return mAbandoned;
}

void SurfaceTexture::onFirstRef() {
    static std::atomic_int32_t sSurfaceTextureId = 0;

    SFT_LOGV("SurfaceTexture::onFirstRef");

    memcpy(mCurrentTransformMatrix, mtxIdentity.asArray(), sizeof(mCurrentTransformMatrix));

    mConsumer->setConsumerUsageBits(DEFAULT_USAGE_FLAGS);

    mName = String8::format("SurfaceTexture-%d-%d", getpid(), sSurfaceTextureId++);
    mConsumer->setName(mName);

    mConsumer->setBufferFreedListener(wp<BufferFreedListener>::fromExisting(this));
    mConsumer->setFrameAvailableListener(wp<FrameAvailableListener>::fromExisting(this));
}

} // namespace android
