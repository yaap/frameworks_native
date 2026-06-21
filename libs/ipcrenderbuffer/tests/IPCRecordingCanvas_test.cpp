/*
 * Copyright 2025 The Android Open Source Project
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"

#include <gui/RenderCommandBuffer.h>
#include <gui/RenderCommandBufferConsumer.h>
#include <gui/RenderCommandBufferProducer.h>

#include <android/ipcrenderbuffer/IPCRecordingCanvas.h>
#include <android/ipcrenderbuffer/RenderBufferHelpers.h>

#include <gtest/gtest.h>

#include <android-base/file.h>
#include <android/bitmap.h>
#include <android/data_space.h>
#include <filesystem>
#include <fstream>

#include <SkCanvas.h>
#include <SkDashPathEffect.h>
#include <SkData.h>
#include <SkEncodedImageFormat.h>
#include <SkFont.h>
#include <SkFontScanner.h>
#include <SkFontScanner_FreeType.h>
#include <SkImage.h>
#include <SkPath.h>
#include <SkPathBuilder.h>
#include <SkPngEncoder.h>
#include <SkRuntimeEffect.h>
#include <SkStream.h>
#include <SkString.h>
#include <SkSurface.h>
#include <SkTextBlob.h>
#include <SkTypeface.h>

#include "src/ports/SkFontMgr_custom.h"

namespace android {

#define CANVAS_TEST_SIZE 512

static const std::string kScreenshotPath("/data/local/tmp/libipcrenderbuffer_test_screenshots/");

static void writePng(const std::filesystem::path& path, const void* pixels, uint32_t width,
                     uint32_t height, uint32_t stride) {
    AndroidBitmapInfo info{
            .width = width,
            .height = height,
            .stride = stride,
            .format = ANDROID_BITMAP_FORMAT_RGBA_8888,
            .flags = ANDROID_BITMAP_FLAGS_ALPHA_OPAQUE,
    };

    std::ofstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open());

    auto writeFunc = [](void* filePtr, const void* data, size_t size) -> bool {
        auto file = reinterpret_cast<std::ofstream*>(filePtr);
        file->write(reinterpret_cast<const char*>(data), size);
        return file->good();
    };

    int compressResult = AndroidBitmap_compress(&info, ADATASPACE_SRGB, pixels,
                                                ANDROID_BITMAP_COMPRESS_FORMAT_PNG,
                                                /*(ignored) quality=*/100, &file, writeFunc);
    ASSERT_EQ(compressResult, ANDROID_BITMAP_RESULT_SUCCESS);
    file.close();
}

struct SoftwareSurfaceAndCanvas {
    SoftwareSurfaceAndCanvas(int width, int height) {
        SkImageInfo info = SkImageInfo::MakeN32Premul(width, height); // Example size
        const size_t minRowBytes = info.minRowBytes();
        const size_t size = info.computeMinByteSize();
        pixels = new SkPMColor[size];

        // Create SkSurface using SkSurfaces::WrapPixels
        surface = SkSurfaces::WrapPixels(info, pixels, minRowBytes);
        if (!surface) {
            ALOGE("Failed to create SkSurface with SkSurfaces::WrapPixels");
            delete[] pixels;
            canvas = nullptr;
            surface = nullptr;
            return;
        }
        canvas = surface->getCanvas(); // Get Canvas from the Surface
    }
    ~SoftwareSurfaceAndCanvas() { delete[] pixels; }
    sk_sp<SkSurface> surface;
    SkCanvas* canvas;
    SkPMColor* pixels;
};

bool compareData(const sk_sp<SkData>& a, const sk_sp<SkData>& b) {
    if (a->size() != b->size()) {
        return false;
    }
    return (memcmp(a->data(), b->data(), a->size()) == 0);
}

sk_sp<SkData> surfaceToPNGData(const sk_sp<SkSurface>& surface) {
    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) {
        ALOGE("Failed to create image snapshot");
        return nullptr;
    }

    SkPngEncoder::Options options;
    return SkPngEncoder::Encode(nullptr, image.get(), options);
}

bool compareSurfaces(const sk_sp<SkSurface>& a, const sk_sp<SkSurface>& b, const char* testName) {
    auto da = surfaceToPNGData(a);
    auto db = surfaceToPNGData(b);

    SkPixmap pixmapA;
    a->peekPixels(&pixmapA);
    writePng(kScreenshotPath + testName + "_direct.png", pixmapA.addr(), a->width(), a->height(),
             pixmapA.rowBytes());

    SkPixmap pixmapB;
    b->peekPixels(&pixmapB);
    writePng(kScreenshotPath + testName + "_ipc.png", pixmapB.addr(), b->width(), b->height(),
             pixmapB.rowBytes());

    return compareData(da, db);
}

class IPCRecordingCanvasTest : public ::testing::Test {
public:
    IPCRecordingCanvasTest()
          : mDirectCanvas(CANVAS_TEST_SIZE, CANVAS_TEST_SIZE),
            mIPCCanvasBackend(CANVAS_TEST_SIZE, CANVAS_TEST_SIZE),
            mIPCRecordingCanvas(mClientCache) {}

protected:
    void SetUp() override {
        Parcel p;

        mIPCRecordingCanvas.getRenderCommandBufferProducer()->writeToParcel(&p);
        p.setDataPosition(0);
        RenderCommandBufferConsumer::readFromParcel(p, &mRenderCommandBufferConsumer);
    }
    void TearDown() override {}

public:
    void renderWithIPCCanvas(const std::function<void(SkCanvas*)>& doDraw) {
        mIPCRecordingCanvas.startRecording();
        doDraw(&mIPCRecordingCanvas);
        mIPCRecordingCanvas.endRecording();
        renderCommandBufferToCanvas(&mServerCache, mRenderCommandBufferConsumer.getCurrentBuffer(),
                                    mIPCCanvasBackend.canvas, [&](int) {});
    }

    bool compareRendering(const std::function<void(SkCanvas*)>& doDraw, const char* testName) {
        doDraw(mDirectCanvas.canvas);
        renderWithIPCCanvas(doDraw);
        return compareSurfaces(mDirectCanvas.surface, mIPCCanvasBackend.surface, testName);
    }

    SoftwareSurfaceAndCanvas mDirectCanvas;
    SoftwareSurfaceAndCanvas mIPCCanvasBackend;
    IPCRecordingCanvas mIPCRecordingCanvas;
    RenderCommandBufferConsumer mRenderCommandBufferConsumer;
    IPCClientResourceCache mClientCache;
    IPCServerResourceCache mServerCache;
};

TEST_F(IPCRecordingCanvasTest, ClearToRed) {
    auto clearToRed = [&](SkCanvas* c) { c->clear(SK_ColorRED); };
    ASSERT_TRUE(compareRendering(clearToRed, "ClearToRed"));
}

TEST_F(IPCRecordingCanvasTest, DrawRect) {
    auto drawRect = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorBLUE);
        c->drawRect(SkRect::MakeWH(100, 100), paint);
    };
    ASSERT_TRUE(compareRendering(drawRect, "DrawRect"));
}

TEST_F(IPCRecordingCanvasTest, DrawOval) {
    auto drawOval = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorGREEN);
        c->drawOval(SkRect::MakeWH(100, 150), paint);
    };
    ASSERT_TRUE(compareRendering(drawOval, "DrawOval"));
}

TEST_F(IPCRecordingCanvasTest, Translate) {
    auto translateAndDraw = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorYELLOW);
        c->save();
        c->translate(50, 50);
        c->drawRect(SkRect::MakeWH(100, 100), paint);
        c->restore();
    };
    ASSERT_TRUE(compareRendering(translateAndDraw, "Translate"));
}

TEST_F(IPCRecordingCanvasTest, Scale) {
    auto scaleAndDraw = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorMAGENTA);
        c->save();
        c->scale(2.0f, 0.5f);
        c->drawRect(SkRect::MakeWH(100, 100), paint);
        c->restore();
    };
    ASSERT_TRUE(compareRendering(scaleAndDraw, "Scale"));
}

TEST_F(IPCRecordingCanvasTest, ClipRect) {
    auto clipAndDraw = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorCYAN);
        c->save();
        c->clipRect(SkRect::MakeLTRB(20, 20, 80, 80));
        c->drawRect(SkRect::MakeWH(100, 100), paint);
        c->restore();
    };
    ASSERT_TRUE(compareRendering(clipAndDraw, "ClipRect"));
}

TEST_F(IPCRecordingCanvasTest, DrawPaint) {
    auto drawPaint = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorBLACK);
        c->drawPaint(paint);
    };
    ASSERT_TRUE(compareRendering(drawPaint, "DrawPaint"));
}

TEST_F(IPCRecordingCanvasTest, DrawTextBlob) {
    std::string fontPath = android::base::GetExecutableDirectory() + "/testdata/Roboto-Regular.ttf";

    auto fontData = SkStreamAsset::MakeFromFile(fontPath.c_str());

    auto typeface = SkTypeface_FreeType::MakeFromStream(std::move(fontData), SkFontArguments());

    auto drawTextBlob = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorDKGRAY);
        SkFont font;
        font.setSize(64);

        font.setTypeface(typeface);
        auto blob = SkTextBlob::MakeFromString("Skia", font);
        c->drawTextBlob(blob, 50, 100, paint);
    };
    ASSERT_TRUE(compareRendering(drawTextBlob, "DrawTextBlob"));
}

TEST_F(IPCRecordingCanvasTest, DrawPath) {
    auto drawPath = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorRED);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(10);
        const SkPath path = SkPathBuilder()
            .moveTo(10, 10)
            .lineTo(100, 100)
            .quadTo(150, 10, 200, 100)
            .detach();
        c->drawPath(path, paint);
    };
    ASSERT_TRUE(compareRendering(drawPath, "DrawPath"));
}

TEST_F(IPCRecordingCanvasTest, DrawRegion) {
    auto drawRegion = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorBLUE);
        SkRegion region(SkIRect::MakeLTRB(20, 20, 80, 80));
        region.op(SkIRect::MakeLTRB(50, 50, 120, 120), SkRegion::kXOR_Op);
        c->drawRegion(region, paint);
    };
    ASSERT_TRUE(compareRendering(drawRegion, "DrawRegion"));
}

TEST_F(IPCRecordingCanvasTest, DrawArc) {
    auto drawArc = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorGREEN);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(5);
        c->drawArc(SkRect::MakeLTRB(10, 10, 150, 150), 45, 270, true, paint);
    };
    ASSERT_TRUE(compareRendering(drawArc, "DrawArc"));
}

TEST_F(IPCRecordingCanvasTest, DrawRRect) {
    auto drawRRect = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorMAGENTA);
        SkRRect rrect = SkRRect::MakeRectXY(SkRect::MakeLTRB(20, 20, 180, 120), 20, 20);
        c->drawRRect(rrect, paint);
    };
    ASSERT_TRUE(compareRendering(drawRRect, "DrawRRect"));
}

TEST_F(IPCRecordingCanvasTest, DrawPoints) {
    auto drawPoints = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorCYAN);
        paint.setStrokeWidth(10);
        paint.setStrokeCap(SkPaint::kRound_Cap);
        SkPoint points[] = {{20, 20}, {100, 20}, {100, 100}, {20, 100}};
        c->drawPoints(SkCanvas::kPolygon_PointMode, points, paint);
    };
    ASSERT_TRUE(compareRendering(drawPoints, "DrawPoints"));
}

TEST_F(IPCRecordingCanvasTest, DrawPatch) {
    auto drawPatch = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorYELLOW);
        const SkPoint cubics[12] = {
                {10, 10},  {60, 10},  {110, 10},  {10, 60},  {60, 60},  {110, 60},
                {10, 110}, {60, 110}, {110, 110}, {10, 160}, {60, 160}, {110, 160},
        };
        const SkColor colors[4] = {SK_ColorRED, SK_ColorGREEN, SK_ColorBLUE, SK_ColorBLACK};
        c->drawPatch(cubics, colors, nullptr, SkBlendMode::kModulate, paint);
    };
    ASSERT_TRUE(compareRendering(drawPatch, "DrawPatch"));
}

TEST_F(IPCRecordingCanvasTest, StrokeMiter) {
    auto drawStrokeMiter = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorRED);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(20);
        paint.setStrokeMiter(1.0f);
        const SkPath path = SkPathBuilder()
            .moveTo(50, 200)
            .lineTo(150, 50)
            .lineTo(250, 200)
            .detach();
        c->drawPath(path, paint);
    };
    ASSERT_TRUE(compareRendering(drawStrokeMiter, "StrokeMiter"));
}

TEST_F(IPCRecordingCanvasTest, StrokeCap) {
    auto drawStrokeCap = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorBLUE);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(20);

        paint.setStrokeCap(SkPaint::kButt_Cap);
        c->drawLine(50, 50, 200, 50, paint);

        paint.setStrokeCap(SkPaint::kRound_Cap);
        c->drawLine(50, 100, 200, 100, paint);

        paint.setStrokeCap(SkPaint::kSquare_Cap);
        c->drawLine(50, 150, 200, 150, paint);
    };
    ASSERT_TRUE(compareRendering(drawStrokeCap, "StrokeCap"));
}

TEST_F(IPCRecordingCanvasTest, StrokeJoin) {
    auto drawStrokeJoin = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorGREEN);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(20);
        SkPathBuilder path;
        path.moveTo(50, 250);
        path.lineTo(100, 200);
        path.lineTo(150, 250);

        paint.setStrokeJoin(SkPaint::kMiter_Join);
        c->drawPath(path.snapshot(), paint);

        path.offset(150, 0);
        paint.setStrokeJoin(SkPaint::kRound_Join);
        c->drawPath(path.snapshot(), paint);

        path.offset(150, 0);
        paint.setStrokeJoin(SkPaint::kBevel_Join);
        c->drawPath(path.snapshot(), paint);
    };
    ASSERT_TRUE(compareRendering(drawStrokeJoin, "StrokeJoin"));
}

TEST_F(IPCRecordingCanvasTest, AntiAlias) {
    auto drawAntiAlias = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorMAGENTA);

        paint.setAntiAlias(false);
        c->drawCircle(100, 100, 50, paint);

        paint.setAntiAlias(true);
        c->drawCircle(250, 100, 50, paint);
    };
    ASSERT_TRUE(compareRendering(drawAntiAlias, "AntiAlias"));
}

#if 0
TEST_F(IPCRecordingCanvasTest, DrawVertices) {
    auto drawVertices = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorCYAN);
        const SkPoint positions[] = {{50, 50}, {150, 50}, {150, 150}, {50, 150}, {100, 100}};
        const SkColor colors[] = {SK_ColorRED, SK_ColorGREEN, SK_ColorBLUE, SK_ColorYELLOW,
                                  SK_ColorMAGENTA};
        auto vertices = SkVertices::MakeCopy(SkVertices::kTriangleFan_VertexMode, 5, positions,
                                             nullptr, colors);
        c->drawVertices(vertices, SkBlendMode::kDst, paint);
    };
    ASSERT_TRUE(compareRendering(drawVertices, "DrawVertices"));
}
#endif

TEST_F(IPCRecordingCanvasTest, RenderTarget) {
    const uint32_t width = 100;
    const uint32_t height = 100;

    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));

    // Register in Server Cache
    uint64_t bufferId = 1234;
    mServerCache.bitmaps[bufferId] = IPCServerBitmap{nullptr, nullptr, surface};

    // Register in Client Cache
    uint32_t imageId = 5678;
    mClientCache.bitmaps[imageId] = IPCClientBitmap{bufferId};

    // Record
    mIPCRecordingCanvas.startRecording();
    mIPCRecordingCanvas.beginRenderTarget(bufferId);
    mIPCRecordingCanvas.drawColor(SK_ColorRED);
    mIPCRecordingCanvas.endRenderTarget();
    mIPCRecordingCanvas.endRecording();

    // Replay
    renderCommandBufferToCanvas(&mServerCache, mRenderCommandBufferConsumer.getCurrentBuffer(),
                                mIPCCanvasBackend.canvas, [&](int) {});

    // Verify
    sk_sp<SkSurface> expectedSurface =
            SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
    expectedSurface->getCanvas()->clear(SK_ColorRED);

    ASSERT_TRUE(compareSurfaces(expectedSurface, surface, "RenderTarget"));
}

TEST_F(IPCRecordingCanvasTest, DashedLine) {
    auto drawDashedLine = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setColor(SK_ColorRED);
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(10);
        float intervals[] = {10.0f, 20.0f};
        paint.setPathEffect(SkDashPathEffect::Make(intervals, 0));
        c->drawLine(50, 50, 450, 450, paint);
    };
    ASSERT_TRUE(compareRendering(drawDashedLine, "DashedLine"));
}

TEST_F(IPCRecordingCanvasTest, SkSLShader) {
    auto drawSkSL = [&](SkCanvas* c) {
        auto [effect, error] = SkRuntimeEffect::MakeForShader(SkString(R"(
            vec4 main(vec2 p) {
                return vec4(1, 0, 0, 1);
            }
        )"));
        ASSERT_TRUE(effect != nullptr);
        SkPaint paint;
        paint.setShader(effect->makeShader(nullptr, nullptr, 0, nullptr));
        c->drawRect(SkRect::MakeWH(100, 100), paint);
    };
    ASSERT_TRUE(compareRendering(drawSkSL, "SkSLShader"));
}

TEST_F(IPCRecordingCanvasTest, ImageShader) {
    const uint32_t width = 100;
    const uint32_t height = 100;
    sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
    surface->getCanvas()->clear(SK_ColorGREEN);
    sk_sp<SkImage> image = surface->makeImageSnapshot();

    // Register in Server Cache
    uint64_t bufferId = 999;
    mServerCache.bitmaps[bufferId] = IPCServerBitmap{nullptr, image, nullptr};

    // Register in Client Cache
    uint32_t imageId = image->uniqueID();
    mClientCache.bitmaps[imageId] = IPCClientBitmap{bufferId};

    auto drawImageShader = [&](SkCanvas* c) {
        SkPaint paint;
        paint.setShader(
                image->makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat, SkSamplingOptions()));
        c->drawRect(SkRect::MakeWH(200, 200), paint);
    };
    ASSERT_TRUE(compareRendering(drawImageShader, "ImageShader"));
}

TEST_F(IPCRecordingCanvasTest, ShadowLayer) {
    auto drawShadowLayer = [&](SkCanvas* c) {
        SkPath path = SkPathBuilder().addRect(SkRect::MakeXYWH(50, 50, 100, 100)).detach();
        SkDrawShadowRec rec;
        rec.fZPlaneParams = SkPoint3::Make(0, 0, 10);
        rec.fLightPos = SkPoint3::Make(100, 100, 100);
        rec.fLightRadius = 50;
        rec.fAmbientColor = SK_ColorRED;
        rec.fSpotColor = SK_ColorBLUE;
        rec.fFlags = 0;
        c->private_draw_shadow_rec(path, rec);
    };
    ASSERT_TRUE(compareRendering(drawShadowLayer, "ShadowLayer"));
}

TEST_F(IPCRecordingCanvasTest, SaveLayer) {
    auto drawSaveLayer = [&](SkCanvas* c) {
        SkRect bounds = SkRect::MakeXYWH(50, 50, 200, 200);
        SkPaint paint;
        paint.setAlpha(128);
        c->saveLayer(bounds, &paint);
        c->drawColor(SK_ColorRED);
        SkPaint bluePaint;
        bluePaint.setColor(SK_ColorBLUE);
        c->drawRect(SkRect::MakeXYWH(100, 100, 100, 100), bluePaint);
        c->restore();
    };
    ASSERT_TRUE(compareRendering(drawSaveLayer, "SaveLayer"));
}

} // namespace android
