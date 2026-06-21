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
#ifndef ANDROID_TRANSACTION_TEST_HARNESSES
#define ANDROID_TRANSACTION_TEST_HARNESSES

#include <com_android_graphics_libgui_flags.h>
#include <gui/BufferItemConsumer.h>
#include <ui/DisplayState.h>

#include <android/ipcrenderbuffer/IPCRecordingCanvas.h>
#include <android/ipcrenderbuffer/RenderBufferOps.h>
#include <SkColor.h>
#include <SkPaint.h>
#include <SkRect.h>
#include "LayerTransactionTest.h"
#include "ui/LayerStack.h"

#include <map>
#include <memory>

namespace android {

// Not a real layer type, but used for testing render command buffer layers.
const uint32_t LAYER_TYPE_RENDER_COMMAND_BUFFER = 0x10000000;

using android::hardware::graphics::common::V1_1::BufferUsage;

class LayerRenderPathTestHarness {
public:
    LayerRenderPathTestHarness(LayerTransactionTest* delegate, RenderPath renderPath,
                               ui::Rotation virtualDisplayRotation = ui::Rotation::Rotation0)
          : mDelegate(delegate), mRenderPath(renderPath) {
        const auto ids = SurfaceComposerClient::getPhysicalDisplayIds();
        mDisplayId = ids.front();
        const auto displayToken =
                ids.empty() ? nullptr : SurfaceComposerClient::getPhysicalDisplayToken(mDisplayId);

        ui::DisplayState displayState;
        SurfaceComposerClient::getDisplayState(displayToken, &displayState);

        ui::DisplayMode displayMode;
        SurfaceComposerClient::getActiveDisplayMode(displayToken, &displayMode);
        mBufferSize = displayMode.resolution;

        if (mRenderPath == RenderPath::VIRTUAL_DISPLAY) {
            mRotation = virtualDisplayRotation;
        } else {
            mRotation = displayState.orientation;
        }
    }

    std::unique_ptr<ScreenCapture> getScreenCapture() {
        switch (mRenderPath) {
            case RenderPath::SCREENSHOT:
                return mDelegate->screenshot();
            case RenderPath::VIRTUAL_DISPLAY:
                sp<IBinder> vDisplay;

                sp<BufferItemConsumer> itemConsumer = sp<BufferItemConsumer>::make(
                        // Sample usage bits from screenrecord
                        GRALLOC_USAGE_HW_VIDEO_ENCODER | GRALLOC_USAGE_SW_READ_OFTEN);
                sp<BufferListener> listener = sp<BufferListener>::make();
                itemConsumer->setFrameAvailableListener(listener);
                itemConsumer->setName(String8("Virtual disp consumer (TransactionTest)"));
                itemConsumer->setDefaultBufferSize(mBufferSize.width, mBufferSize.height);

                static const std::string kDisplayName("VirtualDisplay");
                vDisplay = SurfaceComposerClient::createVirtualDisplay(kDisplayName,
                                                                       false /*isSecure*/);

                constexpr ui::LayerStack layerStack{
                        848472}; // ASCII for TTH (TransactionTestHarnesses)
                sp<SurfaceControl> mirrorSc =
                        SurfaceComposerClient::getDefault()->mirrorLayerStack(mDisplayId);

                SurfaceComposerClient::Transaction t;
                t.setDisplaySurface(vDisplay,
                                    itemConsumer->getSurface()->getIGraphicBufferProducer());
                t.setDisplayProjection(vDisplay, mRotation, Rect(getRotatedResolution()),
                                       Rect(getRotatedResolution()));
                t.setDisplayLayerStack(vDisplay, layerStack);
                t.setLayerStack(mirrorSc, layerStack);
                t.apply();
                SurfaceComposerClient::Transaction().apply(true);

                std::unique_lock lock(listener->mMutex);
                listener->mAvailable = false;
                // Wait for frame buffer ready.
                listener->mCondition.wait_for(lock, std::chrono::seconds(2),
                                              [listener]() NO_THREAD_SAFETY_ANALYSIS {
                                                  return listener->mAvailable;
                                              });

                BufferItem item;
                itemConsumer->acquireBuffer(&item, 0, true);
                constexpr bool kContainsHdr = false;
                auto sc = std::make_unique<ScreenCapture>(item.mGraphicBuffer, kContainsHdr);
                itemConsumer->releaseBuffer(item);

                // Possible race condition with destroying virtual displays, in which
                // CompositionEngine::present may attempt to be called on the same
                // display multiple times. The layerStack is set as unassigned here so
                // that the display is ignored if that scenario occurs.
                t.setLayerStack(mirrorSc, ui::UNASSIGNED_LAYER_STACK);
                t.apply(true);
                SurfaceComposerClient::destroyVirtualDisplay(vDisplay);
                return sc;
        }
    }

    std::string getRotationName() const { return ui::toCString(mRotation); }

    ui::Size getRotatedResolution() const {
        ui::Size size = mBufferSize;
        size.rotate(mRotation);
        return size;
    }

    android::Rect rotateRect(const android::Rect& r) const {
        ui::Transform transform;
        transform.set(ui::Transform::toRotationFlags(mRotation), mBufferSize.width,
                      mBufferSize.height);
        return transform.transform(r);
    }

protected:
    LayerTransactionTest* mDelegate;
    RenderPath mRenderPath;
    ui::Size mBufferSize;
    PhysicalDisplayId mDisplayId;
    ui::Rotation mRotation = ui::Rotation::Rotation0;

    class BufferListener : public ConsumerBase::FrameAvailableListener {
    public:
        BufferListener() = default;

        std::mutex mMutex;
        std::condition_variable mCondition;
        bool mAvailable = false;

        void onFrameAvailable(const BufferItem& /*item*/) override {
            std::unique_lock lock(mMutex);
            mAvailable = true;
            mCondition.notify_all();
        }
    };
};

class LayerTypeTransactionHarness : public LayerTransactionTest {
public:
    LayerTypeTransactionHarness(uint32_t layerType) : mLayerType(layerType) {}

    sp<SurfaceControl> createLayer(const char* name, uint32_t width, uint32_t height,
                                   uint32_t flags = 0, SurfaceControl* parent = nullptr,
                                   uint32_t* outTransformHint = nullptr,
                                   PixelFormat format = PIXEL_FORMAT_RGBA_8888) {
        // if the flags already have a layer type specified, return an error
        if (flags & ISurfaceComposerClient::eFXSurfaceMask) {
            return nullptr;
        }

        if (mLayerType == LAYER_TYPE_RENDER_COMMAND_BUFFER) {
            auto layer =
                    LayerTransactionTest::createLayer(name, 0, 0,
                                                      flags |
                                                              ISurfaceComposerClient::
                                                                      eFXSurfaceEffect,
                                                      parent, outTransformHint, format);
            if (layer) {
                auto cache = std::make_unique<IPCClientResourceCache>();
                auto canvas = std::make_shared<IPCRecordingCanvas>(*cache);
                Transaction()
                        .setRenderCommandBuffer(layer, canvas->getRenderCommandBufferProducer())
                        .apply();
                mRenderResourceCaches[layer.get()] = std::move(cache);
                mRenderCommandCanvases[layer.get()] = canvas;
                mRenderCommandFrameIds[layer.get()] = 0;
            }
            return layer;
        }
        return LayerTransactionTest::createLayer(name, width, height, flags | mLayerType, parent,
                                                 outTransformHint, format);
    }

    void fillLayerColor(const sp<SurfaceControl>& layer, const Color& color, uint32_t bufferWidth,
                        uint32_t bufferHeight) {
        if (mLayerType == LAYER_TYPE_RENDER_COMMAND_BUFFER) {
            auto it = mRenderCommandCanvases.find(layer.get());
            ASSERT_NE(it, mRenderCommandCanvases.end());
            auto canvas = it->second;
            auto& frameId = mRenderCommandFrameIds.at(layer.get());
            frameId++;
            ASSERT_NO_FATAL_FAILURE(fillRenderCommandBufferLayerColor(canvas, frameId, layer,
                                                                      color, bufferWidth,
                                                                      bufferHeight));
        } else {
            ASSERT_NO_FATAL_FAILURE(LayerTransactionTest::fillLayerColor(mLayerType, layer, color,
                                                                         bufferWidth,
                                                                         bufferHeight));
        }
    }

    void fillLayerQuadrant(const sp<SurfaceControl>& layer, uint32_t bufferWidth,
                           uint32_t bufferHeight, const Color& topLeft, const Color& topRight,
                           const Color& bottomLeft, const Color& bottomRight) {
        if (mLayerType == LAYER_TYPE_RENDER_COMMAND_BUFFER) {
            auto it = mRenderCommandCanvases.find(layer.get());
            ASSERT_NE(it, mRenderCommandCanvases.end());
            auto canvas = it->second;
            auto& frameId = mRenderCommandFrameIds.at(layer.get());
            frameId++;
            ASSERT_NO_FATAL_FAILURE(
                    fillRenderCommandBufferLayerQuadrant(canvas, frameId, layer, bufferWidth,
                                                         bufferHeight, topLeft, topRight,
                                                         bottomLeft, bottomRight));
        } else {
            ASSERT_NO_FATAL_FAILURE(
                    LayerTransactionTest::fillLayerQuadrant(mLayerType, layer, bufferWidth,
                                                            bufferHeight, topLeft, topRight,
                                                            bottomLeft, bottomRight));
        }
    }

    void fillRenderCommandBufferLayerColor(std::shared_ptr<IPCRecordingCanvas> canvas,
                                           uint64_t frameId, const sp<SurfaceControl>& layer,
                                           const Color& color, uint32_t bufferWidth,
                                           uint32_t bufferHeight) {
        if (!com_android_graphics_libgui_flags_out_of_process_rendering()) {
            return;
        }
        canvas->storeSize(bufferWidth, bufferHeight);
        canvas->startRecording();
        canvas->drawColor(SkColorSetARGB(color.a, color.r, color.g, color.b), SkBlendMode::kSrc);
        canvas->endRecording();

        Transaction()
                .setRenderCommandBufferFrameId(layer, frameId)
                .setCrop(layer, Rect(bufferWidth, bufferHeight))
                .apply(true);
    }

    void fillRenderCommandBufferLayerQuadrant(std::shared_ptr<IPCRecordingCanvas> canvas,
                                              uint64_t frameId, const sp<SurfaceControl>& layer,
                                              uint32_t bufferWidth, uint32_t bufferHeight,
                                              const Color& topLeft, const Color& topRight,
                                              const Color& bottomLeft,
                                              const Color& bottomRight) {
        if (!com_android_graphics_libgui_flags_out_of_process_rendering()) {
            return;
        }
        canvas->storeSize(bufferWidth, bufferHeight);
        canvas->startRecording();

        ASSERT_TRUE(bufferWidth % 2 == 0 && bufferHeight % 2 == 0);
        const int32_t halfW = bufferWidth / 2;
        const int32_t halfH = bufferHeight / 2;

        SkPaint paint;
        paint.setAntiAlias(false);

        paint.setColor(SkColorSetARGB(topLeft.a, topLeft.r, topLeft.g, topLeft.b));
        canvas->drawRect(SkRect::MakeLTRB(0, 0, halfW, halfH), paint);

        paint.setColor(SkColorSetARGB(topRight.a, topRight.r, topRight.g, topRight.b));
        canvas->drawRect(SkRect::MakeLTRB(halfW, 0, bufferWidth, halfH), paint);

        paint.setColor(SkColorSetARGB(bottomLeft.a, bottomLeft.r, bottomLeft.g, bottomLeft.b));
        canvas->drawRect(SkRect::MakeLTRB(0, halfH, halfW, bufferHeight), paint);

        paint.setColor(SkColorSetARGB(bottomRight.a, bottomRight.r, bottomRight.g, bottomRight.b));
        canvas->drawRect(SkRect::MakeLTRB(halfW, halfH, bufferWidth, bufferHeight), paint);

        canvas->endRecording();

        Transaction()
                .setRenderCommandBufferFrameId(layer, frameId)
                .setCrop(layer, Rect(bufferWidth, bufferHeight))
                .apply(true);
    }

protected:
    uint32_t mLayerType;
    std::map<SurfaceControl*, std::unique_ptr<IPCClientResourceCache>> mRenderResourceCaches;
    std::map<SurfaceControl*, std::shared_ptr<IPCRecordingCanvas>> mRenderCommandCanvases;
    std::map<SurfaceControl*, uint64_t> mRenderCommandFrameIds;
};
} // namespace android
#endif
