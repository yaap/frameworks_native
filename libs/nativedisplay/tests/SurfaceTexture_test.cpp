/*
 * Copyright (C) 2025 The Android Open Source Project
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

#define LOG_TAG "SurfaceTextureTest"

#include <gtest/gtest.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <com_android_graphics_libgui_flags.h>
#include <gui/IConsumerListener.h>
#include <gui/Surface.h>
#include <hardware/gralloc.h>
#include <system/window.h>
#include <ui/BufferQueueDefs.h>
#include <ui/GraphicBuffer.h>
#include <ui/PixelFormat.h>
#include <ui/Rect.h>
#include <utils/Errors.h>

#include <surfacetexture/LegacySurfaceTexture.h>
#include <surfacetexture/SurfaceTexture.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <thread>
#include <vector>

namespace android {

constexpr uint32_t kRed = 0xff0000ff;
constexpr uint32_t kGreen = 0x00ff00ff;
constexpr uint32_t kBlue = 0x0000ffff;
constexpr ui::Size kTextureSize = {4, 4};

using com::android::graphics::libgui::flags::surface_texture_updateteximage_stop_early;
using com::android::graphics::libgui::flags::wb_surfacetexture;

#if COM_ANDROID_GRAPHICS_LIBGUI_FLAGS(WB_SURFACETEXTURE)
typedef SurfaceTexture SurfaceTextureType;
typedef SurfaceTexture::SurfaceTextureListener SurfaceTextureListenerType;
#else
typedef LegacySurfaceTexture SurfaceTextureType;
typedef LegacySurfaceTexture::LegacySurfaceTextureListener SurfaceTextureListenerType;
#endif

status_t DoNothingFenceFn(bool useFenceSync, EGLSyncKHR* eglFence, EGLDisplay* display,
                          int* releaseFence, void* passThroughHandle) {
    (void)useFenceSync;
    (void)eglFence;
    (void)display;
    (void)releaseFence;
    (void)passThroughHandle;
    return OK;
}

status_t DoNothingFenceWaitFn(int fence, void* passThroughHandle) {
    (void)fence;
    (void)passThroughHandle;
    return OK;
}

class SurfaceTextureTest : public testing::Test {
public:
    virtual ~SurfaceTextureTest() = default;

    virtual void SetUp() override {
        // We need a valid GL context so we can test updateTexImage()
        // This initializes EGL and create a GL context placeholder with a
        // pbuffer render target.
        mEglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        ASSERT_EQ(EGL_SUCCESS, eglGetError());
        ASSERT_NE(EGL_NO_DISPLAY, mEglDisplay);

        EGLint majorVersion, minorVersion;
        EXPECT_TRUE(eglInitialize(mEglDisplay, &majorVersion, &minorVersion));
        ASSERT_EQ(EGL_SUCCESS, eglGetError());

        EGLConfig myConfig;
        EGLint numConfigs = 0;
        EXPECT_TRUE(eglChooseConfig(mEglDisplay, getConfigAttribs(), &myConfig, 1, &numConfigs));
        ASSERT_EQ(EGL_SUCCESS, eglGetError());

        mEglConfig = myConfig;
        EGLint pbufferAttribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        mEglSurface = eglCreatePbufferSurface(mEglDisplay, myConfig, pbufferAttribs);
        ASSERT_EQ(EGL_SUCCESS, eglGetError());
        ASSERT_NE(EGL_NO_SURFACE, mEglSurface);

        EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        mEglContext = eglCreateContext(mEglDisplay, myConfig, EGL_NO_CONTEXT, contextAttribs);
        ASSERT_EQ(EGL_SUCCESS, eglGetError());
        ASSERT_NE(EGL_NO_CONTEXT, mEglContext);

        EXPECT_TRUE(eglMakeCurrent(mEglDisplay, mEglSurface, mEglSurface, mEglContext));
        ASSERT_EQ(EGL_SUCCESS, eglGetError());
    }

    virtual void TearDown() override {
        eglMakeCurrent(mEglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(mEglDisplay, mEglContext);
        eglDestroySurface(mEglDisplay, mEglSurface);
        eglTerminate(mEglDisplay);
    }

    sp<SurfaceTextureType> createSurfaceTexture(GLuint textureId) {
        return sp<SurfaceTextureType>::make(textureId, GL_TEXTURE_EXTERNAL_OES,
                                            /* useFenceSync */ true,
                                            /* isControlledByApp */ true);
    }

    sp<SurfaceTextureType> createSingleBufferSurfaceTexture(GLuint textureId) {
        // See frameworks/base/core/jni/android_graphics_SurfaceTexture.cpp
        auto surfaceTexture = sp<SurfaceTextureType>::make(textureId, GL_TEXTURE_EXTERNAL_OES,
                                                           /* useFenceSync */ true,
                                                           /* isControlledByApp */ false);

        surfaceTexture->setMaxBufferCount(1);

        return surfaceTexture;
    }

    void queueColorToSurface(const sp<Surface>& surface, uint32_t color,
                             sp<GraphicBuffer>* outBuffer = nullptr) {
        sp<GraphicBuffer> buffer;
        sp<Fence> fence;
        ASSERT_EQ(OK, surface->dequeueBuffer(&buffer, &fence));
        fillGraphicBuffer(buffer, fence, color);
        // We wait for the fence in the previous function and do all the work locally, so no need to
        // pass a fence to queue.
        ASSERT_EQ(OK, surface->queueBuffer(buffer, Fence::NO_FENCE));

        if (outBuffer) {
            *outBuffer = buffer;
        }
    }

    sp<GraphicBuffer> dequeueFromSurfaceTextureConsumer(
            const sp<SurfaceTextureType>& surfaceTexture) {
        int outSlotid;
        android_dataspace outDataspace;
        HdrMetadata outHdrMetadata;
        float outTransformMatrix[16];
        uint32_t outTransform;
        bool outQueueEmpty;
        void* fencePassThroughHandle = nullptr;
        ARect currentCrop;
        return surfaceTexture->dequeueBuffer(&outSlotid, &outDataspace, &outHdrMetadata,
                                             outTransformMatrix, &outTransform, &outQueueEmpty,
                                             DoNothingFenceFn, DoNothingFenceWaitFn,
                                             fencePassThroughHandle, &currentCrop);
    }

    void fillGraphicBuffer(const sp<GraphicBuffer>& buffer, const sp<Fence>& fence,
                           uint32_t color) {
        ASSERT_NE(nullptr, buffer);
        ASSERT_NE(nullptr, fence);
        ASSERT_EQ(PIXEL_FORMAT_RGBA_8888, buffer->getPixelFormat());

        ASSERT_EQ(OK, fence->waitForever("fillGraphicBuffer"));

        uint8_t* dst = nullptr;
        status_t err = buffer->lock(GRALLOC_USAGE_SW_WRITE_OFTEN, (void**)&dst);
        ASSERT_EQ(OK, err);
        ASSERT_NE(nullptr, dst);

        const int stride = buffer->getStride();
        const int bytesPerPixel = 4;
        const uint8_t r = (color >> 24) & 0xff;
        const uint8_t g = (color >> 16) & 0xff;
        const uint8_t b = (color >> 8) & 0xff;
        const uint8_t a = color & 0xff;

        for (uint32_t y = 0; y < buffer->getHeight(); y++) {
            for (uint32_t x = 0; x < buffer->getWidth(); x++) {
                dst[(y * stride + x) * bytesPerPixel + 0] = r;
                dst[(y * stride + x) * bytesPerPixel + 1] = g;
                dst[(y * stride + x) * bytesPerPixel + 2] = b;
                dst[(y * stride + x) * bytesPerPixel + 3] = a;
            }
        }

        err = buffer->unlock();
        ASSERT_EQ(OK, err);
    }

    testing::AssertionResult checkAllPixelsMatch(GLuint textureId, uint32_t color) {
        std::vector<uint8_t> pixels = getTexturePixelData(textureId);
        const uint8_t r = (color >> 24) & 0xff;
        const uint8_t g = (color >> 16) & 0xff;
        const uint8_t b = (color >> 8) & 0xff;
        const uint8_t a = color & 0xff;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            if (pixels[i + 0] != r || pixels[i + 1] != g || pixels[i + 2] != b ||
                pixels[i + 3] != a) {
                return testing::AssertionFailure()
                        << "Pixel at index " << i << " was (" << (int)pixels[i + 0] << ", "
                        << (int)pixels[i + 1] << ", " << (int)pixels[i + 2] << ", "
                        << (int)pixels[i + 3] << "), expected (" << (int)r << ", " << (int)g << ", "
                        << (int)b << ", " << (int)a << ").";
            }
        }
        return testing::AssertionSuccess();
    }

    std::vector<uint8_t> getTexturePixelData(GLuint textureId) {
        GLuint fbo;
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_EXTERNAL_OES,
                               textureId, 0);

        int width = kTextureSize.width;
        int height = kTextureSize.height;
        std::vector<uint8_t> pixels(width * height * 4);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &fbo);

        EXPECT_EQ(EGL_SUCCESS, eglGetError());

        return pixels;
    }

protected:
    EGLint const* getConfigAttribs() {
        static EGLint sDefaultConfigAttribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT | EGL_WINDOW_BIT,
                                                 EGL_NONE};

        return sDefaultConfigAttribs;
    }

    EGLDisplay mEglDisplay = EGL_NO_DISPLAY;
    EGLSurface mEglSurface = EGL_NO_SURFACE;
    EGLContext mEglContext = EGL_NO_CONTEXT;
    EGLConfig mEglConfig = nullptr;
};

TEST_F(SurfaceTextureTest, UpdateTexImage) {
    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);

    sp<SurfaceTextureType> surfaceTexture = createSurfaceTexture(textureId);

    ASSERT_EQ(OK, surfaceTexture->setDefaultBufferSize(kTextureSize.width, kTextureSize.height));

    sp<Surface> surface = surfaceTexture->getSurface();
    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));
    ASSERT_EQ(OK, surface->setUsage(GRALLOC_USAGE_SW_WRITE_OFTEN));

    queueColorToSurface(surface, kRed);

    ASSERT_EQ(OK, surfaceTexture->updateTexImage());
    ASSERT_EQ(EGL_SUCCESS, eglGetError());

    EXPECT_TRUE(checkAllPixelsMatch(textureId, kRed));
}

TEST_F(SurfaceTextureTest, DequeueBuffer) {
    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);

    sp<SurfaceTextureType> surfaceTexture = createSurfaceTexture(textureId);

    ASSERT_EQ(OK, surfaceTexture->setDefaultBufferSize(kTextureSize.width, kTextureSize.height));

    sp<Surface> surface = surfaceTexture->getSurface();
    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));
    ASSERT_EQ(OK, surface->setUsage(GRALLOC_USAGE_SW_WRITE_OFTEN));

    sp<GraphicBuffer> queuedBuffer;
    queueColorToSurface(surface, kRed, &queuedBuffer);

    EXPECT_EQ(nullptr, dequeueFromSurfaceTextureConsumer(surfaceTexture));

    surfaceTexture->takeConsumerOwnership();
    EXPECT_EQ(nullptr, dequeueFromSurfaceTextureConsumer(surfaceTexture))
            << "Take consumer ownership should have failed, making this call fail.";

    EXPECT_EQ(OK, surfaceTexture->detachFromContext());
    surfaceTexture->takeConsumerOwnership();

    sp<GraphicBuffer> consumerBuffer = dequeueFromSurfaceTextureConsumer(surfaceTexture);
    EXPECT_EQ(queuedBuffer, consumerBuffer);
}

class TestFrameAvailableListener : public SurfaceTextureListenerType {
public:
    virtual void onFrameAvailable(const BufferItem&) override { mFrameCount++; }
    virtual void onSetFrameRate(float /* frameRate */, int8_t /* compatibility */,
                                int8_t /* changeFrameRateStrategy */) override {}

    size_t getFrameAvailableCount() { return mFrameCount; }

private:
    size_t mFrameCount;
};

TEST_F(SurfaceTextureTest, FrameAvailableListenerCallback) {
    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);

    sp<SurfaceTextureType> surfaceTexture = createSurfaceTexture(textureId);
    sp<TestFrameAvailableListener> listener = sp<TestFrameAvailableListener>::make();
    surfaceTexture->setSurfaceTextureListener(listener);

    ASSERT_EQ(OK, surfaceTexture->setDefaultBufferSize(kTextureSize.width, kTextureSize.height));

    sp<Surface> surface = surfaceTexture->getSurface();
    sp<SurfaceListener> surfaceListener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, surfaceListener));
    ASSERT_EQ(OK, surface->setUsage(GRALLOC_USAGE_SW_WRITE_OFTEN));

    queueColorToSurface(surface, kGreen);
    ASSERT_EQ(1u, listener->getFrameAvailableCount());

    ASSERT_EQ(OK, surfaceTexture->updateTexImage());

    queueColorToSurface(surface, kBlue);
    ASSERT_EQ(2u, listener->getFrameAvailableCount());
}

TEST_F(SurfaceTextureTest, UpdateTexImageWithNoNewFrame) {
    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);

    sp<SurfaceTextureType> surfaceTexture = createSurfaceTexture(textureId);
    ASSERT_EQ(OK, surfaceTexture->setDefaultBufferSize(kTextureSize.width, kTextureSize.height));

    sp<Surface> surface = surfaceTexture->getSurface();
    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));
    ASSERT_EQ(OK, surface->setUsage(GRALLOC_USAGE_SW_WRITE_OFTEN));

    queueColorToSurface(surface, kRed);

    ASSERT_EQ(OK, surfaceTexture->updateTexImage());
    EXPECT_TRUE(checkAllPixelsMatch(textureId, kRed));

    // Calling updateTexImage again should be a no-op and return no error.
    ASSERT_EQ(NO_ERROR, surfaceTexture->updateTexImage());
    EXPECT_TRUE(checkAllPixelsMatch(textureId, kRed));
}

TEST_F(SurfaceTextureTest, SingleBufferMode) {
    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);

    sp<SurfaceTextureType> surfaceTexture = createSingleBufferSurfaceTexture(textureId);

    ASSERT_EQ(OK, surfaceTexture->setDefaultBufferSize(kTextureSize.width, kTextureSize.height));

    sp<Surface> surface = surfaceTexture->getSurface();
    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));
    ASSERT_EQ(OK, surface->setUsage(GRALLOC_USAGE_SW_WRITE_OFTEN));

    // Set the dequeue timeout so we don't block.
    ASSERT_EQ(OK, surface->setDequeueTimeout(0));

    // After queuing one buffer, we can't queue another unless it's been released.
    queueColorToSurface(surface, kRed);

    sp<GraphicBuffer> buffer;
    sp<Fence> fence;
    ASSERT_EQ(TIMED_OUT, surface->dequeueBuffer(&buffer, &fence));

    ASSERT_EQ(OK, surfaceTexture->updateTexImage());
    EXPECT_TRUE(checkAllPixelsMatch(textureId, kRed));

    EXPECT_EQ(OK, surfaceTexture->releaseTexImage());

    queueColorToSurface(surface, kGreen);
    ASSERT_EQ(OK, surfaceTexture->updateTexImage());
    EXPECT_TRUE(checkAllPixelsMatch(textureId, kGreen));
}

TEST_F(SurfaceTextureTest, DetachAndReattachToContext_OldBehavior) {
    if (surface_texture_updateteximage_stop_early()) {
        GTEST_SKIP();
        return;
    }

    GLuint textureId1;
    glGenTextures(1, &textureId1);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId1);

    sp<SurfaceTextureType> surfaceTexture = createSurfaceTexture(textureId1);
    ASSERT_EQ(OK, surfaceTexture->setDefaultBufferSize(kTextureSize.width, kTextureSize.height));
    ASSERT_EQ(OK, surfaceTexture->setMaxBufferCount(10));

    sp<Surface> surface = surfaceTexture->getSurface();
    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));
    ASSERT_EQ(OK, surface->setUsage(GRALLOC_USAGE_SW_WRITE_OFTEN));

    // Setting up the BQ to let us queue multiple buffers
    ASSERT_EQ(OK, surface->setLegacyBufferDrop(false));

    queueColorToSurface(surface, kGreen);

    // When we're attached, updating should work as expected, and the GREEN image is used.
    ASSERT_EQ(OK, surfaceTexture->updateTexImage());
    EXPECT_TRUE(checkAllPixelsMatch(textureId1, kGreen));

    ASSERT_EQ(OK, surfaceTexture->detachFromContext());

    // Without anything queued, this returns OK for some reason.
    EXPECT_EQ(OK, surfaceTexture->updateTexImage());

    queueColorToSurface(surface, kRed);
    queueColorToSurface(surface, kBlue);

    // With things queued, this will fail. But still update the image.
    EXPECT_EQ(INVALID_OPERATION, surfaceTexture->updateTexImage());

    GLuint textureId2;
    glGenTextures(1, &textureId2);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId2);

    // When we attach, the previous color should remain:
    ASSERT_EQ(OK, surfaceTexture->attachToContext(textureId2));
    EXPECT_TRUE(checkAllPixelsMatch(textureId2, kGreen));

    // updateTexImage should work again and update the new texture. But notice that the red image
    // was skipped.
    ASSERT_EQ(OK, surfaceTexture->updateTexImage());
    EXPECT_TRUE(checkAllPixelsMatch(textureId2, kBlue));
}

TEST_F(SurfaceTextureTest, DetachAndReattachToContext) {
    if (!surface_texture_updateteximage_stop_early()) {
        GTEST_SKIP();
        return;
    }

    GLuint textureId1;
    glGenTextures(1, &textureId1);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId1);

    sp<SurfaceTextureType> surfaceTexture = createSurfaceTexture(textureId1);
    ASSERT_EQ(OK, surfaceTexture->setDefaultBufferSize(kTextureSize.width, kTextureSize.height));
    ASSERT_EQ(OK, surfaceTexture->setMaxBufferCount(10));

    sp<Surface> surface = surfaceTexture->getSurface();
    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));
    ASSERT_EQ(OK, surface->setUsage(GRALLOC_USAGE_SW_WRITE_OFTEN));

    // Setting up the BQ to let us queue multiple buffers
    ASSERT_EQ(OK, surface->setLegacyBufferDrop(false));

    queueColorToSurface(surface, kGreen);
    queueColorToSurface(surface, kRed);
    queueColorToSurface(surface, kBlue);

    // When we're attached, updating should work as expected, and the GREEN image is used.
    ASSERT_EQ(OK, surfaceTexture->updateTexImage());
    EXPECT_TRUE(checkAllPixelsMatch(textureId1, kGreen));

    ASSERT_EQ(OK, surfaceTexture->detachFromContext());

    // updateTexImage should fail now that we're detached... but it will still cycle images.
    EXPECT_EQ(INVALID_OPERATION, surfaceTexture->updateTexImage());

    GLuint textureId2;
    glGenTextures(1, &textureId2);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId2);

    // When we attach, the previous color should remain:
    ASSERT_EQ(OK, surfaceTexture->attachToContext(textureId2));
    EXPECT_TRUE(checkAllPixelsMatch(textureId2, kGreen));

    // updateTexImage should work again and update the new texture
    ASSERT_EQ(OK, surfaceTexture->updateTexImage());
    EXPECT_TRUE(checkAllPixelsMatch(textureId2, kRed));
}

TEST_F(SurfaceTextureTest, MemoryManagement_GLModeClearsBuffers_WhenBufferDetached) {
    GLuint textureId1;
    glGenTextures(1, &textureId1);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId1);

    sp<SurfaceTextureType> surfaceTexture = createSurfaceTexture(textureId1);
    ASSERT_EQ(OK, surfaceTexture->setDefaultBufferSize(kTextureSize.width, kTextureSize.height));

    sp<Surface> surface = surfaceTexture->getSurface();
    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));
    ASSERT_EQ(OK, surface->setUsage(GRALLOC_USAGE_SW_WRITE_OFTEN));

    // Run a single buffer all the way through the queue.
    sp<GraphicBuffer> bufferA;
    queueColorToSurface(surface, kRed, &bufferA);

    ASSERT_EQ(OK, surfaceTexture->updateTexImage());
    ASSERT_TRUE(checkAllPixelsMatch(textureId1, kRed));

    queueColorToSurface(surface, kBlue);
    ASSERT_EQ(OK, surfaceTexture->updateTexImage());
    ASSERT_TRUE(checkAllPixelsMatch(textureId1, kBlue));

    // When the buffer has been acquired and finally released by the second updateTexImage, remove
    // it from the BQ and make sure it's not referenced anywhere else.
    sp<GraphicBuffer> bufferToDelete;
    sp<Fence> fence;
    ASSERT_EQ(OK, surface->dequeueBuffer(&bufferToDelete, &fence));

    ASSERT_EQ(bufferA, bufferToDelete)
            << "Unexpected buffer from queue. Expected " << bufferA->getId() << " but got "
            << bufferToDelete->getId();

    wp<GraphicBuffer> weakBufferToDelete = bufferToDelete;
    bufferToDelete = bufferA = nullptr;

    ASSERT_NE(nullptr, weakBufferToDelete.promote())
            << "The ST should hold a reference to buffers it's seen if they're still in the queue.";

    ASSERT_EQ(OK, surface->detachBuffer(weakBufferToDelete.promote()));

    ASSERT_EQ(nullptr, weakBufferToDelete.promote())
            << "When the buffer is removed from the queue, it should be removed from the ST.";
}

TEST_F(SurfaceTextureTest, MemoryManagement_ConsumerModeClearsBuffers_WhenBufferDetached) {
    GLuint textureId1;
    glGenTextures(1, &textureId1);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId1);

    sp<SurfaceTextureType> surfaceTexture = createSurfaceTexture(textureId1);
    ASSERT_EQ(OK, surfaceTexture->setDefaultBufferSize(kTextureSize.width, kTextureSize.height));

    sp<Surface> surface = surfaceTexture->getSurface();
    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));
    ASSERT_EQ(OK, surface->setUsage(GRALLOC_USAGE_SW_WRITE_OFTEN));

    ASSERT_EQ(OK, surfaceTexture->detachFromContext());
    surfaceTexture->takeConsumerOwnership();

    // Run a single buffer all the way through the queue.
    sp<GraphicBuffer> bufferA;
    queueColorToSurface(surface, kRed, &bufferA);

    sp<GraphicBuffer> dequeuedBuffer = dequeueFromSurfaceTextureConsumer(surfaceTexture);
    ASSERT_EQ(bufferA, dequeuedBuffer);

    queueColorToSurface(surface, kBlue);
    dequeueFromSurfaceTextureConsumer(surfaceTexture);

    // When the buffer has been acquired and finally released by the second updateTexImage, remove
    // it from the BQ and make sure it's not referenced anywhere else.
    sp<GraphicBuffer> bufferToDelete;
    sp<Fence> fence;
    ASSERT_EQ(OK, surface->dequeueBuffer(&bufferToDelete, &fence));

    ASSERT_EQ(bufferA, bufferToDelete)
            << "Unexpected buffer from queue. Expected " << bufferA->getId() << " but got "
            << bufferToDelete->getId();

    wp<GraphicBuffer> weakBufferToDelete = bufferToDelete;
    bufferToDelete = dequeuedBuffer = bufferA = nullptr;

    ASSERT_NE(nullptr, weakBufferToDelete.promote())
            << "The ST should hold a reference to buffers it's seen if they're still in the queue.";

    ASSERT_EQ(OK, surface->detachBuffer(weakBufferToDelete.promote()));

    ASSERT_EQ(nullptr, weakBufferToDelete.promote())
            << "When the buffer is removed from the queue, it should be removed from the ST.";
}

// See b/458169755.
TEST_F(SurfaceTextureTest, Legacy_UnlimitedSlots_NotAllowed) {
    if (wb_surfacetexture()) {
        GTEST_SKIP();
        return;
    }

    GLuint textureId1;
    glGenTextures(1, &textureId1);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId1);

    sp<SurfaceTextureType> surfaceTexture = createSurfaceTexture(textureId1);

    sp<Surface> surface = surfaceTexture->getSurface();
    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));

    EXPECT_NE(OK, surface->setMaxDequeuedBufferCount(BufferQueueDefs::NUM_BUFFER_SLOTS * 2));
}

TEST_F(SurfaceTextureTest, UnlimitedSlots_Allowed) {
    constexpr size_t kManyDequeuedBuffers = BufferQueueDefs::NUM_BUFFER_SLOTS * 2;
    if (!wb_surfacetexture()) {
        GTEST_SKIP();
        return;
    }

    GLuint textureId1;
    glGenTextures(1, &textureId1);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId1);

    sp<SurfaceTextureType> surfaceTexture = createSurfaceTexture(textureId1);

    sp<Surface> surface = surfaceTexture->getSurface();
    sp<SurfaceListener> listener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, listener));

    ASSERT_EQ(OK, surface->setMaxDequeuedBufferCount(kManyDequeuedBuffers));

    std::vector<Surface::BatchBuffer> buffers(kManyDequeuedBuffers);
    ASSERT_EQ(OK, surface->dequeueBuffers(&buffers));

    for (size_t i = 0; i < kManyDequeuedBuffers; i++) {
        auto& buffer = buffers[i];
        sp<GraphicBuffer> graphicBuffer = static_cast<GraphicBuffer*>(buffer.buffer);
        EXPECT_EQ(OK, surface->queueBuffer(graphicBuffer, sp<Fence>::make(buffer.fenceFd)))
                << "Unable to queue buffer " << graphicBuffer->getId() << " #" << i;
    }
}

TEST_F(SurfaceTextureTest, MultiThreaded_UpdateTexImage_vs_SwapBuffers) {
    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);

    sp<SurfaceTextureType> surfaceTexture = createSurfaceTexture(textureId);
    ASSERT_EQ(OK, surfaceTexture->setDefaultBufferSize(kTextureSize.width, kTextureSize.height));

    sp<Surface> surface = surfaceTexture->getSurface();

    std::promise<bool> setupSucceededPromise;
    std::atomic<bool> stop{false};
    std::thread producerThread([&]() {
        EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(display, nullptr, nullptr);

        EGLConfig config = mEglConfig;
        EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        EGLContext producerContext =
                eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);

        EGLSurface producerSurface =
                eglCreateWindowSurface(display, config, surface.get(), nullptr);

        if (producerSurface == EGL_NO_SURFACE || producerContext == EGL_NO_CONTEXT) {
            setupSucceededPromise.set_value(false);
            return;
        }
        setupSucceededPromise.set_value(true);

        eglMakeCurrent(display, producerSurface, producerSurface, producerContext);

        while (!stop) {
            glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            eglSwapBuffers(display, producerSurface);
        }

        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(display, producerSurface);
        eglDestroyContext(display, producerContext);
    });

    ASSERT_TRUE(setupSucceededPromise.get_future().get());

    // Try to update texture while producer is spamming frames.
    // If deadlock exists, updateTexImage (holding A) will block on Driver (Lock B),
    // and Producer (holding B) will block on onFrameAvailable (Lock A).

    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
        surfaceTexture->updateTexImage();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    stop = true;
    if (producerThread.joinable()) {
        producerThread.join();
    }
}
} // namespace android
