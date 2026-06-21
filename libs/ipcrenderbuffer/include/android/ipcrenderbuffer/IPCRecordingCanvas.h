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

// Skia headers have some bits that trigger -Wunused-parameter which is an error in some parts
// of the Android build
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include <SkCanvas.h>
#include <SkCanvasVirtualEnforcer.h>
#include <SkDrawable.h>
#include <SkNoDrawCanvas.h>
#include <SkPaint.h>
#include <SkPath.h>
#include <SkRect.h>
#include <SkRuntimeEffect.h>
#pragma clang diagnostic pop

#include <gui/RenderCommandBufferProducer.h>
#include <gui/SurfaceComposerClient.h>
#include <gui/SurfaceControl.h>
#include <log/log.h>

#include <cstdlib>
#include <map>
#include <utility>
#include <vector>

enum class SkBlendMode;
class SkRRect;

namespace android {

class Mesh;
struct IPCClientResourceCache;

class IPCRecordingCanvas : public SkCanvasVirtualEnforcer<SkNoDrawCanvas> {
public:
    explicit IPCRecordingCanvas(IPCClientResourceCache& resourceCache);

    bool canRecord();
    void startRecording();
    void endRecording();

    sk_sp<SkSurface> onNewSurface(const SkImageInfo&, const SkSurfaceProps&) override;
    void willSave() override;
    SaveLayerStrategy getSaveLayerStrategy(const SaveLayerRec&) override;
    void willRestore() override;
    bool onDoSaveBehind(const SkRect*) override;

    void didConcat44(const SkM44&) override;
    void didSetM44(const SkM44&) override;
    void didScale(SkScalar, SkScalar) override;
    void didTranslate(SkScalar, SkScalar) override;

    void onClipRect(const SkRect&, SkClipOp, ClipEdgeStyle) override;
    void onClipRRect(const SkRRect&, SkClipOp, ClipEdgeStyle) override;
    void onClipPath(const SkPath&, SkClipOp, ClipEdgeStyle) override;
    void onClipShader(sk_sp<SkShader>, SkClipOp) override;
    void onClipRegion(const SkRegion&, SkClipOp) override;
    void onResetClip() override;

    void onDrawPaint(const SkPaint&) override;
    void onDrawBehind(const SkPaint&) override;
    void onDrawPath(const SkPath&, const SkPaint&) override;
    void onDrawRect(const SkRect&, const SkPaint&) override;
    void onDrawRegion(const SkRegion&, const SkPaint&) override;
    void onDrawOval(const SkRect&, const SkPaint&) override;
    void onDrawArc(const SkRect&, SkScalar, SkScalar, bool, const SkPaint&) override;
    void onDrawRRect(const SkRRect&, const SkPaint&) override;
    void onDrawDRRect(const SkRRect&, const SkRRect&, const SkPaint&) override;

    void onDrawDrawable(SkDrawable*, const SkMatrix*) override;
    void onDrawPicture(const SkPicture*, const SkMatrix*, const SkPaint*) override;
    void onDrawAnnotation(const SkRect&, const char[], SkData*) override;

    void onDrawTextBlob(const SkTextBlob*, SkScalar, SkScalar, const SkPaint&) override;

    void onDrawImage2(const SkImage*, SkScalar, SkScalar, const SkSamplingOptions&,
                      const SkPaint*) override;
    void onDrawImageLattice2(const SkImage*, const Lattice&, const SkRect&, SkFilterMode,
                             const SkPaint*) override;
    void onDrawImageRect2(const SkImage*, const SkRect&, const SkRect&, const SkSamplingOptions&,
                          const SkPaint*, SrcRectConstraint) override;

    void onDrawPatch(const SkPoint[12], const SkColor[4], const SkPoint[4], SkBlendMode,
                     const SkPaint&) override;
    void onDrawPoints(PointMode, size_t count, const SkPoint pts[], const SkPaint&) override;
    void onDrawVerticesObject(const SkVertices*, SkBlendMode, const SkPaint&) override;
    void onDrawMesh(const SkMesh&, sk_sp<SkBlender>, const SkPaint&) override;
    void onDrawAtlas2(const SkImage*, const SkRSXform[], const SkRect[], const SkColor[], int,
                      SkBlendMode, const SkSamplingOptions&, const SkRect*,
                      const SkPaint*) override;
    void onDrawShadowRec(const SkPath&, const SkDrawShadowRec&) override;

    //    void onDrawProxySurfaceControl(int id) override;

    void beginRenderTarget(uint64_t bufferId);
    void endRenderTarget();

    void storeSize(int width, int height);

    std::shared_ptr<RenderCommandBufferProducer> getRenderCommandBufferProducer() {
        return mRenderCommandBufferProducer;
    }

    RenderCommandBuffer* getRenderCommandBuffer() { return mCurrentRenderCommandBuffer; }

private:
    typedef SkCanvasVirtualEnforcer<SkNoDrawCanvas> INHERITED;
    std::shared_ptr<RenderCommandBufferProducer> mRenderCommandBufferProducer;
    IPCClientResourceCache& mResourceCache;
    RenderCommandBuffer* mCurrentRenderCommandBuffer = nullptr;

    int mWidth = 0;
    int mHeight = 0;
};

} // namespace android
