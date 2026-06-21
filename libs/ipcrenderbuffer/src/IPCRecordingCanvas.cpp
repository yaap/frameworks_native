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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"

#include <android/ipcrenderbuffer/IPCRecordingCanvas.h>
#include <android/ipcrenderbuffer/RenderBufferOps.h>
#include <gui/RenderCommandBufferProducer.h>
#include <gui/SurfaceComposerClient.h>
#include <log/log.h>
#include <utils/Trace.h>

#include <SkColorFilter.h>

#include <cstring>

#include "log/log_main.h"

// #define TRACE_IPC_CANVAS 1

#ifndef TRACE_IPC_CANVAS
#define IPC_CANVAS_TRACE_CALL (void)0
#else
#define IPC_CANVAS_TRACE_CALL ATRACE_CALL()
#endif

namespace android {

IPCRecordingCanvas::IPCRecordingCanvas(IPCClientResourceCache& resourceCache)
      : INHERITED(512, 512), mResourceCache(resourceCache) {
    mRenderCommandBufferProducer = std::make_shared<RenderCommandBufferProducer>();
}

sk_sp<SkSurface> IPCRecordingCanvas::onNewSurface(const SkImageInfo&, const SkSurfaceProps&) {
    ALOGE("onNewSurface Not implemented");
    return nullptr;
}

bool IPCRecordingCanvas::canRecord() {
    return mRenderCommandBufferProducer->canRecord();
}

void IPCRecordingCanvas::startRecording() {
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer != nullptr, "Already recording");
    mCurrentRenderCommandBuffer = mRenderCommandBufferProducer->startRecording();
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr,
                        "User should check canRecord() first");
    mCurrentRenderCommandBuffer->setFrameSize(mWidth, mHeight);
}

void IPCRecordingCanvas::endRecording() {
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    mRenderCommandBufferProducer->finishRecordingAndPostFrame();
    mCurrentRenderCommandBuffer = nullptr;
}

void IPCRecordingCanvas::willSave() {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = SaveOp::Create(mCurrentRenderCommandBuffer);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
SkCanvas::SaveLayerStrategy IPCRecordingCanvas::getSaveLayerStrategy(const SaveLayerRec& rec) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = SaveLayerOp::Create(mCurrentRenderCommandBuffer, rec.fBounds, rec.fPaint);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);

    return SkCanvas::kNoLayer_SaveLayerStrategy;
}
void IPCRecordingCanvas::willRestore() {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = RestoreOp::Create(mCurrentRenderCommandBuffer);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
bool IPCRecordingCanvas::onDoSaveBehind(const SkRect*) {
    IPC_CANVAS_TRACE_CALL;
    ALOGE("onDoSaveBehind Not implemented");
    return true; // ?
}

void IPCRecordingCanvas::didConcat44(const SkM44& m) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = ConcatOp::Create(mCurrentRenderCommandBuffer, m);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
void IPCRecordingCanvas::didSetM44(const SkM44& m) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = SetMatrixOp::Create(mCurrentRenderCommandBuffer, m);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
void IPCRecordingCanvas::didScale(SkScalar sx, SkScalar sy) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = ScaleOp::Create(mCurrentRenderCommandBuffer, sx, sy);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
void IPCRecordingCanvas::didTranslate(SkScalar tx, SkScalar ty) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = TranslateOp::Create(mCurrentRenderCommandBuffer, tx, ty);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}

void IPCRecordingCanvas::onClipRect(const SkRect& r, SkClipOp o, ClipEdgeStyle s) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    bool isAa = s == kSoft_ClipEdgeStyle;
    auto op = ClipRectOp::Create(mCurrentRenderCommandBuffer, r, o, isAa);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
void IPCRecordingCanvas::onClipRRect(const SkRRect& r, SkClipOp o, ClipEdgeStyle s) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    bool isAa = s == kSoft_ClipEdgeStyle;
    auto op = ClipRRectOp::Create(mCurrentRenderCommandBuffer, r, o, isAa);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
void IPCRecordingCanvas::onClipPath(const SkPath& p, SkClipOp o, ClipEdgeStyle s) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    bool isAa = s == kSoft_ClipEdgeStyle;
    auto op = ClipPathOp::Create(mCurrentRenderCommandBuffer, p, o, isAa);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
void IPCRecordingCanvas::onClipShader(sk_sp<SkShader> shader, SkClipOp op) {
    IPC_CANVAS_TRACE_CALL;
    auto opClipShader = ClipShaderOp::Create(mCurrentRenderCommandBuffer, shader, op);
    LOG_ALWAYS_FATAL_IF(opClipShader == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(opClipShader);
}
void IPCRecordingCanvas::onClipRegion(const SkRegion& r, SkClipOp o) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = ClipRegionOp::Create(mCurrentRenderCommandBuffer, r, o);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
void IPCRecordingCanvas::onResetClip() {
    IPC_CANVAS_TRACE_CALL;
    auto op = ResetClipOp::Create(mCurrentRenderCommandBuffer);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}

/*void IPCRecordingCanvas::onDrawProxySurfaceControl(int id) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = DrawProxySurfaceControlOp::Create(mCurrentRenderCommandBuffer, id);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
    }*/

void IPCRecordingCanvas::onDrawPaint(const SkPaint& p) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = DrawPaintOp::Create(mCurrentRenderCommandBuffer, p);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}

void IPCRecordingCanvas::onDrawBehind(const SkPaint& p) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = DrawBehindOp::Create(mCurrentRenderCommandBuffer, p);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}

void IPCRecordingCanvas::onDrawPath(const SkPath& path, const SkPaint& paint) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");

    auto op = DrawPathOp::Create(mCurrentRenderCommandBuffer, path, paint, &mResourceCache);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
void IPCRecordingCanvas::onDrawRect(const SkRect& rect, const SkPaint& paint) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = DrawRectOp::Create(mCurrentRenderCommandBuffer, rect, paint, &mResourceCache);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
void IPCRecordingCanvas::onDrawRegion(const SkRegion& region, const SkPaint& paint) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = DrawRegionOp::Create(mCurrentRenderCommandBuffer, region, paint, &mResourceCache);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
void IPCRecordingCanvas::onDrawOval(const SkRect& oval, const SkPaint& paint) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = DrawOvalOp::Create(mCurrentRenderCommandBuffer, oval, paint, &mResourceCache);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}

void IPCRecordingCanvas::onDrawArc(const SkRect& oval, SkScalar startAngle, SkScalar sweepAngle,
                                   bool useCenter, const SkPaint& paint) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = DrawArcOp::Create(mCurrentRenderCommandBuffer, oval, startAngle, sweepAngle,
                                useCenter, paint, &mResourceCache);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}

void dumpPaintToLog(const SkPaint& paint) {
    ALOGE("SkPaint Details:");
    ALOGE("  AntiAlias: %s", paint.isAntiAlias() ? "true" : "false");
    ALOGE("  Dither: %s", paint.isDither() ? "true" : "false");
    ALOGE("  Style: %u", (unsigned int)paint.getStyle());
    ALOGE("  Color: 0x%08X", paint.getColor());
    ALOGE("  Alpha: %f", paint.getAlphaf());
    ALOGE("  StrokeWidth: %f", paint.getStrokeWidth());
    ALOGE("  StrokeMiter: %f", paint.getStrokeMiter());
    ALOGE("  StrokeCap: %u", (unsigned int)paint.getStrokeCap());
    ALOGE("  StrokeJoin: %u", (unsigned int)paint.getStrokeJoin());

    if (auto shader = paint.refShader()) {
        ALOGE("  Shader: Present");
    } else {
        ALOGE("  Shader: None");
    }
    if (auto colorFilter = paint.refColorFilter()) {
        ALOGE("  ColorFilter: Present");

    } else {
        ALOGE("  ColorFilter: None");
    }

    if (auto blendMode = paint.asBlendMode()) {
        ALOGE("  BlendMode: %u", (unsigned int)blendMode.value());
    } else if (auto blender = paint.refBlender()) {
        ALOGE("  Blender: Present");
    } else {
        ALOGE("  BlendMode: None");
    }
    ALOGE("  Nothing to Draw: %s", paint.nothingToDraw() ? "true" : "false");
}

void IPCRecordingCanvas::onDrawRRect(const SkRRect& rect, const SkPaint& paint) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");

    auto op = DrawRRectOp::Create(mCurrentRenderCommandBuffer, rect, paint, &mResourceCache);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
void IPCRecordingCanvas::onDrawDRRect(const SkRRect&, const SkRRect&, const SkPaint&) {
    IPC_CANVAS_TRACE_CALL;
    ALOGE("onDrawDRRect Not implemented");
}

void IPCRecordingCanvas::onDrawDrawable(SkDrawable* drawable, const SkMatrix* matrix) {
    IPC_CANVAS_TRACE_CALL;
    if (matrix) {
        SkAutoCanvasRestore acr(this, true);
        this->concat(*matrix);
        drawable->draw(this, nullptr);
    } else {
        drawable->draw(this, nullptr);
    }
}
void IPCRecordingCanvas::onDrawPicture(const SkPicture*, const SkMatrix*, const SkPaint*) {
    IPC_CANVAS_TRACE_CALL;
    ALOGE("on DrawPicture Not implemented");
}
void IPCRecordingCanvas::onDrawAnnotation(const SkRect&, const char[], SkData*) {
    IPC_CANVAS_TRACE_CALL;
    ALOGE("onDrawAnnotation Not implemented");
}

void IPCRecordingCanvas::onDrawTextBlob(const SkTextBlob* blob, SkScalar x, SkScalar y,
                                        const SkPaint& paint) {
    IPC_CANVAS_TRACE_CALL;
    auto op =
            DrawTextBlobOp::Create(mCurrentRenderCommandBuffer, blob, x, y, paint, &mResourceCache);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}

void IPCRecordingCanvas::onDrawImage2(const SkImage* image, SkScalar x, SkScalar y,
                                      const SkSamplingOptions& sampling, const SkPaint* paint) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto it = mResourceCache.bitmaps.find(image->uniqueID());
    if (it == mResourceCache.bitmaps.end()) {
        ALOGE("Bitmap not found in cache uniqueID = %u", image->uniqueID());
        return;
    }
    auto op = DrawImageOp::Create(mCurrentRenderCommandBuffer, it->second.id, x, y, sampling, paint,
                                  &mResourceCache);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
void IPCRecordingCanvas::onDrawImageLattice2(const SkImage*, const Lattice&, const SkRect&,
                                             SkFilterMode, const SkPaint*) {
    IPC_CANVAS_TRACE_CALL;
    ALOGE("onDrawImageLattice2 Not implemented");
}

void IPCRecordingCanvas::onDrawImageRect2(const SkImage* image, const SkRect& src, const SkRect& dst,
                                          const SkSamplingOptions& sampling, const SkPaint* paint,
                                          SrcRectConstraint constraint) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto it = mResourceCache.bitmaps.find(image->uniqueID());
    if (it == mResourceCache.bitmaps.end()) {
        ALOGE("Bitmap not found in cache uniqueID = %u", image->uniqueID());
        return;
    }
    auto op = DrawImageRectOp::Create(mCurrentRenderCommandBuffer, it->second.id, src, dst,
                                      sampling, paint, constraint);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}

void IPCRecordingCanvas::storeSize(int width, int height) {
    mWidth = width;
    mHeight = height;
    this->resetCanvas(width, height);
}

void IPCRecordingCanvas::onDrawPatch(const SkPoint cubics[12], const SkColor colors[4],
                                     const SkPoint texCoords[4], SkBlendMode mode,
                                     const SkPaint& paint) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = DrawPatchOp::Create(mCurrentRenderCommandBuffer, cubics, colors, texCoords, mode,
                                  paint, &mResourceCache);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
void IPCRecordingCanvas::onDrawPoints(PointMode mode, size_t count, const SkPoint pts[],
                                      const SkPaint& paint) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = DrawPointsOp::Create(mCurrentRenderCommandBuffer, mode, count, pts, paint,
                                   &mResourceCache);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}
void IPCRecordingCanvas::onDrawVerticesObject(const SkVertices* vertices, SkBlendMode mode,
                                              const SkPaint& paint) {
    IPC_CANVAS_TRACE_CALL;
    ALOGE("onDrawVerticesObject Not implemented");
    
    #if 0
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = DrawVerticesOp::Create(mCurrentRenderCommandBuffer, vertices, mode, paint, &mResourceCache);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
    #endif
}
void IPCRecordingCanvas::onDrawMesh(const SkMesh&, sk_sp<SkBlender>, const SkPaint&) {
    IPC_CANVAS_TRACE_CALL;
    ALOGE("onDrawMesh Not implemented");
}
void IPCRecordingCanvas::onDrawAtlas2(const SkImage*, const SkRSXform[], const SkRect[],
                                      const SkColor[], int, SkBlendMode, const SkSamplingOptions&,
                                      const SkRect*, const SkPaint*) {
    IPC_CANVAS_TRACE_CALL;
    ALOGE("onDrawAtlas Not implemented");
}
void IPCRecordingCanvas::onDrawShadowRec(const SkPath& path, const SkDrawShadowRec& rec) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = DrawShadowRecOp::Create(mCurrentRenderCommandBuffer, path, rec);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}

void IPCRecordingCanvas::beginRenderTarget(uint64_t bufferId) {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = BeginRenderTargetOp::Create(mCurrentRenderCommandBuffer, bufferId);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}

void IPCRecordingCanvas::endRenderTarget() {
    IPC_CANVAS_TRACE_CALL;
    LOG_ALWAYS_FATAL_IF(mCurrentRenderCommandBuffer == nullptr, "Not recording");
    auto op = EndRenderTargetOp::Create(mCurrentRenderCommandBuffer);
    LOG_ALWAYS_FATAL_IF(op == nullptr, "%s : Failed to alloc op", __func__);
    mCurrentRenderCommandBuffer->pushOp(op);
}

} // namespace android
