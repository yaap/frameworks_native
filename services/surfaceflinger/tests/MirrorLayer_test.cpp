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

// TODO(b/129481165): remove the #pragma below and fix conversion issues
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"

#include <android-base/properties.h>
#include <com_android_graphics_surfaceflinger_flags.h>
#include <common/FlagManager.h>
#include <common/test/FlagUtils.h>
#include <gui/AidlUtil.h>
#include <private/android_filesystem_config.h>
#include "LayerTransactionTest.h"
#include "utils/TransactionUtils.h"

namespace android {

namespace flags = com::android::graphics::surfaceflinger::flags;

class MirrorLayerTest : public LayerTransactionTest {
protected:
    virtual void SetUp() {
        LayerTransactionTest::SetUp();
        ASSERT_EQ(NO_ERROR, mClient->initCheck());
        const auto ids = SurfaceComposerClient::getPhysicalDisplayIds();
        ASSERT_FALSE(ids.empty());

        const auto display = SurfaceComposerClient::getPhysicalDisplayToken(ids.front());
        ASSERT_FALSE(display == nullptr);

        mParentLayer = createColorLayer("Parent layer", Color::RED);
        mChildLayer = createColorLayer("Child layer", Color::GREEN, mParentLayer.get());
        asTransaction([&](Transaction& t) {
            t.setDisplayLayerStack(display, ui::DEFAULT_LAYER_STACK);
            t.setLayer(mParentLayer, INT32_MAX - 2).show(mParentLayer);
            t.setCrop(mChildLayer, Rect(0, 0, 400, 400)).show(mChildLayer);
            t.setPosition(mChildLayer, 50, 50);
            t.setFlags(mParentLayer, layer_state_t::eLayerOpaque, layer_state_t::eLayerOpaque);
            t.setFlags(mChildLayer, layer_state_t::eLayerOpaque, layer_state_t::eLayerOpaque);
        });

        mPatternLayer = createLayer("Pattern layer", 100, 100);
        mPatternBlueLayer = createColorLayer("Blue layer", Color::BLUE, mPatternLayer.get());
        mPatternGreenLayer = createColorLayer("Green layer", Color::GREEN, mPatternLayer.get());
        mPatternBlackLayer = createColorLayer("Black layer", Color::BLACK, mPatternLayer.get());
        mPatternYellowLayer = createColorLayer("Yellow layer", Color::YELLOW, mPatternLayer.get());
        asTransaction([&](Transaction& t) {

            // Now let's configure the pattern layer to look like this:
            // .----------------.
            // | BLUE  | GREEN  |
            // |-------|--------|
            // | BLACK | YELLOW |
            // .----------------.

            // These have to fit into a 100x100 layer. So each element is 50x50.
            t.setCrop(mPatternBlueLayer, Rect(0, 0, 50, 50));
            t.setCrop(mPatternGreenLayer, Rect(50, 0, 100, 50));
            t.setCrop(mPatternBlackLayer, Rect(0, 50, 50, 100));
            t.setCrop(mPatternYellowLayer, Rect(50, 50, 100, 100));
        });

        mMirrorWithCropSupported = FlagManager::getInstance().mirror_with_crop();
    }

    void expectQuadrantPattern(ScreenCapture* shot, Rect bounds) {
        shot->expectQuadrant(bounds, Color::BLUE, Color::GREEN, Color::BLACK, Color::YELLOW);
    }

    void expectColorExcluding(ScreenCapture* shot, const Color &color, const Rect& bounds,
        const Rect& exclude) {
        // .___________________.
        // | 1 |    2    |  3  |
        // |___|_________|_____|
        // | 4 | exclude |  5  |
        // |___|_________|_____|
        // | 6 |    7    |  8  |
        // |___|_________|_____|
        //
        // We need to check regions 1-8 for color but exclude the excluded region from checks.
        // We will perform 8 checks. If the rectangle to check has a zero width/height, then we
        // can skip the check.

        Rect r1 = Rect(bounds.left, bounds.top, exclude.left, exclude.top);
        Rect r2 = Rect(exclude.left, bounds.top, exclude.right, exclude.top);
        Rect r3 = Rect(exclude.right, bounds.top, bounds.right, exclude.top);

        Rect r4 = Rect(bounds.left, exclude.top, exclude.left, exclude.bottom);
        Rect r5 = Rect(exclude.right, exclude.top, bounds.right, exclude.bottom);

        Rect r6 = Rect(bounds.left, exclude.bottom, exclude.left, bounds.bottom);
        Rect r7 = Rect(exclude.left, exclude.bottom, exclude.right, bounds.bottom);
        Rect r8 = Rect(exclude.right, exclude.bottom, bounds.right, bounds.bottom);

        for (const Rect& rect : {r1, r2, r3, r4, r5, r6, r7, r8}) {
             if (rect.width() > 0 && rect.height() > 0) {
                shot->expectColor(rect, color);
             }
        }
    }

    virtual void TearDown() {
        LayerTransactionTest::TearDown();
        mParentLayer = 0;
        mChildLayer = 0;
        mPatternLayer = 0;
        mPatternBlueLayer = 0;
        mPatternGreenLayer = 0;
        mPatternBlackLayer = 0;
        mPatternYellowLayer = 0;
    }

    sp<SurfaceControl> mParentLayer;
    sp<SurfaceControl> mChildLayer;

    // Pattern layer available to use for testing. Must reparent manually.
    sp<SurfaceControl> mPatternLayer;
    // The following layers are children of mPatternLayer.
    sp<SurfaceControl> mPatternBlueLayer;
    sp<SurfaceControl> mPatternGreenLayer;
    sp<SurfaceControl> mPatternBlackLayer;
    sp<SurfaceControl> mPatternYellowLayer;
    bool mMirrorWithCropSupported;

    const Color kSemiTransparentColor = Color{100, 100, 100, 100};
};

TEST_F(MirrorLayerTest, MirrorColorLayer) {
    sp<SurfaceControl> grandchild =
            createColorLayer("Grandchild layer", Color::BLUE, mChildLayer.get());
    Transaction()
            .setFlags(grandchild, layer_state_t::eLayerOpaque, layer_state_t::eLayerOpaque)
            .setCrop(grandchild, Rect(0, 0, 200, 200))
            .show(grandchild)
            .apply();

    // Mirror mChildLayer
    sp<SurfaceControl> mirrorLayer = mClient->mirrorSurface(mChildLayer.get());
    ASSERT_NE(mirrorLayer, nullptr);

    // Add mirrorLayer as child of mParentLayer so it's shown on the display
    Transaction()
            .reparent(mirrorLayer, mParentLayer)
            .setPosition(mirrorLayer, 500, 500)
            .show(mirrorLayer)
            .apply();

    Transaction().setPosition(mirrorLayer, 550, 550).apply();

    {
        SCOPED_TRACE("Initial Mirror");
        auto shot = screenshot();
        // Grandchild mirror
        shot->expectColor(Rect(550, 550, 750, 750), Color::BLUE);
        // Child mirror
        shot->expectColor(Rect(750, 750, 950, 950), Color::GREEN);
    }

    // Set color to white on grandchild layer.
    Transaction().setColor(grandchild, half3{1, 1, 1}).apply();
    {
        SCOPED_TRACE("Updated Grandchild Layer Color");
        auto shot = screenshot();
        // Grandchild mirror
        shot->expectColor(Rect(550, 550, 750, 750), Color::WHITE);
        // Child mirror
        shot->expectColor(Rect(750, 750, 950, 950), Color::GREEN);
    }

    // Set color to black on child layer.
    Transaction().setColor(mChildLayer, half3{0, 0, 0}).apply();
    {
        SCOPED_TRACE("Updated Child Layer Color");
        auto shot = screenshot();
        // Grandchild mirror
        shot->expectColor(Rect(550, 550, 750, 750), Color::WHITE);
        // Child mirror
        shot->expectColor(Rect(750, 750, 950, 950), Color::BLACK);
    }

    // Remove grandchild layer
    Transaction().reparent(grandchild, nullptr).apply();
    {
        SCOPED_TRACE("Removed Grandchild Layer");
        auto shot = screenshot();
        // Grandchild mirror
        shot->expectColor(Rect(550, 550, 750, 750), Color::BLACK);
        // Child mirror
        shot->expectColor(Rect(750, 750, 950, 950), Color::BLACK);
    }

    if (base::GetBoolProperty("debug.sf.enable_legacy_frontend", true)) {
        GTEST_SKIP() << "Skipping test because mirroring behavior changes with legacy frontend";
    }

    // Remove child layer and verify we can still mirror the layer when
    // its offscreen.
    Transaction().reparent(mChildLayer, nullptr).apply();
    {
        SCOPED_TRACE("Removed Child Layer");
        auto shot = screenshot();
        // Grandchild mirror
        shot->expectColor(Rect(550, 550, 750, 750), Color::BLACK);
        // Child mirror
        shot->expectColor(Rect(750, 750, 950, 950), Color::BLACK);
    }

    // Add grandchild layer to offscreen layer
    Transaction().reparent(grandchild, mChildLayer).apply();
    {
        SCOPED_TRACE("Added Grandchild Layer");
        auto shot = screenshot();
        // Grandchild mirror
        shot->expectColor(Rect(550, 550, 750, 750), Color::WHITE);
        // Child mirror
        shot->expectColor(Rect(750, 750, 950, 950), Color::BLACK);
    }

    // Add child layer
    Transaction().reparent(mChildLayer, mParentLayer).apply();
    {
        SCOPED_TRACE("Added Child Layer");
        auto shot = screenshot();
        // Grandchild mirror
        shot->expectColor(Rect(550, 550, 750, 750), Color::WHITE);
        // Child mirror
        shot->expectColor(Rect(750, 750, 950, 950), Color::BLACK);
    }
}

TEST_F(MirrorLayerTest, MirrorBufferLayer) {
    sp<SurfaceControl> bufferQueueLayer =
            createLayer("BufferQueueLayer", 200, 200, 0, mChildLayer.get());
    fillBufferQueueLayerColor(bufferQueueLayer, Color::BLUE, 200, 200);
    Transaction().show(bufferQueueLayer).apply();

    sp<SurfaceControl> mirrorLayer = mClient->mirrorSurface(mChildLayer.get());
    Transaction()
            .reparent(mirrorLayer, mParentLayer)
            .setPosition(mirrorLayer, 500, 500)
            .show(mirrorLayer)
            .apply();

    Transaction().setPosition(mirrorLayer, 550, 550).apply();
    {
        SCOPED_TRACE("Initial Mirror BufferQueueLayer");
        auto shot = screenshot();
        // Buffer mirror
        shot->expectColor(Rect(550, 550, 750, 750), Color::BLUE);
        // Child mirror
        shot->expectColor(Rect(750, 750, 950, 950), Color::GREEN);
    }

    fillBufferQueueLayerColor(bufferQueueLayer, Color::WHITE, 200, 200);
    {
        SCOPED_TRACE("Update BufferQueueLayer");
        auto shot = screenshot();
        // Buffer mirror
        shot->expectColor(Rect(550, 550, 750, 750), Color::WHITE);
        // Child mirror
        shot->expectColor(Rect(750, 750, 950, 950), Color::GREEN);
    }

    Transaction().reparent(bufferQueueLayer, nullptr).apply();
    {
        SCOPED_TRACE("Removed BufferQueueLayer");
        auto shot = screenshot();
        // Buffer mirror
        shot->expectColor(Rect(550, 550, 750, 750), Color::GREEN);
        // Child mirror
        shot->expectColor(Rect(750, 750, 950, 950), Color::GREEN);
    }

    sp<SurfaceControl> layer =
            createLayer("Layer", 200, 200, ISurfaceComposerClient::eFXSurfaceBufferState,
                        mChildLayer.get());
    fillBufferLayerColor(layer, Color::BLUE, 200, 200);
    Transaction().show(layer).apply();

    {
        SCOPED_TRACE("Initial Mirror Layer");
        auto shot = screenshot();
        // Buffer mirror
        shot->expectColor(Rect(550, 550, 750, 750), Color::BLUE);
        // Child mirror
        shot->expectColor(Rect(750, 750, 950, 950), Color::GREEN);
    }

    fillBufferLayerColor(layer, Color::WHITE, 200, 200);
    {
        SCOPED_TRACE("Update Layer");
        auto shot = screenshot();
        // Buffer mirror
        shot->expectColor(Rect(550, 550, 750, 750), Color::WHITE);
        // Child mirror
        shot->expectColor(Rect(750, 750, 950, 950), Color::GREEN);
    }

    Transaction().reparent(layer, nullptr).apply();
    {
        SCOPED_TRACE("Removed Layer");
        auto shot = screenshot();
        // Buffer mirror
        shot->expectColor(Rect(550, 550, 750, 750), Color::GREEN);
        // Child mirror
        shot->expectColor(Rect(750, 750, 950, 950), Color::GREEN);
    }
}

// Test that the mirror layer is initially offscreen.
TEST_F(MirrorLayerTest, InitialMirrorState) {
    const auto ids = SurfaceComposerClient::getPhysicalDisplayIds();
    ASSERT_FALSE(ids.empty());

    const auto display = SurfaceComposerClient::getPhysicalDisplayToken(ids.front());
    ui::DisplayMode mode;
    SurfaceComposerClient::getActiveDisplayMode(display, &mode);
    const ui::Size& size = mode.resolution;

    sp<SurfaceControl> mirrorLayer = nullptr;
    {
        // Run as system to get the ACCESS_SURFACE_FLINGER permission when mirroring
        UIDFaker f(AID_SYSTEM);
        // Mirror mChildLayer
        mirrorLayer = mClient->mirrorSurface(mChildLayer.get());
        ASSERT_NE(mirrorLayer, nullptr);
    }

    // Show the mirror layer, but don't reparent to a layer on screen.
    Transaction()
            .setPosition(mirrorLayer, 500, 500)
            .show(mirrorLayer)
            .setLayer(mirrorLayer, INT32_MAX - 1)
            .apply();

    Transaction().setPosition(mirrorLayer, 550, 550).apply();
    {
        SCOPED_TRACE("Offscreen Mirror");
        auto shot = screenshot();
        shot->expectColor(Rect(0, 0, size.getWidth(), 50), Color::RED);
        shot->expectColor(Rect(0, 0, 50, size.getHeight()), Color::RED);
        shot->expectColor(Rect(450, 0, size.getWidth(), size.getHeight()), Color::RED);
        shot->expectColor(Rect(0, 450, size.getWidth(), size.getHeight()), Color::RED);
        shot->expectColor(Rect(50, 50, 450, 450), Color::GREEN);
    }

    // Add mirrorLayer as child of mParentLayer so it's shown on the display
    Transaction().reparent(mirrorLayer, mParentLayer).apply();

    {
        SCOPED_TRACE("On Screen Mirror");
        auto shot = screenshot();
        // Child mirror
        shot->expectColor(Rect(550, 550, 950, 950), Color::GREEN);
    }
}

// Test that a mirror layer can be screenshot when offscreen
TEST_F(MirrorLayerTest, OffscreenMirrorScreenshot) {
    const auto ids = SurfaceComposerClient::getPhysicalDisplayIds();
    ASSERT_FALSE(ids.empty());
    const auto display = SurfaceComposerClient::getPhysicalDisplayToken(ids.front());
    ui::DisplayMode mode;
    SurfaceComposerClient::getActiveDisplayMode(display, &mode);
    const ui::Size& size = mode.resolution;

    sp<SurfaceControl> grandchild =
            createLayer("Grandchild layer", 50, 50, ISurfaceComposerClient::eFXSurfaceBufferState,
                        mChildLayer.get());
    ASSERT_NO_FATAL_FAILURE(fillBufferLayerColor(grandchild, Color::BLUE, 50, 50));
    Rect childBounds = Rect(50, 50, 450, 450);

    asTransaction([&](Transaction& t) {
        t.setCrop(grandchild, Rect(0, 0, 50, 50)).show(grandchild);
        t.setFlags(grandchild, layer_state_t::eLayerOpaque, layer_state_t::eLayerOpaque);
    });

    sp<SurfaceControl> mirrorLayer = nullptr;
    {
        // Run as system to get the ACCESS_SURFACE_FLINGER permission when mirroring
        UIDFaker f(AID_SYSTEM);
        // Mirror mChildLayer
        mirrorLayer = mClient->mirrorSurface(mChildLayer.get());
        ASSERT_NE(mirrorLayer, nullptr);
    }

    sp<SurfaceControl> mirrorParent =
            createLayer("Grandchild layer", 50, 50, ISurfaceComposerClient::eFXSurfaceBufferState);

    // Show the mirror layer, but don't reparent to a layer on screen.
    Transaction().reparent(mirrorLayer, mirrorParent).show(mirrorLayer).apply();

    Transaction().setPosition(mirrorLayer, 50, 50).apply();

    {
        SCOPED_TRACE("Offscreen Mirror");
        auto shot = screenshot();
        shot->expectColor(Rect(0, 0, size.getWidth(), 50), Color::RED);
        shot->expectColor(Rect(0, 0, 50, size.getHeight()), Color::RED);
        shot->expectColor(Rect(450, 0, size.getWidth(), size.getHeight()), Color::RED);
        shot->expectColor(Rect(0, 450, size.getWidth(), size.getHeight()), Color::RED);
        shot->expectColor(Rect(100, 100, 450, 450), Color::GREEN);
        shot->expectColor(Rect(50, 50, 100, 100), Color::BLUE);
    }

    {
        SCOPED_TRACE("Capture Mirror");
        // Capture just the mirror layer and child.
        LayerCaptureArgs captureArgs;
        captureArgs.layerHandle = mirrorParent->getHandle();
        captureArgs.captureArgs.sourceCrop = gui::aidl_utils::toARect(childBounds);
        std::unique_ptr<ScreenCapture> shot;
        ScreenCapture::captureLayers(&shot, captureArgs);
        shot->expectSize(childBounds.width(), childBounds.height());
        shot->expectColor(Rect(0, 0, 50, 50), Color::BLUE);
        shot->expectColor(Rect(50, 50, 400, 400), Color::GREEN);
    }
}

TEST_F(MirrorLayerTest, MirrorLayerWithStopLayer) {
    sp<SurfaceControl> grandchild =
            createColorLayer("Grandchild layer", Color::BLUE, mChildLayer.get());
    Transaction()
            .setFlags(grandchild, layer_state_t::eLayerOpaque, layer_state_t::eLayerOpaque)
            .setCrop(grandchild, Rect(0, 0, 200, 200))
            .show(grandchild)
            .apply();

    // Mirror child with stop layer set to grandchild.
    sp<SurfaceControl> mirrorLayer = mClient->mirrorSurface(mChildLayer.get(), grandchild.get());
    ASSERT_NE(mirrorLayer, nullptr);

    // Add mirrorLayer as child of mParentLayer so it's shown on the display
    Transaction()
            .reparent(mirrorLayer, mParentLayer)
            .setPosition(mirrorLayer, 500, 500)
            .show(mirrorLayer)
            .apply();

    auto shot = screenshot();
    // Assert that we see the child's color and not the grandchild's color
    shot->expectColor(Rect(550, 550, 600, 600), Color::GREEN);
}

TEST_F(MirrorLayerTest, MirrorLayerWithCrop) {
    if (!mMirrorWithCropSupported) {
        GTEST_SKIP() << "Skipping test because mirror_with_crop flag is not enabled.";
    }

    sp<SurfaceControl> cropLayer = createColorLayer(
        "Bounds layer", kSemiTransparentColor, mParentLayer.get());
    Transaction()
        .setCrop(cropLayer, Rect(0, 0, 100, 100))
        .show(cropLayer)
        .apply();

    sp<SurfaceControl> mirrorLayer = mClient->mirrorSurface(
        mChildLayer.get(), nullptr, cropLayer.get());

    ASSERT_NE(mirrorLayer, nullptr);

    // Add mirrorLayer as child of mParentLayer so it's shown on the display
    Transaction()
        .reparent(mirrorLayer, mParentLayer)
        .setPosition(mirrorLayer, 500, 50)
        .show(mirrorLayer)
        .apply();

    auto mirrorRegion = Rect(500, 50, 600, 150);
    auto expandedRegion = Rect(451, 0, 650, 200); // Expanded test region to ensure offset.
    {
        SCOPED_TRACE("Mirror with crop");
        auto shot = screenshot();
        shot->expectQuadrant(mirrorRegion, Color::RED, Color::RED, Color::RED, Color::GREEN);
        expectColorExcluding(shot.get(), Color::RED, expandedRegion, mirrorRegion);
    }

    // Move child to be within the bounds
    Transaction().setPosition(mChildLayer, 0, 0).apply();
    {
        SCOPED_TRACE("Mirror with crop - child moved");
        auto shot = screenshot();
        shot->expectColor(mirrorRegion, Color::GREEN);
        expectColorExcluding(shot.get(), Color::RED, expandedRegion, mirrorRegion);
    }

    // Move bounds to lower right corner
    Transaction().setPosition(cropLayer, 350, 350).apply();
    {
        SCOPED_TRACE("Mirror with crop - crop moved");
        auto shot = screenshot();
        shot->expectQuadrant(mirrorRegion, Color::GREEN, Color::RED, Color::RED, Color::RED);
        expectColorExcluding(shot.get(), Color::RED, expandedRegion, mirrorRegion);
    }

    // Out of bounds for only x - negative side
    Transaction()
        .setPosition(mChildLayer, 50, 50)
        .setPosition(cropLayer, 0, 200)
        .apply();
    {
        SCOPED_TRACE("Mirror with crop - crop out of bounds for only x (negative side)");
        auto shot = screenshot();
        shot->expectQuadrant(mirrorRegion, Color::RED, Color::GREEN, Color::RED, Color::GREEN);
        expectColorExcluding(shot.get(), Color::RED, expandedRegion, mirrorRegion);
    }

    // Out of bounds for only x - positive side
    Transaction()
        .setPosition(mChildLayer, 50, 50)
        .setPosition(cropLayer, 400, 200)
        .apply();
    {
        SCOPED_TRACE("Mirror with crop - crop out of bounds for only x (positive side)");
        auto shot = screenshot();
        shot->expectQuadrant(mirrorRegion, Color::GREEN, Color::RED, Color::GREEN, Color::RED);
        expectColorExcluding(shot.get(), Color::RED, expandedRegion, mirrorRegion);
    }

    // Out of bounds for only y - negative side
    Transaction()
        .setPosition(mChildLayer, 50, 50)
        .setPosition(cropLayer, 200, 0)
        .apply();
    {
        SCOPED_TRACE("Mirror with crop - crop out of bounds for only y (negative side)");
        auto shot = screenshot();
        shot->expectQuadrant(mirrorRegion, Color::RED, Color::RED, Color::GREEN, Color::GREEN);
        expectColorExcluding(shot.get(), Color::RED, expandedRegion, mirrorRegion);
    }

    // Out of bounds for only y - positive side
    Transaction()
        .setPosition(mChildLayer, 50, 50)
        .setPosition(cropLayer, 200, 400)
        .apply();
    {
        SCOPED_TRACE("Mirror with crop - crop out of bounds for only y (positive side)");
        auto shot = screenshot();
        shot->expectQuadrant(mirrorRegion, Color::GREEN, Color::GREEN, Color::RED, Color::RED);
        expectColorExcluding(shot.get(), Color::RED, expandedRegion, mirrorRegion);
    }
}

TEST_F(MirrorLayerTest, MirrorLayerWithCropAndStopAt) {
    if (!mMirrorWithCropSupported) {
        GTEST_SKIP() << "Skipping test because mirror_with_crop flag is not enabled.";
    }

    sp<SurfaceControl> cropLayer = createColorLayer("Crop layer", Color{255, 255, 0, 100});
    Rect bounds(0, 0, 100, 100);
    asTransaction([&](Transaction& t) {
        t.setLayer(cropLayer, INT32_MAX - 1);
        t.setCrop(cropLayer, bounds);
        t.show(cropLayer);
    });

    sp<SurfaceControl> grandchild =
            createColorLayer("Grandchild layer", Color::BLUE, mChildLayer.get());

    asTransaction([&](Transaction& t) {
        t.setCrop(grandchild, Rect(0, 0, 50, 50));
        t.setPosition(grandchild, 10, 10);
        t.show(grandchild);
    });

    sp<SurfaceControl> greatgrandchild =
            createColorLayer("Great Grandchild layer", Color::BLACK, grandchild.get());
    asTransaction([&](Transaction& t) {
        t.setCrop(greatgrandchild, Rect(0, 0, 25, 25));
        t.setPosition(greatgrandchild, 10, 10);
        t.show(greatgrandchild);
    });

    sp<SurfaceControl> mirrorLayer = mClient->mirrorSurface(
        mChildLayer.get(), greatgrandchild.get(), cropLayer.get());
    ASSERT_NE(mirrorLayer, nullptr);

    asTransaction([&](Transaction& t) {
        t.reparent(mirrorLayer, mParentLayer);
        t.setPosition(mirrorLayer, 500, 50);
        t.show(mirrorLayer);
    });

    auto mirrorRegion = Rect(500, 50, 600, 150);
    auto expandedRegion = Rect(451, 0, 650, 200); // Expanded test region to ensure offset.

    // Crop               @ [0, 0, 100, 100]
    // Child      (green) @ [50, 50, 450, 450]
    // Grandchild (blue ) @ [60, 60, 110, 110]
    // ----------------------------------------------------
    // Crop (in mirror cs)      @ [500, 50, 600, 150]
    //
    // Child mirror (green)     @ [550, 100, 950, 500]
    //         bounded            [550, 100, 600, 150]
    //
    // Grandchild mirror (blue) @ [560, 110, 610, 160]
    //         bounded            [560, 110, 600, 150]
    {
        SCOPED_TRACE("Mirror with crop and stopAt");
        auto shot = screenshot();
        // Window is [500, 50, 600, 150]. Content mChildLayer starts at [550, 100].
        // Top and Left areas of the window should be RED (background).
        auto greenRegion = Rect(550, 100, 600, 150);
        auto blueRegion = Rect(560, 110, 600, 150);

        expectColorExcluding(shot.get(), Color::RED, mirrorRegion, greenRegion);
        expectColorExcluding(shot.get(), Color::GREEN, greenRegion, blueRegion);
        shot->expectColor(blueRegion, Color::BLUE);
        expectColorExcluding(shot.get(), Color::RED, expandedRegion, mirrorRegion);
    }

    asTransaction([&](Transaction& t) {
        t.setPosition(mChildLayer, 0, 0);
    });

    // Crop               @ [0,  0, 100, 100]
    // Child      (green) @ [0,  0, 400, 400]
    // Grandchild (blue ) @ [10, 10, 60,  60]
    // ----------------------------------------------------
    // Crop   (in mirror cs)    @ [500, 50, 600, 150]
    //
    // Child mirror (green)     @ [500, 50, 900, 450]
    //         bounded            [500, 50, 600, 150]
    //
    // Grandchild mirror (blue) @ [510, 60, 560, 110]
    //         bounded            [510, 60, 560, 110]
    {
        SCOPED_TRACE("Mirror with crop and stopAt - child moved");
        auto shot = screenshot();
        auto greenRegion = Rect(500, 50, 600, 150);
        auto blueRegion = Rect(510, 60, 560, 110);

        expectColorExcluding(shot.get(), Color::RED, mirrorRegion, greenRegion);
        expectColorExcluding(shot.get(), Color::GREEN, greenRegion, blueRegion);
        shot->expectColor(blueRegion, Color::BLUE);
        expectColorExcluding(shot.get(), Color::RED, expandedRegion, mirrorRegion);
    }

    asTransaction([&](Transaction& t) {
        t.setPosition(cropLayer, 350, 350);
    });

    // Crop                      @ [350, 350, 450, 450]
    // Child      (green)        @ [0,   400, 400, 400]
    // ----------------------------------------------------
    // Cropped Child (green)     @ [350, 350, 400, 400]
    // ----------------------------------------------------
    //
    // Mirror Layer                  @ [500, 50, 600, 150]
    // Cropped Child mirror (green)  @ [500, 50, 550, 100]
    {
        SCOPED_TRACE("Mirror with crop and stopAt - bounds moved");
        auto shot = screenshot();
        auto greenRegion = Rect(500, 50, 550, 100);

        expectColorExcluding(shot.get(), Color::RED, mirrorRegion, greenRegion);
        shot->expectColor(greenRegion, Color::GREEN);
        expectColorExcluding(shot.get(), Color::RED, expandedRegion, mirrorRegion);
    }
}

TEST_F(MirrorLayerTest, MirrorLayerWithCropAndDifferentTransforms) {
    if (!mMirrorWithCropSupported) {
        GTEST_SKIP() << "Skipping test because mirror_with_crop flag is not enabled.";
    }
    // Creates two root layers to test transformation across different root layers
    // 1. root1 contains the pattern layer that is scaled 2x onto root1.
    // 2. root2 contains cropLayer that is cropped to 200x200.

    sp<SurfaceControl> root1 = createLayer("Root1", 500, 500);

    asTransaction([&](Transaction& t) {
        t.setPosition(root1, 100, 450);
        t.setLayer(root1, INT32_MAX - 1);            // Make sure it's on top.
        t.show(root1);

        t.reparent(mPatternLayer, root1);
        t.setMatrix(mPatternLayer, 2.0f, 0, 0, 2.0f); // Scale 2x onto root1
        t.show(mPatternLayer);
    });

    sp<SurfaceControl> root2 = createLayer("Root2", 500, 500);
    sp<SurfaceControl> cropLayer =
            createColorLayer("CropLayer", kSemiTransparentColor, root2.get());

    asTransaction([&](Transaction& t) {
        t.setPosition(root2, 100, 450);
        t.setLayer(root2, INT32_MAX);
        t.show(root2);

        t.setCrop(cropLayer, Rect(0, 0, 200, 200));
        t.show(cropLayer);
    });

    // 3. Mirror root1 with boundsLayer.
    sp<SurfaceControl> mirrorLayer =
            mClient->mirrorSurface(root1.get(), nullptr, cropLayer.get());

    ASSERT_NE(mirrorLayer, nullptr);

    asTransaction([&](Transaction& t) {
        t.reparent(mirrorLayer, mParentLayer);
        t.setPosition(mirrorLayer, 500, 100);
        t.setLayer(mirrorLayer, INT32_MAX-1);
        t.show(mirrorLayer);
    });

    auto mirrorRegion = Rect(500, 100, 700, 300);
    auto expandedRegion = Rect(451, 0, 750, 350); // Expanded test region to ensure offset.
    {
        SCOPED_TRACE("Mirror with crop different transform - before move");
        auto shot = screenshot();
        expectQuadrantPattern(shot.get(), mirrorRegion);
        expectColorExcluding(shot.get(), Color::RED, expandedRegion, mirrorRegion);
    }

    // Move root2 layer to see if it triggers a change
    asTransaction([&](Transaction& t) {
        t.setPosition(root2, 150, 500);
    });

    {
        SCOPED_TRACE("Mirror with crop and different transforms");
        auto shot = screenshot();
        // Mirror layer at 500, 500.
        // It should show the Red content.
        // The size should be 25x25 (because the crop is 25x25 in local space).
        shot->expectColor(Rect(500, 100, 550, 150), Color::BLUE);
        shot->expectColor(Rect(550, 100, 650, 150), Color::GREEN);
        shot->expectColor(Rect(650, 100, 700, 150), Color::RED);
        shot->expectColor(Rect(500, 150, 550, 250), Color::BLACK);
        shot->expectColor(Rect(550, 150, 650, 250), Color::YELLOW);
        shot->expectColor(Rect(650, 150, 700, 250), Color::RED);
        shot->expectColor(Rect(500, 250, 700, 300), Color::RED);
        expectColorExcluding(shot.get(), Color::RED, expandedRegion, mirrorRegion);
    }
}

TEST_F(MirrorLayerTest, MirrorLayerWithCropUnsupported) {
    if (mMirrorWithCropSupported) {
        GTEST_SKIP() << "Skipping test because mirror_with_crop flag is enabled.";
    }

    sp<SurfaceControl> cropLayer = createColorLayer("Crop layer", kSemiTransparentColor,
         mParentLayer.get());
    Transaction()
        .setCrop(cropLayer, Rect(0, 0, 100, 100))
        .show(cropLayer)
        .apply();

    // Mirroring with bounds should fail when the feature is disabled.
    sp<SurfaceControl> mirrorLayer = mClient->mirrorSurface(
        mChildLayer.get(), nullptr, cropLayer.get());
    ASSERT_EQ(mirrorLayer, nullptr);
}

} // namespace android

// TODO(b/129481165): remove the #pragma below and fix conversion issues
#pragma clang diagnostic pop // ignored "-Wconversion"
