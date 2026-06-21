/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <android/hardware_buffer.h>
#include <binder/Binder.h>
#include <common/FlagManager.h>
#include <gtest/gtest.h>
#include <gui/BufferItemConsumer.h>
#include <gui/IConsumerListener.h>
#include <gui/Surface.h>
#include <gui/SurfaceComposerClient.h>
#include <system/window.h>
#include <ui/GraphicBuffer.h>
#include <ui/LayerStack.h>
#include <ui/PixelFormat.h>
#include <ui/Size.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace android {
namespace {

using namespace std::chrono_literals;

constexpr int32_t kWidth = 100;
constexpr int32_t kHeight = 400;

class VirtualDisplayTest : public ::testing::Test {
protected:
    void SetUp() override {
        mConsumer = sp<BufferItemConsumer>::make(AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN);
        mConsumer->setName(String8("Virtual disp consumer (VirtualDisplayTest)"));
        mConsumer->setDefaultBufferSize(kWidth, kHeight);
        mProducer = mConsumer->getSurface()->getIGraphicBufferProducer();
    }

    sp<IGraphicBufferProducer> mProducer;
    sp<BufferItemConsumer> mConsumer;
};

TEST_F(VirtualDisplayTest, ResizeTransactions_AffectReturnedBuffers) {
    if (!FlagManager::getInstance().bugfix_resize_virtual_display_surfaces()) {
        GTEST_SKIP();
    }

    // This coordinates binder calls of onFrameAvailable with the test itself.
    class SizeWatchingFrameListener : public BufferItemConsumer::FrameAvailableListener {
    public:
        SizeWatchingFrameListener(const sp<BufferItemConsumer>& consumer) : mConsumer(consumer) {}

        virtual void onFrameAvailable(const BufferItem&) override {
            sp<BufferItemConsumer> consumer = mConsumer.promote();
            if (!consumer) {
                return;
            }

            std::lock_guard<std::mutex> lock(mMutex);
            BufferItem item;
            ASSERT_EQ(OK, consumer->acquireBuffer(&item, 0));
            mTrackedSizes.push({item.mGraphicBuffer->getWidth(), item.mGraphicBuffer->getHeight()});
            consumer->releaseBuffer(item);

            mCondition.notify_all();
        }

        ui::Size getNextSize() {
            std::unique_lock<std::mutex> lock(mMutex);

            if (mTrackedSizes.empty()) {
                mCondition.wait(lock);
            }
            ui::Size size = mTrackedSizes.front();
            mTrackedSizes.pop();
            return size;
        }

    private:
        std::mutex mMutex;
        std::condition_variable mCondition;
        std::queue<ui::Size> mTrackedSizes;
        wp<BufferItemConsumer> mConsumer;
    };

    // Set up the virtual display itself to render to mProducer
    auto sizeListener = sp<SizeWatchingFrameListener>::make(mConsumer);
    mConsumer->setFrameAvailableListener(sizeListener);

    static const std::string kDisplayName("VirtualDisplay");
    sp<IBinder> virtualDisplay =
            SurfaceComposerClient::createVirtualDisplay(kDisplayName, /*isSecure=*/false,
                                                        /*optimizeForPower=*/false,
                                                        /*uniqueId=*/"",
                                                        /*requestedRefreshRate=*/60);
    ASSERT_NE(nullptr, virtualDisplay);

    // Set up a surface that we'll use to write to the VD. If this surface doesn't get updated,
    // there won't be any new frames:
    sp<SurfaceControl> sc =
            SurfaceComposerClient::getDefault()->createSurface(String8("VDSurface"), 10, 10,
                                                               PIXEL_FORMAT_RGBA_8888, 0);
    ASSERT_NE(nullptr, sc);

    ui::LayerStack layerStack = {123454321};
    SurfaceComposerClient::Transaction t;
    t.setDisplaySurface(virtualDisplay, mProducer);
    t.setDisplayLayerStack(virtualDisplay, layerStack);
    t.setLayerStack(sc, layerStack);
    ASSERT_EQ(OK, t.apply(true));

    sp<Surface> surface = sc->getSurface();
    sp<SurfaceListener> surfaceListener = sp<StubSurfaceListener>::make();
    ASSERT_EQ(OK, surface->connect(NATIVE_WINDOW_API_CPU, surfaceListener));

    auto doFrame = [&]() {
        sp<GraphicBuffer> buffer;
        sp<Fence> fence;
        ASSERT_EQ(OK, surface->dequeueBuffer(&buffer, &fence));
        ASSERT_EQ(OK, surface->queueBuffer(buffer, fence));
    };

    // At first, the frame size should be the default we started with:
    doFrame();
    ui::Size size = sizeListener->getNextSize();
    EXPECT_EQ(kWidth, size.width);
    EXPECT_EQ(kHeight, size.height);

    // But once we update the display size, the frame size should also change:
    t.setDisplaySize(virtualDisplay, kWidth / 2, kHeight / 2);
    ASSERT_EQ(OK, t.apply(true));

    doFrame();
    size = sizeListener->getNextSize();
    EXPECT_EQ(kWidth / 2, size.width);
    EXPECT_EQ(kHeight / 2, size.height);

    // Be a good citizen and clean up:
    EXPECT_EQ(OK, SurfaceComposerClient::destroyVirtualDisplay(virtualDisplay));
}

TEST_F(VirtualDisplayTest, VirtualDisplayDestroyedSurfaceReuse) {
    static const std::string kDisplayName("VirtualDisplay");
    sp<IBinder> virtualDisplay =
            SurfaceComposerClient::createVirtualDisplay(kDisplayName, false /*isSecure*/);

    SurfaceComposerClient::Transaction t;
    t.setDisplaySurface(virtualDisplay, mProducer);
    t.apply(true);

    EXPECT_EQ(NO_ERROR, SurfaceComposerClient::destroyVirtualDisplay(virtualDisplay));
    virtualDisplay.clear();
    // Sync here to ensure the display was completely destroyed in SF
    t.apply(true);
    // add another sync since we are deferring the display destruction
    t.apply(true);

    sp<Surface> surface = sp<Surface>::make(mProducer);
    sp<ANativeWindow> window(surface);

    ASSERT_EQ(NO_ERROR, native_window_api_connect(window.get(), NATIVE_WINDOW_API_EGL));
}

} // namespace
} // namespace android