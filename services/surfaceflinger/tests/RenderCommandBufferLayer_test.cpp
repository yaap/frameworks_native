/*
 * Copyright (C) 2026 The Android Open Source Project
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

// TODO(b/129481165): remove the #pragma below and fix conversion issues
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"

#include <com_android_graphics_libgui_flags.h>

#include "LayerTransactionTest.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include <android/ipcrenderbuffer/IPCRecordingCanvas.h>
#include <android/ipcrenderbuffer/RenderBufferOps.h>
#pragma clang diagnostic pop

namespace android {

class RenderCommandBufferLayerTest : public LayerTransactionTest {
protected:
    void SetUp() override {
        LayerTransactionTest::SetUp();
    }

    // Helper to fill a BufferStateLayer
    void fillBufferStateLayerColor(const sp<SurfaceControl>& sc, Color color,
                                   uint32_t width, uint32_t height) {
        sp<GraphicBuffer> buffer = sp<GraphicBuffer>::make(width, height, PIXEL_FORMAT_RGBA_8888, 1,
                hardware::graphics::common::V1_1::BufferUsage::CPU_WRITE_OFTEN |
                hardware::graphics::common::V1_1::BufferUsage::GPU_TEXTURE, "test");
        ASSERT_NE(nullptr, buffer);
        ASSERT_NO_FATAL_FAILURE(
                TransactionUtils::fillGraphicBufferColor(buffer, Rect(width, height), color));
        Transaction().setBuffer(sc, buffer).apply();
    }

    sp<SurfaceControl> mBufferStateLayer;
    sp<SurfaceControl> mRenderCommandBufferLayer;
    const int mLayerWidth = 100;
    const int mLayerHeight = 100;
};

TEST_F(RenderCommandBufferLayerTest, InitialClearIsSkipped) {
    if (!com_android_graphics_libgui_flags_out_of_process_rendering()) {
        return;
    }

    // 1. Create layers
    ASSERT_NO_FATAL_FAILURE(mBufferStateLayer = createLayer("BufferStateLayer", mLayerWidth, mLayerHeight,
                                                              ISurfaceComposerClient::eFXSurfaceBufferState));
    ASSERT_NO_FATAL_FAILURE(mRenderCommandBufferLayer = createLayer("RenderCommandBufferLayer", 0, 0,
                                                                    ISurfaceComposerClient::eFXSurfaceBufferState));

    asTransaction([&](Transaction& t) {
        t.setLayer(mBufferStateLayer, INT32_MAX - 2);
        t.setPosition(mBufferStateLayer, 0, 0);
        t.show(mBufferStateLayer);

        t.setLayer(mRenderCommandBufferLayer, INT32_MAX - 1);
        t.setPosition(mRenderCommandBufferLayer, 0, 0);
        t.setCrop(mRenderCommandBufferLayer, Rect(mLayerWidth, mLayerHeight));
        t.show(mRenderCommandBufferLayer);
    });

    // 2. Fill Bottom with Blue
    fillBufferStateLayerColor(mBufferStateLayer, Color::BLUE, mLayerWidth, mLayerHeight);

    // 3. Top Layer: Single drawing op: clear (0,0,0,0)
    IPCClientResourceCache clientCache;
    auto canvas = IPCRecordingCanvas(clientCache);
    canvas.storeSize(mLayerWidth, mLayerHeight);
    canvas.startRecording();
    SkPaint paint;
    paint.setColor(0x00000000);
    paint.setBlendMode(SkBlendMode::kSrc);
    canvas.drawPaint(paint);
    canvas.endRecording();

    Transaction()
            .setRenderCommandBuffer(mRenderCommandBufferLayer, canvas.getRenderCommandBufferProducer())
            .setRenderCommandBufferFrameId(mRenderCommandBufferLayer, 1)
            .apply();

    // 4. Verify Blue
    {
        SCOPED_TRACE("Verify InitialClearIsSkipped");
        auto sc = screenshot();
        sc->expectColor(Rect(0, 0, mLayerWidth, mLayerHeight), Color::BLUE);
    }
}

TEST_F(RenderCommandBufferLayerTest, RenderCommandBufferAdvancesOnlyOnFrameIdUpdate) {
    if (!com_android_graphics_libgui_flags_out_of_process_rendering()) {
        return;
    }

    // 1. Create layers side by side
    ASSERT_NO_FATAL_FAILURE(mBufferStateLayer = createLayer("BufferStateLayer", mLayerWidth, mLayerHeight,
                                                              ISurfaceComposerClient::eFXSurfaceBufferState));
    ASSERT_NO_FATAL_FAILURE(mRenderCommandBufferLayer = createLayer("RenderCommandBufferLayer", 0, 0,
                                                                    ISurfaceComposerClient::eFXSurfaceBufferState));

    asTransaction([&](Transaction& t) {
        t.setLayer(mBufferStateLayer, INT32_MAX - 2);
        t.setPosition(mBufferStateLayer, 0, 0);
        t.show(mBufferStateLayer);

        t.setLayer(mRenderCommandBufferLayer, INT32_MAX - 1);
        t.setPosition(mRenderCommandBufferLayer, mLayerWidth, 0);
        t.setCrop(mRenderCommandBufferLayer, Rect(mLayerWidth, mLayerHeight));
        t.show(mRenderCommandBufferLayer);
    });

    // Initial state: both black
    {
        SCOPED_TRACE("Initial state");
        auto sc = screenshot();
        sc->expectColor(Rect(0, 0, mLayerWidth, mLayerHeight), Color::BLACK);
        sc->expectColor(Rect(mLayerWidth, 0, 2 * mLayerWidth, mLayerHeight), Color::BLACK);
    }

    // 2. Change BufferStateLayer to red and paint red in RenderCommandBuffer
    fillBufferStateLayerColor(mBufferStateLayer, Color::RED, mLayerWidth, mLayerHeight);

    IPCClientResourceCache clientCache;
    auto canvas = IPCRecordingCanvas(clientCache);
    canvas.storeSize(mLayerWidth, mLayerHeight);
    canvas.startRecording();
    canvas.drawColor(0xFFFF0000, SkBlendMode::kSrc); // Red
    canvas.endRecording();

    Transaction()
            .setRenderCommandBuffer(mRenderCommandBufferLayer, canvas.getRenderCommandBufferProducer())
            .apply();

    // 3. Verify only BufferStateLayer updated
    {
        SCOPED_TRACE("BufferStateLayer updated, RenderCommandBufferLayer not");
        auto sc = screenshot();
        sc->expectColor(Rect(0, 0, mLayerWidth, mLayerHeight), Color::RED);
        sc->expectColor(Rect(mLayerWidth, 0, 2 * mLayerWidth, mLayerHeight), Color::BLACK);
    }

    // 4. Update frame ID and verify RenderCommandBufferLayer updates
    Transaction().setRenderCommandBufferFrameId(mRenderCommandBufferLayer, 1).apply();

    {
        SCOPED_TRACE("RenderCommandBufferLayer updated");
        auto sc = screenshot();
        sc->expectColor(Rect(0, 0, mLayerWidth, mLayerHeight), Color::RED);
        sc->expectColor(Rect(mLayerWidth, 0, 2 * mLayerWidth, mLayerHeight), Color::RED);
    }
}

TEST_F(RenderCommandBufferLayerTest, RenderCommandBufferGeneratesSurfaceStats) {
    if (!com_android_graphics_libgui_flags_out_of_process_rendering()) {
        return;
    }

    ASSERT_NO_FATAL_FAILURE(mRenderCommandBufferLayer = createLayer("RenderCommandBufferLayer", 0, 0,
                                                                    ISurfaceComposerClient::eFXSurfaceBufferState));

    IPCClientResourceCache clientCache;
    auto canvas = IPCRecordingCanvas(clientCache);
    canvas.storeSize(mLayerWidth, mLayerHeight);
    canvas.startRecording();
    canvas.drawColor(0xFFFF0000, SkBlendMode::kSrc);
    canvas.endRecording();

    Transaction()
            .setRenderCommandBuffer(mRenderCommandBufferLayer, canvas.getRenderCommandBufferProducer())
            .apply();

    std::mutex mutex;
    std::condition_variable cv;
    bool surfaceStatsReceived = false;

    // Register a surface stats listener
    auto listener = [&](void* /*context*/, nsecs_t /*latchTime*/, const sp<Fence>& /*presentFence*/,
                        const SurfaceStats& /*stats*/) {
        std::lock_guard<std::mutex> lock(mutex);
        surfaceStatsReceived = true;
        cv.notify_one();
    };

    TransactionCompletedListener::getInstance()->addSurfaceStatsListener(
            this, reinterpret_cast<void*>(0x12345678), mRenderCommandBufferLayer, listener);

    // Update frame ID, this should trigger the surface stats callback
    Transaction().setRenderCommandBufferFrameId(mRenderCommandBufferLayer, 1).apply();

    // Wait for the surface stats callback
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait_for(lock, std::chrono::seconds(2), [&] { return surfaceStatsReceived; });

    EXPECT_TRUE(surfaceStatsReceived);

    TransactionCompletedListener::getInstance()->removeSurfaceStatsListener(
            this, reinterpret_cast<void*>(0x12345678));
}

} // namespace android

// TODO(b/129481165): remove the #pragma below and fix conversion issues
#pragma clang diagnostic pop // ignored "-Wconversion"
