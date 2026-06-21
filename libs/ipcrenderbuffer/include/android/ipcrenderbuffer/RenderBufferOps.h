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

#pragma once

#include <SkRect.h>
#include <gui/RenderCommandBuffer.h>
#include <gui/RenderCommandBufferConsumer.h>
#include <ui/GraphicBuffer.h>

#include <SkAndroidFrameworkUtils.h>
#include <SkCanvas.h>
/*
#include <SkCanvasPriv.h>
*/
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include <SkCanvasVirtualEnforcer.h>
#pragma clang diagnostic pop
#include <SkColor.h>
#include <SkDrawable.h>
#include <SkSurface.h>
#include "src/core/SkDrawShadowInfo.h"
// #include <SkGainmapInfo.h>
#include <SkBitmap.h>
#include <SkImage.h>
#include <SkImageAndroid.h>
#include <SkNoDrawCanvas.h>
#include <SkPaint.h>
#include <SkPath.h>
#include <SkPixmap.h>
#include <SkRRect.h>
#include <SkRect.h>
#include <SkRegion.h>
#include <SkRuntimeEffect.h>
#include <SkSerialProcs.h>
#include <SkTextBlob.h>
#include <SkVertices.h>
#include <unordered_set>

#include <SkStream.h>
#include <sys/stat.h>
// #include <ports/SkFontMgr_android.h>
#include <SkColorSpace.h>
#include <SkData.h>
#include <SkFontArguments.h>
#include <SkFontMgr.h>
#include <SkFontMgr_android.h>
#include <SkFontMgr_android_ndk.h>
#include <SkFontMgr_empty.h>

#include <functional>
#include <map>
#include <mutex>

#include <android/ipcrenderbuffer/RenderBufferDebugUtils.h>
#include <android/ipcrenderbuffer/RenderBufferOpTypes.h>

#define IPCRENDERBUFFER_UNIMPLEMENTED_IS_FATAL 0
#ifdef IPCRENDERRBUFFER_UNIMPLEMENTED_IS_FATAL
#define IPCRENDERBUFFER_UNIMPLEMENTED LOG_ALWAYS_FATAL("Not implemented %s", __FUNCTION)
#else
#define IPCRENDERBUFFER_UNIMPLEMENTED ALOGE("Not implemented %s", __FUNCTION__)
#endif

namespace android {

struct IPCClientBitmap {
    uint64_t id = 0;
    sp<GraphicBuffer> buffer;

    // If this image is backed by a heap bitmap then this will be set.
    SkBitmap bitmap;
};

struct IPCClientResourceCache {
    std::unordered_set<uint32_t> typefaces;
    std::map<uint64_t, IPCClientBitmap> bitmaps;
};

struct IPCServerBitmap {
    sp<GraphicBuffer> buffer;
    sk_sp<SkImage> image;
    sk_sp<SkSurface> surface;
};

struct IPCServerResourceCache {
    std::map<uint64_t, IPCServerBitmap> bitmaps;
    std::map<uint32_t, sk_sp<SkTypeface>> typefaces;
};

struct ShmemPaint {
    RSpan<uint8_t> data;
};

// Derived from RecordingCanvas.cpp
struct SaveOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_SAVE;

    static SaveOp* Create(RenderCommandBuffer* commandBuffer);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct RestoreOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_RESTORE;

    static RestoreOp* Create(RenderCommandBuffer* commandBuffer);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct SaveLayerOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_SAVELAYER;
    SkRect bounds;
    ShmemPaint paint;
    bool hasBounds;
    bool hasPaint;

    static SaveLayerOp* Create(RenderCommandBuffer* commandBuffer, const SkRect* bounds,
                               const SkPaint* paint, IPCClientResourceCache* clientCache = nullptr);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct SaveBehindOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_SAVEBEHIND;
    SkRect subset;

    static SaveBehindOp* Create(RenderCommandBuffer* commandBuffer, const SkRect& subset);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct ConcatOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CONCAT;
    SkM44 matrix;

    static ConcatOp* Create(RenderCommandBuffer* commandBuffer, const SkM44& matrix);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct SetMatrixOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_SETMATRIX;
    SkM44 matrix;

    static SetMatrixOp* Create(RenderCommandBuffer* commandBuffer, const SkM44& matrix);
    void draw(SkCanvas* c, const SkMatrix& original, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct ScaleOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_SCALE;
    SkScalar sx, sy;

    static ScaleOp* Create(RenderCommandBuffer* commandBuffer, SkScalar sx, SkScalar sy);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct TranslateOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_TRANSLATE;
    SkScalar dx, dy;

    static TranslateOp* Create(RenderCommandBuffer* commandBuffer, SkScalar dx, SkScalar dy);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct ClipPathOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPPATH;
    SkClipOp op;
    bool aa;
    RSpan<uint8_t> pathData;

    static ClipPathOp* Create(RenderCommandBuffer* commandBuffer, const SkPath& path, SkClipOp op,
                              bool aa);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct ClipRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPRECT;
    SkRect rect;
    SkClipOp op;
    bool aa;

    static ClipRectOp* Create(RenderCommandBuffer* commandBuffer, const SkRect& rect, SkClipOp op,
                              bool aa);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct ClipRRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPRRECT;
    SkRRect rrect;
    SkClipOp op;
    bool aa;

    static ClipRRectOp* Create(RenderCommandBuffer* commandBuffer, const SkRRect& rrect,
                               SkClipOp op, bool aa);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct ClipRegionOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPREGION;
    SkClipOp op;
    RSpan<uint8_t> regionData;

    static ClipRegionOp* Create(RenderCommandBuffer* commandBuffer, const SkRegion& region,
                                SkClipOp op);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct ClipShaderOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_CLIPSHADER;

    static ClipShaderOp* Create(RenderCommandBuffer* commandBuffer,
                                const sk_sp<SkShader>& /*shader*/, SkClipOp op);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    SkClipOp op;
    std::string toString() const;
};

struct ResetClipOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_RESETCLIP;

    static ResetClipOp* Create(RenderCommandBuffer* commandBuffer);
    void draw(SkCanvas* c, const SkMatrix&, const SkRect& initialClip,
              IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawPaintOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPAINT;
    ShmemPaint paint;

    static DrawPaintOp* Create(RenderCommandBuffer* commandBuffer, const SkPaint& p,
                               IPCClientResourceCache* clientCache = nullptr);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawBehindOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWBEHIND;
    ShmemPaint paint;

    static DrawBehindOp* Create(RenderCommandBuffer* commandBuffer, const SkPaint& p,
                                IPCClientResourceCache* clientCache = nullptr);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawShadowRecOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWSHADOWREC;
    RSpan<uint8_t> pathData;
    SkDrawShadowRec rec;

    static DrawShadowRecOp* Create(RenderCommandBuffer* commandBuffer, const SkPath& path,
                                   const SkDrawShadowRec& rec);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawPathOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPATH;
    ShmemPaint paint;
    RSpan<uint8_t> pathData;

    static DrawPathOp* Create(RenderCommandBuffer* commandBuffer, const SkPath& path,
                              const SkPaint& p, IPCClientResourceCache* clientCache = nullptr);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWRECT;
    SkRect rect;
    ShmemPaint paint;

    static DrawRectOp* Create(RenderCommandBuffer* commandBuffer, const SkRect& r, const SkPaint& p,
                              IPCClientResourceCache* clientCache = nullptr);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawRegionOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWREGION;
    RSpan<uint8_t> regionData;
    ShmemPaint paint;

    static DrawRegionOp* Create(RenderCommandBuffer* commandBuffer, const SkRegion& r,
                                const SkPaint& p, IPCClientResourceCache* clientCache = nullptr);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawOvalOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWOVAL;
    SkRect oval;
    ShmemPaint paint;

    static DrawOvalOp* Create(RenderCommandBuffer* commandBuffer, const SkRect& o, const SkPaint& p,
                              IPCClientResourceCache* clientCache = nullptr);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawArcOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWARC;
    SkRect oval;
    SkScalar startAngle;
    SkScalar sweepAngle;
    bool useCenter;
    ShmemPaint paint;

    static DrawArcOp* Create(RenderCommandBuffer* commandBuffer, const SkRect& oval,
                             SkScalar startAngle, SkScalar sweepAngle, bool useCenter,
                             const SkPaint& paint, IPCClientResourceCache* clientCache = nullptr);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawRRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWRRECT;
    SkRRect rrect;
    ShmemPaint paint;

    static DrawRRectOp* Create(RenderCommandBuffer* commandBuffer, const SkRRect& rr,
                               const SkPaint& p, IPCClientResourceCache* clientCache = nullptr);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawAnnotationOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWANNOTATION;

    static DrawAnnotationOp* Create(RenderCommandBuffer* commandBuffer, const SkRect& rect,
                                    const char* text, SkData* data);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawDrawableOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWDRAWABLE;

    static DrawDrawableOp* Create(RenderCommandBuffer* commandBuffer, SkDrawable* drawable,
                                  const SkMatrix* matrix);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawPictureOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPICTURE;

    static DrawPictureOp* Create(RenderCommandBuffer* commandBuffer, const SkPicture* picture,
                                 const SkMatrix* matrix, const SkPaint* paint);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawImageOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWIMAGE;
    uint64_t bitmapId;
    SkScalar x;
    SkScalar y;
    SkSamplingOptions sampling;
    ShmemPaint paint;
    bool hasPaint;

    static DrawImageOp* Create(RenderCommandBuffer* commandBuffer, uint64_t bitmapId, SkScalar x,
                               SkScalar y, const SkSamplingOptions& sampling, const SkPaint* paint,
                               IPCClientResourceCache* clientCache = nullptr);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

// TODO(b/448196792): Implement hardware bitmap support
struct DrawImageRectOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWIMAGERECT;
    uint64_t bitmapId;
    SkRect src;
    SkRect dst;
    SkSamplingOptions sampling;
    ShmemPaint paint;
    bool hasPaint;
    SkCanvas::SrcRectConstraint constraint;

    static DrawImageRectOp* Create(RenderCommandBuffer* commandBuffer, uint64_t bitmapId,
                                   const SkRect& src, const SkRect& dst,
                                   const SkSamplingOptions& sampling, const SkPaint* paint,
                                   SkCanvas::SrcRectConstraint constraint);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct ShmemTypeface {
    RSpan<char> name;
    int weight;
    int width;
    SkFontStyle::Slant slant;
};
struct DrawTextBlobOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWTEXTBLOB;
    ShmemPaint paint;
    SkScalar x;
    SkScalar y;
    RSpan<uint8_t> blobData;

    static DrawTextBlobOp* Create(RenderCommandBuffer* commandBuffer, const SkTextBlob* blob,
                                  SkScalar x_in, SkScalar y_in, const SkPaint& p,
                                  IPCClientResourceCache* cache);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawPatchOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPATCH;
    RSpan<SkPoint> points;
    RSpan<SkColor> colors;
    RSpan<SkPoint> texCoords;
    SkBlendMode mode;
    ShmemPaint paint;

    static DrawPatchOp* Create(RenderCommandBuffer* commandBuffer, const SkPoint inPoints[12],
                               const SkColor inColors[4], const SkPoint inTexCoords[4],
                               SkBlendMode inMode, const SkPaint& inPaint,
                               IPCClientResourceCache* clientCache = nullptr);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawPointsOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPOINTS;
    SkCanvas::PointMode mode;
    RSpan<SkPoint> points;
    ShmemPaint paint;

    static DrawPointsOp* Create(RenderCommandBuffer* commandBuffer, SkCanvas::PointMode mode,
                                size_t count, const SkPoint* points, const SkPaint& paint,
                                IPCClientResourceCache* clientCache = nullptr);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawVerticesOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWVERTICES;
    SkBlendMode mode;
    ShmemPaint paint;
    RSpan<uint8_t> verticesData;

    static DrawVerticesOp* Create(RenderCommandBuffer* commandBuffer, const SkVertices* vertices,
                                  SkBlendMode mode, const SkPaint& paint,
                                  IPCClientResourceCache* clientCache = nullptr);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

/*struct DrawMeshOp final : IPCRenderBufferOp {
  static const auto kType = TYPE_DRAWMESH;
  DrawMeshOp(const Mesh& mesh, const SkPaint& paint) {
    ALOGE("Not implemented %s", __FUNCTION__);
  }
};*/

struct DrawSkMeshOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWMESH;

    static DrawSkMeshOp* Create(RenderCommandBuffer* commandBuffer, const SkMesh& mesh,
                                sk_sp<SkBlender> blender, const SkPaint& paint,
                                IPCClientResourceCache* clientCache = nullptr);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawAtlasOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWATLAS;

    static DrawAtlasOp* Create(RenderCommandBuffer* commandBuffer, const SkImage* atlas,
                               const SkRSXform* xform, const SkRect* tex, const SkColor* colors,
                               int count, SkBlendMode mode, const SkSamplingOptions& sampling,
                               const SkRect* cull, const SkPaint* paint);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct DrawProxySurfaceControlOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_DRAWPROXYSURFACECONTROL;
    int proxyId;

    static DrawProxySurfaceControlOp* Create(RenderCommandBuffer* commandBuffer, int id);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct BeginRenderTargetOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_BEGINRENDERTARGET;
    uint64_t bufferId;

    static BeginRenderTargetOp* Create(RenderCommandBuffer* commandBuffer, uint64_t bufferId);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

struct EndRenderTargetOp final : IPCRenderBufferOp {
    static const auto kType = TYPE_ENDRENDERTARGET;

    static EndRenderTargetOp* Create(RenderCommandBuffer* commandBuffer);
    void draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache);
    std::string toString() const;
};

// Structs are defined in RenderCommandBuffer.h in libgui.

struct UploadBitmap final : IPCRenderBufferUploadOp {
    static const auto kType = TYPE_UPLOADBITMAP;
    uint64_t imageId;
    int32_t width;
    int32_t height;
    int32_t colorType;
    int32_t alphaType;
    size_t rowBytes;
    RSpan<uint8_t> pixels;

    static UploadBitmap* Create(IpcRenderRegion* region, uint64_t imageId, const SkBitmap& bitmap);
    void execute(IPCServerResourceCache& resourceCache);
    std::string toString() const;
};

struct FreeBitmap final : IPCRenderBufferUploadOp {
    static const auto kType = TYPE_FREEBITMAP;
    uint64_t imageId;

    static FreeBitmap* Create(IpcRenderRegion* region, uint64_t imageId);
    void execute(IPCServerResourceCache& resourceCache);
    std::string toString() const;
};

struct UploadTypeface final : IPCRenderBufferUploadOp {
    static const auto kType = TYPE_UPLOADTYPEFACE;
    uint32_t fontId;
    RSpan<uint8_t> data;

    static UploadTypeface* Create(IpcRenderRegion* region, uint32_t fontId, const SkData* data);
    void execute(IPCServerResourceCache& resourceCache);
    std::string toString() const;
};

} // namespace android
