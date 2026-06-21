/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <SkColorFilter.h>
#include <SkSurface.h>

#include "SkFontScanner_FreeType.h"
#include "src/core/SkReadBuffer.h"
//#include "src/core/SkVerticesPriv.h"
#include "src/core/SkWriteBuffer.h"

#include <android/ipcrenderbuffer/RenderBufferOps.h>
#include <android/ipcrenderbuffer/RenderBufferDebugUtils.h>

#include <unordered_map>
#include <unordered_set>

#define DUMP_OPS 0
// #define DUMP_OPS 1

namespace android {

void renderOpToCanvas(IPCServerResourceCache* cache, RenderCommandBuffer* buffer,
                      IPCRenderBufferOp* op, SkCanvas* canvas,
                      const std::function<void(int)>& renderProxyCallback,
                      const SkMatrix& initialMatrix = SkMatrix::I(),
                      const SkRect& initialClip = SkRect::MakeEmpty()) {
    switch (op->type) {
        case TYPE_SAVE: {
            SaveOp* co = (SaveOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_RESTORE: {
            RestoreOp* co = (RestoreOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_SAVELAYER: {
            SaveLayerOp* co = (SaveLayerOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_SAVEBEHIND: {
            SaveBehindOp* co = (SaveBehindOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_CONCAT: {
            ConcatOp* co = (ConcatOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_SETMATRIX: {
            SetMatrixOp* co = (SetMatrixOp*)op;
            co->draw(canvas, initialMatrix, cache);
            break;
        }
        case TYPE_SCALE: {
            ScaleOp* co = (ScaleOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_TRANSLATE: {
            TranslateOp* co = (TranslateOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_CLIPPATH: {
            ClipPathOp* co = (ClipPathOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_CLIPRECT: {
            ClipRectOp* co = (ClipRectOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_CLIPRRECT: {
            ClipRRectOp* co = (ClipRRectOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_CLIPREGION: {
            ClipRegionOp* co = (ClipRegionOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_CLIPSHADER: {
            ClipShaderOp* co = (ClipShaderOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_RESETCLIP: {
            ResetClipOp* co = (ResetClipOp*)op;
            co->draw(canvas, initialMatrix, initialClip, cache);
            break;
        }
        case TYPE_DRAWPAINT: {
            DrawPaintOp* co = (DrawPaintOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWBEHIND: {
            DrawBehindOp* co = (DrawBehindOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWPATH: {
            DrawPathOp* co = (DrawPathOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWRECT: {
            DrawRectOp* co = (DrawRectOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWREGION: {
            DrawRegionOp* co = (DrawRegionOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWOVAL: {
            DrawOvalOp* co = (DrawOvalOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWARC: {
            DrawArcOp* co = (DrawArcOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWRRECT: {
            DrawRRectOp* co = (DrawRRectOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWANNOTATION: {
            DrawAnnotationOp* co = (DrawAnnotationOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWDRAWABLE: {
            DrawDrawableOp* co = (DrawDrawableOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWPICTURE: {
            DrawPictureOp* co = (DrawPictureOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWIMAGE: {
            if (!cache) {
                ALOGE("DrawImageOp failed due to unpopulated cache.");
                break;
            }
            DrawImageOp* co = (DrawImageOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWIMAGERECT: {
            if (!cache) {
                ALOGE("DrawImageRectOp failed due to unpopulated cache.");
                break;
            }
            DrawImageRectOp* co = (DrawImageRectOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWTEXTBLOB: {
            if (!cache) {
                ALOGE("DrawTextBlobOp failed due to unpopulated cache.");
                break;
            }
            DrawTextBlobOp* co = (DrawTextBlobOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWPATCH: {
            DrawPatchOp* co = (DrawPatchOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWPOINTS: {
            DrawPointsOp* co = (DrawPointsOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWVERTICES: {
            DrawVerticesOp* co = (DrawVerticesOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWSKMESH: {
            DrawSkMeshOp* co = (DrawSkMeshOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWATLAS: {
            DrawAtlasOp* co = (DrawAtlasOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWSHADOWREC: {
            DrawShadowRecOp* co = (DrawShadowRecOp*)op;
            co->draw(canvas, SkMatrix::I(), cache);
            break;
        }
        case TYPE_DRAWPROXYSURFACECONTROL: {
            DrawProxySurfaceControlOp* co = (DrawProxySurfaceControlOp*)op;
            // co->draw(canvas, SkMatrix::I(), cache);
            renderProxyCallback(co->proxyId);
            break;
        }
        case TYPE_BEGINRENDERTARGET:
        case TYPE_ENDRENDERTARGET: {
            // Handled by loop
            break;
        }
        default: {
            ALOGE("Unexpected op in RenderCommandBuffer %d", op->type);
            break;
        }
    }
}

bool isDrawingOp(uint32_t type) {
    switch (type) {
        case TYPE_SAVE:
        case TYPE_RESTORE:
        case TYPE_SAVELAYER:
        case TYPE_SAVEBEHIND:
        case TYPE_CONCAT:
        case TYPE_SETMATRIX:
        case TYPE_SCALE:
        case TYPE_TRANSLATE:
        case TYPE_CLIPPATH:
        case TYPE_CLIPRECT:
        case TYPE_CLIPRRECT:
        case TYPE_CLIPREGION:
        case TYPE_CLIPSHADER:
        case TYPE_RESETCLIP:
        case TYPE_DRAWPROXYSURFACECONTROL: // Questionable
            return false;
        case TYPE_DRAWPAINT:
        case TYPE_DRAWBEHIND:
        case TYPE_DRAWPATH:
        case TYPE_DRAWRECT:
        case TYPE_DRAWREGION:
        case TYPE_DRAWOVAL:
        case TYPE_DRAWARC:
        case TYPE_DRAWRRECT:
        case TYPE_DRAWDRRECT:
        case TYPE_DRAWANNOTATION:
        case TYPE_DRAWDRAWABLE:
        case TYPE_DRAWPICTURE:
        case TYPE_DRAWIMAGE:
        case TYPE_DRAWIMAGERECT:
        case TYPE_DRAWIMAGELATTICE:
        case TYPE_DRAWTEXTBLOB:
        case TYPE_DRAWPATCH:
        case TYPE_DRAWPOINTS:
        case TYPE_DRAWVERTICES:
        case TYPE_DRAWATLAS:
        case TYPE_DRAWSHADOWREC:
        case TYPE_DRAWVECTORDRAWABLE:
        case TYPE_DRAWRIPPLEDRAWABLE:
        case TYPE_DRAWWEBVIEW:
        case TYPE_DRAWSKMESH:
        case TYPE_DRAWMESH:
            return true;
        default:
            return false;
    }
}

SkPaint fromShmemPaint(const ShmemPaint& paint, IPCServerResourceCache* serverCache) {
    if (!paint.data.data) {
        return SkPaint();
    }
    SkReadBuffer reader(paint.data.data.get(), paint.data.size);
    reader.setAllowSkSL(true);
    if (serverCache) {
        SkDeserialProcs procs;
        procs.fImageCtx = serverCache;
        procs.fImageProc = [](const void* data, size_t length, void* ctx) -> sk_sp<SkImage> {
            if (length != sizeof(uint64_t)) {
                return nullptr;
            }
            uint64_t id;
            memcpy(&id, data, sizeof(id));
            auto* cache = static_cast<IPCServerResourceCache*>(ctx);
            auto it = cache->bitmaps.find(id);
            if (it == cache->bitmaps.end()) {
                return nullptr;
            }
            return it->second.image;
        };
        reader.setDeserialProcs(procs);
    }
    return SkPaintPriv::Unflatten(reader);
}

bool isClear(const IPCRenderBufferOp* op) {
    if (op->type != TYPE_DRAWPAINT) {
        return false;
    }
    DrawPaintOp *dop = (DrawPaintOp *)op;
    auto paint = fromShmemPaint(dop->paint, nullptr);
    auto color = paint.getColor4f();
    if (color.fR == 0.0f && color.fG == 0.0f && color.fB == 0.0f && color.fA == 0.0f) {
        return true;
    }
    return false;
}

bool renderCommandBufferToCanvas(IPCServerResourceCache* cache, RenderCommandBuffer* buffer,
                                 SkCanvas* canvas,
                                 const std::function<void(int)>& renderProxyCallback) {
    bool foundFirstDrawingOp = false;

    if constexpr (DUMP_OPS) {
        ALOGE("Rendering command buffer");
    }

    while (auto* baseOp = buffer->mRegion->mUploadBuf.peek()) {
        auto* op = static_cast<IPCRenderBufferUploadOp*>(baseOp);
        if (op->type == TYPE_UPLOADBITMAP) {
            UploadBitmap* uo = static_cast<UploadBitmap*>(op);
            if (cache) uo->execute(*cache);
        } else if (op->type == TYPE_UPLOADTYPEFACE) {
            UploadTypeface* uo = static_cast<UploadTypeface*>(op);
            if (cache) uo->execute(*cache);
        } else if (op->type == TYPE_FREEBITMAP) {
            FreeBitmap* fo = static_cast<FreeBitmap*>(op);
            if (cache) fo->execute(*cache);
        }
        buffer->mRegion->mUploadBuf.pop();
    }

    SkMatrix rootMatrix = canvas->getTotalMatrix();
    SkRect rootClip = SkRect::Make(canvas->getDeviceClipBounds());

    sk_sp<SkSurface> boundSurface = nullptr;
    bool renderingOffscreenLayer = false;

    for (IPCRenderBufferOp* op = buffer->getOps(); op; op = op->next) {

        if constexpr (DUMP_OPS) {
            ALOGE("Rendering op %s", opTypeToString(op->type).c_str());
            ALOGE("Details %s", opToString(op).c_str());
        }

        if (op->type == TYPE_BEGINRENDERTARGET) {
            if (boundSurface) {
                if constexpr (DUMP_OPS) {
                    ALOGE("Nesting BeginRenderTargetOp is not supported");
                }
                return false;
            }
            renderingOffscreenLayer = true;

            if (cache) {
                BeginRenderTargetOp* co = (BeginRenderTargetOp*)op;
                auto it = cache->bitmaps.find(co->bufferId);
                if (it != cache->bitmaps.end()) {
                    boundSurface = it->second.surface;
                } else {
                    if constexpr (DUMP_OPS) {
                        ALOGE("Failed to acquire buffer surface for %" PRIu64, co->bufferId);
                    }
                    return false;
                }
            } else {
                if constexpr (DUMP_OPS) {
                    ALOGE("IPC cache is null, BeginRenderTargetOp requires a non-null cache");
                }
                return false;
            }

        } else if (op->type == TYPE_ENDRENDERTARGET) {
            renderingOffscreenLayer = false;
            if (!boundSurface) {
                if constexpr (DUMP_OPS) {
                    ALOGE("Encountered EndRenderTargetOp but no BeginRenderTargetOp was "
                          "submitted");
                }
                return false;
            }
            boundSurface = nullptr;
        } else {
            // TODO(b/485930305): Effectively every layer comes with a clear
            // at the beginning. We skip this as it's not useful (e.g. it will)
            // end up clearing whatever is underneath in the OOPR case. However
            // it would be better to just not omit it on the client side.
            if (!renderingOffscreenLayer && !foundFirstDrawingOp) {
                if (isDrawingOp(op->type)) {
                    foundFirstDrawingOp = true;
                }
                if (isClear(op)) {
                    continue;
                }
            }
            SkMatrix initialMatrix;
            SkRect initialClip;
            if (boundSurface) {
                initialMatrix = SkMatrix::I();
                initialClip = SkRect::MakeWH(boundSurface->width(), boundSurface->height());
            } else {
                initialMatrix = rootMatrix;
                initialClip = rootClip;
            }
            renderOpToCanvas(cache, buffer, op, boundSurface ? boundSurface->getCanvas() : canvas,
                             renderProxyCallback, initialMatrix, initialClip);
        }
    }
    if constexpr (DUMP_OPS) {
        ALOGE("Done rendering command buffer");
    }

    return true;
}

bool toShmemPaint(RenderCommandBuffer* buffer, const SkPaint& paint, ShmemPaint& outPaint,
                  IPCClientResourceCache* clientCache) {
    SkSerialProcs procs;
    procs.fImageCtx = clientCache;
    procs.fImageProc = [](SkImage* img, void* ctx) -> sk_sp<const SkData> {
        uint64_t bufferId = 0;
        if (ctx) {
            auto* cache = static_cast<IPCClientResourceCache*>(ctx);
            auto it = cache->bitmaps.find(img->uniqueID());
            if (it != cache->bitmaps.end()) {
                bufferId = it->second.id;
            }
        }
        return SkData::MakeWithCopy(&bufferId, sizeof(bufferId));
    };
    SkBinaryWriteBuffer writer(procs);
    SkPaintPriv::Flatten(paint, writer);
    sk_sp<SkData> data = writer.snapshotAsData();

    if (!SetRSpan(outPaint.data, buffer, (const uint8_t*)nullptr, data->size())) {
        return false;
    }
    if (data->size() > 0) {
        memcpy(outPaint.data.data.get(), data->data(), data->size());
    }
    return true;
}

std::string shmemPaintToString(const ShmemPaint& paint) {
    return std::string("ShmemPaint(size=") + std::to_string(paint.data.size) + ")";
}

#define OP_REQUIRE(expr)                                                                      \
    ({                                                                                        \
        if (!(expr)) {                                                                        \
            ALOGE("%s:%s:%d: Expression was false: %s", __FILE__, __func__, __LINE__, #expr); \
            return nullptr;                                                                   \
        }                                                                                     \
    })
SaveOp* SaveOp::Create(RenderCommandBuffer* commandBuffer) {
    SaveOp* op = commandBuffer->allocAligned<SaveOp>();
    OP_REQUIRE(op);
    op->type = kType;
    return op;
}

void SaveOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    c->save();
}

std::string SaveOp::toString() const {
    return std::string("SaveOp");
}

RestoreOp* RestoreOp::Create(RenderCommandBuffer* commandBuffer) {
    RestoreOp* op = commandBuffer->allocAligned<RestoreOp>();
    OP_REQUIRE(op);
    op->type = kType;
    return op;
}

void RestoreOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    c->restore();
}

std::string RestoreOp::toString() const {
    return std::string("RestoreOp");
}

SaveLayerOp* SaveLayerOp::Create(RenderCommandBuffer* commandBuffer, const SkRect* bounds,
                                 const SkPaint* paint, IPCClientResourceCache* clientCache) {
    SaveLayerOp* op = commandBuffer->allocAligned<SaveLayerOp>();
    OP_REQUIRE(op);
    op->type = kType;
    if (bounds) {
        op->bounds = *bounds;
        op->hasBounds = true;
    } else {
        op->hasBounds = false;
    }
    if (paint) {
        OP_REQUIRE(toShmemPaint(commandBuffer, *paint, op->paint, clientCache));
        op->hasPaint = true;
    } else {
        op->hasPaint = false;
    }
    return op;
}

void SaveLayerOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    const SkRect* boundsPtr = hasBounds ? &bounds : nullptr;
    SkPaint p;
    const SkPaint* paintPtr = hasPaint ? &(p = fromShmemPaint(paint, serverCache)) : nullptr;
    c->saveLayer(boundsPtr, paintPtr);
}

std::string SaveLayerOp::toString() const {
    return std::string("SaveLayerOp");
}

SaveBehindOp* SaveBehindOp::Create(RenderCommandBuffer* commandBuffer, const SkRect& subset) {
    SaveBehindOp* op = commandBuffer->allocAligned<SaveBehindOp>();
    OP_REQUIRE(op);
    op->type = kType;
    if (subset.isEmpty()) {
        op->subset.setEmpty();
    } else {
        op->subset = subset;
    }
    return op;
}

void SaveBehindOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    SkAndroidFrameworkUtils::SaveBehind(c, &subset);
}

std::string SaveBehindOp::toString() const {
    return std::string("SaveBehindOp subset: ") + rectToString(subset);
}

ConcatOp* ConcatOp::Create(RenderCommandBuffer* commandBuffer, const SkM44& matrix) {
    ConcatOp* op = commandBuffer->allocAligned<ConcatOp>();
    OP_REQUIRE(op);
    op->type = kType;
    op->matrix = matrix;
    return op;
}

void ConcatOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    c->concat(matrix);
}

std::string ConcatOp::toString() const {
    return std::string("ConcatOp matrix: ") + skmatrixToString(matrix);
}

SetMatrixOp* SetMatrixOp::Create(RenderCommandBuffer* commandBuffer, const SkM44& matrix) {
    SetMatrixOp* op = commandBuffer->allocAligned<SetMatrixOp>();
    OP_REQUIRE(op);
    op->type = kType;
    op->matrix = matrix;
    return op;
}

void SetMatrixOp::draw(SkCanvas* c, const SkMatrix& original, IPCServerResourceCache* serverCache) {
    c->setMatrix(SkM44(original) * matrix);
}

std::string SetMatrixOp::toString() const {
    return std::string("SetMatrixOp matrix: ") + skmatrixToString(matrix);
}

ScaleOp* ScaleOp::Create(RenderCommandBuffer* commandBuffer, SkScalar sx, SkScalar sy) {
    ScaleOp* op = commandBuffer->allocAligned<ScaleOp>();
    OP_REQUIRE(op);
    op->type = kType;
    op->sx = sx;
    op->sy = sy;
    return op;
}

void ScaleOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    c->scale(sx, sy);
}

std::string ScaleOp::toString() const {
    return std::string("ScaleOp sx: ") + std::to_string(sx) + std::string(" sy: ") +
            std::to_string(sy);
}

TranslateOp* TranslateOp::Create(RenderCommandBuffer* commandBuffer, SkScalar dx, SkScalar dy) {
    TranslateOp* op = commandBuffer->allocAligned<TranslateOp>();
    OP_REQUIRE(op);
    op->type = kType;
    op->dx = dx;
    op->dy = dy;
    return op;
}

void TranslateOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    c->translate(dx, dy);
}

std::string TranslateOp::toString() const {
    return std::string("TranslateOp dx: ") + std::to_string(dx) + std::string(" dy: ") +
            std::to_string(dy);
}

ClipPathOp* ClipPathOp::Create(RenderCommandBuffer* commandBuffer, const SkPath& path, SkClipOp op,
                               bool aa) {
    ClipPathOp* opData = commandBuffer->allocAligned<ClipPathOp>();
    OP_REQUIRE(opData);
    size_t pathSize = path.writeToMemory(nullptr);
    OP_REQUIRE(SetRSpan<uint8_t>(opData->pathData, commandBuffer, nullptr, pathSize));
    path.writeToMemory(opData->pathData.data.get());
    opData->type = kType;
    opData->op = op;
    opData->aa = aa;
    return opData;
}

void ClipPathOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    const SkPath path =
        SkPath::ReadFromMemory(pathData.data.get(), pathData.size).value_or(SkPath());
    c->clipPath(path, op, aa);
}

std::string ClipPathOp::toString() const {
    return "ClipPathOp";
}

ClipRectOp* ClipRectOp::Create(RenderCommandBuffer* commandBuffer, const SkRect& rect, SkClipOp op,
                               bool aa) {
    ClipRectOp* opData = commandBuffer->allocAligned<ClipRectOp>();
    OP_REQUIRE(opData);
    opData->type = kType;
    opData->rect = rect;
    opData->op = op;
    opData->aa = aa;
    return opData;
}

void ClipRectOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    c->clipRect(rect, op, aa);
}

std::string ClipRectOp::toString() const {
    return std::string("ClipRectOp: rect: ") + rectToString(rect) + std::string(" op: ") +
            std::to_string((int)op) + std::string(" aa: ") + std::to_string(aa);
}

ClipRRectOp* ClipRRectOp::Create(RenderCommandBuffer* commandBuffer, const SkRRect& rrect,
                                 SkClipOp op, bool aa) {
    ClipRRectOp* opData = commandBuffer->allocAligned<ClipRRectOp>();
    OP_REQUIRE(opData);
    opData->type = kType;
    opData->rrect = rrect;
    opData->op = op;
    opData->aa = aa;
    return opData;
}

void ClipRRectOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    c->clipRRect(rrect, op, aa);
}

std::string ClipRRectOp::toString() const {
    std::string rectAsString = std::string(rrect.dumpToString(false).c_str());
    return std::string("ClipRRectOp: rrect: ") + rectAsString + std::string(" op: ") +
            std::to_string((int)op) + std::string(" aa: ") + std::to_string(aa);
}

ClipRegionOp* ClipRegionOp::Create(RenderCommandBuffer* commandBuffer, const SkRegion& region,
                                   SkClipOp op) {
    IPCRENDERBUFFER_UNIMPLEMENTED;
    return nullptr;
}

void ClipRegionOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    SkRegion region;
    region.readFromMemory(regionData.data.get(), regionData.size);
    c->clipRegion(region, op);
}

std::string ClipRegionOp::toString() const {
    return "ClipRegionOp";
}

ClipShaderOp* ClipShaderOp::Create(RenderCommandBuffer* commandBuffer,
                                   const sk_sp<SkShader>& /*shader*/, SkClipOp op) {
    IPCRENDERBUFFER_UNIMPLEMENTED;
    return nullptr;
}

void ClipShaderOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    IPCRENDERBUFFER_UNIMPLEMENTED;
}

std::string ClipShaderOp::toString() const {
    return "ClipShaderOp";
}

ResetClipOp* ResetClipOp::Create(RenderCommandBuffer* commandBuffer) {
    ResetClipOp* op = commandBuffer->allocAligned<ResetClipOp>();
    OP_REQUIRE(op);
    op->type = kType;
    return op;
}

void ResetClipOp::draw(SkCanvas* c, const SkMatrix&, const SkRect& initialClip,
                       IPCServerResourceCache* serverCache) {
    SkAndroidFrameworkUtils::ResetClip(c);
    if (!initialClip.isEmpty()) {
        SkMatrix ctm = c->getTotalMatrix();
        c->setMatrix(SkMatrix::I());
        c->clipRect(initialClip, SkClipOp::kIntersect, false);
        c->setMatrix(ctm);
    }
}

std::string ResetClipOp::toString() const {
    return "ResetClipOp";
}

DrawPaintOp* DrawPaintOp::Create(RenderCommandBuffer* commandBuffer, const SkPaint& p,
                                 IPCClientResourceCache* clientCache) {
    DrawPaintOp* op = commandBuffer->allocAligned<DrawPaintOp>();
    OP_REQUIRE(op);
    OP_REQUIRE(toShmemPaint(commandBuffer, p, op->paint, clientCache));
    op->type = kType;
    return op;
}

void DrawPaintOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    c->drawPaint(fromShmemPaint(paint, serverCache));
}

std::string DrawPaintOp::toString() const {
    return "DrawPaintOp" + shmemPaintToString(paint);
}

DrawBehindOp* DrawBehindOp::Create(RenderCommandBuffer* commandBuffer, const SkPaint& p,
                                   IPCClientResourceCache* clientCache) {
    DrawBehindOp* op = commandBuffer->allocAligned<DrawBehindOp>();
    OP_REQUIRE(op);
    OP_REQUIRE(toShmemPaint(commandBuffer, p, op->paint, clientCache));
    op->type = kType;
    return op;
}

void DrawBehindOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    IPCRENDERBUFFER_UNIMPLEMENTED;
}
std::string DrawBehindOp::toString() const {
    return "DrawBehindOp";
}

DrawPathOp* DrawPathOp::Create(RenderCommandBuffer* commandBuffer, const SkPath& path,
                               const SkPaint& p, IPCClientResourceCache* clientCache) {
    DrawPathOp* op = commandBuffer->allocAligned<DrawPathOp>();
    OP_REQUIRE(op);
    size_t pathSize = path.writeToMemory(nullptr);
    OP_REQUIRE(SetRSpan<uint8_t>(op->pathData, commandBuffer, nullptr, pathSize));
    path.writeToMemory(op->pathData.data.get());
    OP_REQUIRE(toShmemPaint(commandBuffer, p, op->paint, clientCache));
    op->type = kType;
    return op;
}

void DrawPathOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    const SkPath path =
        SkPath::ReadFromMemory(pathData.data.get(), pathData.size).value_or(SkPath());
    c->drawPath(path, fromShmemPaint(paint, serverCache));
}
std::string DrawPathOp::toString() const {
    return "DrawPathOp";
}

DrawRectOp* DrawRectOp::Create(RenderCommandBuffer* commandBuffer, const SkRect& r,
                               const SkPaint& p, IPCClientResourceCache* clientCache) {
    DrawRectOp* op = commandBuffer->allocAligned<DrawRectOp>();
    OP_REQUIRE(op);
    op->rect = r;
    OP_REQUIRE(toShmemPaint(commandBuffer, p, op->paint, clientCache));
    op->type = kType;
    return op;
}

void DrawRectOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    c->drawRect(rect, fromShmemPaint(paint, serverCache));
}

std::string DrawRectOp::toString() const {
    return std::string("DrawRectOp rect(") + rectToString(rect) + ") " + std::string(" paint: ") +
            shmemPaintToString(paint);
}

DrawRegionOp* DrawRegionOp::Create(RenderCommandBuffer* commandBuffer, const SkRegion& r,
                                   const SkPaint& p, IPCClientResourceCache* clientCache) {
    DrawRegionOp* op = commandBuffer->allocAligned<DrawRegionOp>();
    OP_REQUIRE(op);
    size_t regionSize = r.writeToMemory(nullptr);
    OP_REQUIRE(SetRSpan<uint8_t>(op->regionData, commandBuffer, nullptr, regionSize));
    r.writeToMemory(op->regionData.data.get());
    OP_REQUIRE(toShmemPaint(commandBuffer, p, op->paint, clientCache));
    op->type = kType;
    return op;
}

void DrawRegionOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    SkRegion region;
    region.readFromMemory(regionData.data.get(), regionData.size);
    c->drawRegion(region, fromShmemPaint(paint, serverCache));
}

std::string DrawRegionOp::toString() const {
    return "DrawRegionOp";
}

DrawOvalOp* DrawOvalOp::Create(RenderCommandBuffer* commandBuffer, const SkRect& o,
                               const SkPaint& p, IPCClientResourceCache* clientCache) {
    DrawOvalOp* op = commandBuffer->allocAligned<DrawOvalOp>();
    OP_REQUIRE(op);
    op->oval = o;
    OP_REQUIRE(toShmemPaint(commandBuffer, p, op->paint, clientCache));
    op->type = kType;
    return op;
}

void DrawOvalOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    c->drawOval(oval, fromShmemPaint(paint, serverCache));
}
std::string DrawOvalOp::toString() const {
    return std::string("DrawOvalOp") + rectToString(oval);
}

DrawArcOp* DrawArcOp::Create(RenderCommandBuffer* commandBuffer, const SkRect& oval,
                             SkScalar startAngle, SkScalar sweepAngle, bool useCenter,
                             const SkPaint& paint, IPCClientResourceCache* clientCache) {
    DrawArcOp* op = commandBuffer->allocAligned<DrawArcOp>();
    OP_REQUIRE(op);
    op->type = kType;
    OP_REQUIRE(toShmemPaint(commandBuffer, paint, op->paint, clientCache));
    op->oval = oval;
    op->startAngle = startAngle;
    op->sweepAngle = sweepAngle;
    op->useCenter = useCenter;
    return op;
}

void DrawArcOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    c->drawArc(oval, startAngle, sweepAngle, useCenter, fromShmemPaint(paint, serverCache));
}
std::string DrawArcOp::toString() const {
    return std::string("DrawArcOp") + rectToString(oval) + std::string(" startAngle: ") +
            std::to_string(startAngle) + std::string(" sweepAngle: ") + std::to_string(sweepAngle) +
            std::string(" useCenter: ") + std::to_string(useCenter);
}

DrawRRectOp* DrawRRectOp::Create(RenderCommandBuffer* commandBuffer, const SkRRect& rr,
                                 const SkPaint& p, IPCClientResourceCache* clientCache) {
    DrawRRectOp* op = commandBuffer->allocAligned<DrawRRectOp>();
    OP_REQUIRE(op);
    OP_REQUIRE(toShmemPaint(commandBuffer, p, op->paint, clientCache));
    op->rrect = rr;
    op->type = kType;
    return op;
}

void DrawRRectOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    c->drawRRect(rrect, fromShmemPaint(paint, serverCache));
}
std::string DrawRRectOp::toString() const {
    return std::string("DrawRRectOp") + std::string(rrect.dumpToString(false).c_str());
}

DrawAnnotationOp* DrawAnnotationOp::Create(RenderCommandBuffer* commandBuffer, const SkRect& rect,
                                           const char* text, SkData* data) {
    DrawAnnotationOp* op = commandBuffer->allocAligned<DrawAnnotationOp>();
    OP_REQUIRE(op);
    IPCRENDERBUFFER_UNIMPLEMENTED;
    op->type = kType;
    return op;
}

void DrawAnnotationOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    IPCRENDERBUFFER_UNIMPLEMENTED;
}
std::string DrawAnnotationOp::toString() const {
    return "DrawAnnotationOp";
}

DrawDrawableOp* DrawDrawableOp::Create(RenderCommandBuffer* commandBuffer, SkDrawable* drawable,
                                       const SkMatrix* matrix) {
    DrawDrawableOp* op = commandBuffer->allocAligned<DrawDrawableOp>();
    OP_REQUIRE(op);
    IPCRENDERBUFFER_UNIMPLEMENTED;
    op->type = kType;
    return op;
}

void DrawDrawableOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    IPCRENDERBUFFER_UNIMPLEMENTED;
}
std::string DrawDrawableOp::toString() const {
    return "DrawDrawableOp";
}

DrawPictureOp* DrawPictureOp::Create(RenderCommandBuffer* commandBuffer, const SkPicture* picture,
                                     const SkMatrix* matrix, const SkPaint* paint) {
    DrawPictureOp* op = commandBuffer->allocAligned<DrawPictureOp>();
    OP_REQUIRE(op);
    IPCRENDERBUFFER_UNIMPLEMENTED;
    op->type = kType;
    return op;
}

void DrawPictureOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    IPCRENDERBUFFER_UNIMPLEMENTED;
}
std::string DrawPictureOp::toString() const {
    return "DrawPictureOp";
}

DrawImageOp* DrawImageOp::Create(RenderCommandBuffer* commandBuffer, uint64_t bitmapId, SkScalar x,
                                 SkScalar y, const SkSamplingOptions& sampling,
                                 const SkPaint* paint, IPCClientResourceCache* clientCache) {
    // SkCanvas::drawImage uses ImageRectOp
    IPCRENDERBUFFER_UNIMPLEMENTED;
    return nullptr;
}

void DrawImageOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    // SkCanvas::drawImage uses ImageRectOp
    IPCRENDERBUFFER_UNIMPLEMENTED;
}
std::string DrawImageOp::toString() const {
    return "DrawImageOp";
}

DrawImageRectOp* DrawImageRectOp::Create(RenderCommandBuffer* commandBuffer, uint64_t bitmapId,
                                         const SkRect& src, const SkRect& dst,
                                         const SkSamplingOptions& sampling, const SkPaint* paint,
                                         SkCanvas::SrcRectConstraint constraint) {
    DrawImageRectOp* op = commandBuffer->allocAligned<DrawImageRectOp>();
    OP_REQUIRE(op);
    op->type = kType;
    op->bitmapId = bitmapId;
    op->src = src;
    op->dst = dst;
    op->sampling = sampling;
    if (paint) {
        OP_REQUIRE(toShmemPaint(commandBuffer, *paint, op->paint, nullptr)); // FIXME: no cache
        op->hasPaint = true;
    } else {
        op->hasPaint = false;
    }
    op->constraint = constraint;
    return op;
}

void DrawImageRectOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    auto it = serverCache->bitmaps.find(bitmapId);
    if (it == serverCache->bitmaps.end()) {
        // This currently only happens when a process shuts down.
        // There may be a frame remaining that references bitmaps which were destroyed.
        ALOGE("Bitmap not found in cache id=%" PRIu64, bitmapId);
        return;
    }
    SkPaint p;
    const SkPaint* paintPtr = hasPaint ? &(p = fromShmemPaint(paint, serverCache)) : nullptr;
    c->drawImageRect(it->second.image, src, dst, sampling, paintPtr, constraint);
}
std::string DrawImageRectOp::toString() const {
    return "DrawImageRectOp";
}

struct SerializeTypefaceContext {
    RenderCommandBuffer* commandBuffer;
    IPCClientResourceCache* cache;
};

SkSerialReturnType serializeTypeFace(SkTypeface* tf, void* ctx) {
    SerializeTypefaceContext* stc = reinterpret_cast<SerializeTypefaceContext*>(ctx);
    RenderCommandBuffer* commandBuffer = stc->commandBuffer;
    IPCClientResourceCache* cache = stc->cache;

    uint32_t id = tf->uniqueID();
    if (cache && cache->typefaces.count(id) == 0) {
        cache->typefaces.insert(id);
        auto data = tf->serialize(SkTypeface::SerializeBehavior::kDoIncludeData);
        if (data) {
            UploadTypeface::Create(commandBuffer->mRegion.get(), id, data.get());
        }
    }

    return SkData::MakeWithCopy(&id, sizeof(id));
}

sk_sp<SkTypeface> deserializeTypeFace(SkStream& stream, void* ctx) {
    auto* cache = reinterpret_cast<IPCServerResourceCache*>(ctx);
    if (!cache) {
        ALOGE("Trying to draw text with no resource cache!");
        return nullptr;
    }

    uint32_t id;
    if (stream.read(&id, sizeof(id)) != sizeof(id)) {
        return nullptr;
    }

    auto it = cache->typefaces.find(id);
    if (it != cache->typefaces.end()) {
        return it->second;
    }

    ALOGE("Typeface id %u expected to be cached but not found", id);
    return nullptr;
}

DrawTextBlobOp* DrawTextBlobOp::Create(RenderCommandBuffer* commandBuffer, const SkTextBlob* blob,
                                       SkScalar x_in, SkScalar y_in, const SkPaint& p,
                                       IPCClientResourceCache* cache) {
    SkSerialProcs procs;

    DrawTextBlobOp* op = commandBuffer->allocAligned<DrawTextBlobOp>();
    OP_REQUIRE(op);

    op->type = kType;
    OP_REQUIRE(toShmemPaint(commandBuffer, p, op->paint, cache));
    op->x = x_in;
    op->y = y_in;

    SerializeTypefaceContext ctx = {commandBuffer, cache};
    procs.fTypefaceCtx = &ctx;
    procs.fTypefaceProc = serializeTypeFace;

    auto data = blob->serialize(procs);
    OP_REQUIRE(data);

    OP_REQUIRE(SetRSpan<uint8_t>(op->blobData, commandBuffer, (const uint8_t*)data->data(),
                                 data->size()));

    return op;
}

void DrawTextBlobOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    SkDeserialProcs procs;
    procs.fTypefaceCtx = serverCache;
    procs.fTypefaceStreamProc = deserializeTypeFace;
    sk_sp<SkTextBlob> blob = SkTextBlob::Deserialize(blobData.data.get(), blobData.size, procs);
    if (blob) {
        c->drawTextBlob(blob, x, y, fromShmemPaint(paint, serverCache));
    } else {
        ALOGE("Failed to deserialize text blob");
    }
}
std::string DrawTextBlobOp::toString() const {
    return "DrawTextBlobOp";
}

DrawPatchOp* DrawPatchOp::Create(RenderCommandBuffer* commandBuffer, const SkPoint inPoints[12],
                                 const SkColor inColors[4], const SkPoint inTexCoords[4],
                                 SkBlendMode inMode, const SkPaint& inPaint,
                                 IPCClientResourceCache* clientCache) {
    DrawPatchOp* op = commandBuffer->allocAligned<DrawPatchOp>();
    OP_REQUIRE(op);
    op->type = kType;
    op->mode = inMode;
    OP_REQUIRE(toShmemPaint(commandBuffer, inPaint, op->paint, clientCache));

    OP_REQUIRE(SetRSpan(op->points, commandBuffer, inPoints, 12));
    OP_REQUIRE(SetRSpan(op->colors, commandBuffer, inColors, 4));
    OP_REQUIRE(SetRSpan(op->texCoords, commandBuffer, inTexCoords, 4));
    return op;
}

void DrawPatchOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    c->drawPatch(points.data.get(), colors.data.get(), texCoords.data.get(), mode,
                 fromShmemPaint(paint, serverCache));
}
std::string DrawPatchOp::toString() const {
    return "DrawPatchOp";
}

DrawPointsOp* DrawPointsOp::Create(RenderCommandBuffer* commandBuffer, SkCanvas::PointMode mode,
                                   size_t count, const SkPoint* points, const SkPaint& paint,
                                   IPCClientResourceCache* clientCache) {
    DrawPointsOp* op = commandBuffer->allocAligned<DrawPointsOp>();
    OP_REQUIRE(op);
    op->type = kType;
    op->mode = mode;
    OP_REQUIRE(toShmemPaint(commandBuffer, paint, op->paint, clientCache));
    OP_REQUIRE(SetRSpan(op->points, commandBuffer, points, count));
    return op;
}

void DrawPointsOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    c->drawPoints(mode, {points.data.get(), points.size}, fromShmemPaint(paint, serverCache));
}
std::string DrawPointsOp::toString() const {
    return "DrawPointsOp";
}

DrawVerticesOp* DrawVerticesOp::Create(RenderCommandBuffer* commandBuffer,
                                       const SkVertices* vertices, SkBlendMode mode,
                                       const SkPaint& paint, IPCClientResourceCache* clientCache) {
    IPCRENDERBUFFER_UNIMPLEMENTED;
    return nullptr;

    #if 0
    DrawVerticesOp* op = commandBuffer->allocAligned<DrawVerticesOp>();
    OP_REQUIRE(op);
    op->type = kType;
    op->mode = mode;
    OP_REQUIRE(toShmemPaint(commandBuffer, paint, op->paint, clientCache));
    SkBinaryWriteBuffer writeBuffer(SkSerialProcs{});
    vertices->priv().encode(writeBuffer);
    auto data = writeBuffer.snapshotAsData();
    OP_REQUIRE(SetRSpan<uint8_t>(op->verticesData, commandBuffer,
                                 reinterpret_cast<const uint8_t*>(data->data()), data->size()));
    return op;
    #endif
}

void DrawVerticesOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    IPCRENDERBUFFER_UNIMPLEMENTED;
    #if 0
    SkReadBuffer readBuffer(verticesData.data.get(), verticesData.size);
    sk_sp<SkVertices> vertices = SkVerticesPriv::Decode(readBuffer);
    if (vertices) {
        c->drawVertices(vertices, mode, fromShmemPaint(paint, serverCache));
    }
    #endif
}
std::string DrawVerticesOp::toString() const {
    return "DrawVerticesOp";
}

/*struct DrawMeshOp final : IPCRenderBufferOp {
  const auto kType = TYPE_DRAWMESH;
  DrawMeshOp(const Mesh& mesh, const SkPaint& paint) {
    ALOGE("Not implemented %s", __FUNCTION__);
  }
};*/

DrawSkMeshOp* DrawSkMeshOp::Create(RenderCommandBuffer* commandBuffer, const SkMesh& mesh,
                                   sk_sp<SkBlender> blender, const SkPaint& paint,
                                   IPCClientResourceCache* clientCache) {
    DrawSkMeshOp* op = commandBuffer->allocAligned<DrawSkMeshOp>();
    OP_REQUIRE(op);
    op->type = kType;
    IPCRENDERBUFFER_UNIMPLEMENTED;
    return op;
}

void DrawSkMeshOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    IPCRENDERBUFFER_UNIMPLEMENTED;
}
std::string DrawSkMeshOp::toString() const {
    return "DrawSkMeshOp";
}

DrawAtlasOp* DrawAtlasOp::Create(RenderCommandBuffer* commandBuffer, const SkImage* atlas,
                                 const SkRSXform* xform, const SkRect* tex, const SkColor* colors,
                                 int count, SkBlendMode mode, const SkSamplingOptions& sampling,
                                 const SkRect* cull, const SkPaint* paint) {
    DrawAtlasOp* op = commandBuffer->allocAligned<DrawAtlasOp>();
    OP_REQUIRE(op);
    IPCRENDERBUFFER_UNIMPLEMENTED;
    op->type = kType;
    return op;
}

void DrawAtlasOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    IPCRENDERBUFFER_UNIMPLEMENTED;
}
std::string DrawAtlasOp::toString() const {
    return "DrawAtlasOp";
}

DrawProxySurfaceControlOp* DrawProxySurfaceControlOp::Create(RenderCommandBuffer* commandBuffer,
                                                             int id) {
    DrawProxySurfaceControlOp* op = commandBuffer->allocAligned<DrawProxySurfaceControlOp>();
    OP_REQUIRE(op);
    op->type = kType;
    op->proxyId = id;
    return op;
}

void DrawProxySurfaceControlOp::draw(SkCanvas* c, const SkMatrix&,
                                     IPCServerResourceCache* serverCache) {
    LOG_ALWAYS_FATAL_IF("DrawProxySurfaceControlOp::draw unexpected");
}

std::string DrawProxySurfaceControlOp::toString() const {
    return "DrawProxySurfaceControlOp";
}

BeginRenderTargetOp* BeginRenderTargetOp::Create(RenderCommandBuffer* commandBuffer,
                                                 uint64_t bufferId) {
    BeginRenderTargetOp* op = commandBuffer->allocAligned<BeginRenderTargetOp>();
    OP_REQUIRE(op);
    op->type = kType;
    op->bufferId = bufferId;
    return op;
}

void BeginRenderTargetOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {}

std::string BeginRenderTargetOp::toString() const {
    return "BeginRenderTargetOp";
}

EndRenderTargetOp* EndRenderTargetOp::Create(RenderCommandBuffer* commandBuffer) {
    EndRenderTargetOp* op = commandBuffer->allocAligned<EndRenderTargetOp>();
    OP_REQUIRE(op);
    op->type = kType;
    return op;
}

void EndRenderTargetOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {}

std::string EndRenderTargetOp::toString() const {
    return "EndRenderTargetOp";
}

UploadBitmap* UploadBitmap::Create(IpcRenderRegion* region, uint64_t imageId,
                                   const SkBitmap& bitmap) {
    UploadBitmap* op = region->mUploadBuf.reserve<UploadBitmap>();
    OP_REQUIRE(op);
    op->type = TYPE_UPLOADBITMAP;
    op->imageId = imageId;
    op->width = bitmap.width();
    op->height = bitmap.height();
    op->colorType = (int32_t)bitmap.colorType();
    op->alphaType = (int32_t)bitmap.alphaType();
    op->rowBytes = bitmap.rowBytes();

    size_t pixelSize = bitmap.computeByteSize();
    OP_REQUIRE(SetRSpan(op->pixels, region, (const uint8_t*)bitmap.getPixels(), pixelSize));

    region->mUploadBuf.commit();
    return op;
}

void UploadBitmap::execute(IPCServerResourceCache& resourceCache) {
    SkImageInfo info =
            SkImageInfo::Make(width, height, (SkColorType)colorType, (SkAlphaType)alphaType);
    if (pixels.data.get()) {
        SkBitmap bitmap;
        if (bitmap.tryAllocPixels(info, rowBytes)) {
            memcpy(bitmap.getPixels(), pixels.data.get(), pixels.size);
            bitmap.setImmutable();
            sk_sp<SkImage> image = SkImages::RasterFromBitmap(bitmap);
            resourceCache.bitmaps[imageId] = {nullptr, image, nullptr};
        } else {
            ALOGE("Failed to allocate pixels for UploadBitmap");
        }
    }
}

std::string UploadBitmap::toString() const {
    return std::string("UploadBitmap id=") + std::to_string(imageId) + std::string(" w=") +
            std::to_string(width) + std::string(" h=") + std::to_string(height) +
            std::string(" ct=") + std::to_string(colorType) + std::string(" at=") +
            std::to_string(alphaType) + std::string(" rb=") + std::to_string(rowBytes) +
            std::string(" sz=") + std::to_string(pixels.size);
}

FreeBitmap* FreeBitmap::Create(IpcRenderRegion* region, uint64_t imageId) {
    FreeBitmap* op = region->mUploadBuf.reserve<FreeBitmap>();
    OP_REQUIRE(op);
    op->type = TYPE_FREEBITMAP;
    op->imageId = imageId;
    region->mUploadBuf.commit();
    return op;
}

void FreeBitmap::execute(IPCServerResourceCache& resourceCache) {
    resourceCache.bitmaps.erase(imageId);
}

std::string FreeBitmap::toString() const {
    return std::string("FreeBitmap id=") + std::to_string(imageId);
}

UploadTypeface* UploadTypeface::Create(IpcRenderRegion* region, uint32_t fontId,
                                       const SkData* data) {
    UploadTypeface* op = region->mUploadBuf.reserve<UploadTypeface>();
    OP_REQUIRE(op);
    op->type = TYPE_UPLOADTYPEFACE;
    op->fontId = fontId;
    OP_REQUIRE(SetRSpan(op->data, region, (const uint8_t*)data->data(), data->size()));
    region->mUploadBuf.commit();
    return op;
}

void UploadTypeface::execute(IPCServerResourceCache& resourceCache) {
    if (data.data.get() && data.size > 0) {
        SkMemoryStream stream(data.data.get(), data.size);
        sk_sp<SkTypeface> tf = SkTypeface::MakeDeserialize(&stream, nullptr);
        if (tf) {
            resourceCache.typefaces[fontId] = tf;
        } else {
            ALOGE("Failed to deserialize Typeface for font id %u", fontId);
        }
    }
}

std::string UploadTypeface::toString() const {
    return std::string("UploadTypeface id=") + std::to_string(fontId) + std::string(" size=") +
            std::to_string(data.size);
}

DrawShadowRecOp* DrawShadowRecOp::Create(RenderCommandBuffer* commandBuffer, const SkPath& path,
                                         const SkDrawShadowRec& rec) {
    DrawShadowRecOp* op = commandBuffer->allocAligned<DrawShadowRecOp>();
    OP_REQUIRE(op);
    size_t pathSize = path.writeToMemory(nullptr);
    OP_REQUIRE(SetRSpan<uint8_t>(op->pathData, commandBuffer, nullptr, pathSize));
    path.writeToMemory(op->pathData.data.get());
    op->rec = rec;
    op->type = kType;
    return op;
}

void DrawShadowRecOp::draw(SkCanvas* c, const SkMatrix&, IPCServerResourceCache* serverCache) {
    const SkPath path =
            SkPath::ReadFromMemory(pathData.data.get(), pathData.size).value_or(SkPath());
    c->private_draw_shadow_rec(path, rec);
}

std::string DrawShadowRecOp::toString() const {
    return "DrawShadowRecOp";
}

} // namespace android

#pragma clang diagnostic pop
