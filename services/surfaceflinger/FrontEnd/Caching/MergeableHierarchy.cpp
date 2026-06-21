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

#include <FrontEnd/LayerHierarchy.h>
#include <iterator>
#include <optional>
#include "FrontEnd/LayerSnapshot.h"
#include "Layer.h"
#include "LayerFE.h"
#include "ScreenCaptureOutput.h"
#include "renderengine/impl/ExternalTexture.h"

#include "MergeableHierarchy.h"

namespace android::surfaceflinger::frontend::caching {

void MergeableHierarchy::constructSnapshot(LayerSnapshotBuilder& builder,
                                           const LayerSnapshotBuilder::Args& args,
                                           compositionengine::CompositionEngine& compositionEngine,
                                           std::unordered_map<uint32_t, sp<Layer>>& legacyLayers) {
    SFTRACE_CALL();

    if (mSnapshot) {
        return;
    }

    if (!mRoot.hierarchy || !mRoot.hierarchy->getLayer()) {
        return;
    }

    auto layer = mRoot.hierarchy->getLayer();

    auto localArgs = args;
    localArgs.forceUpdate = LayerSnapshotBuilder::ForceUpdateFlags::ALL;
    localArgs.root = *mRoot.hierarchy;
    auto cropRect = Rect(layer->getCroppedBufferSize(layer->getBufferSize(0)));

    auto bounds = Rect(cropRect).offsetToOrigin();

    if (!bounds.isEmpty()) {
        localArgs.parentCrop = bounds.toFloatRect();
    } else {
        localArgs.parentCrop = std::nullopt;
    }

    builder.update(localArgs);

    frontend::LayerSnapshot* rootSnapshot = builder.getSnapshot(layer->id);

    auto transform = rootSnapshot->localTransform.inverse();
    std::vector<std::pair<Layer*, sp<LayerFE>>> layers;

    auto debugName = std::format("flattenedHierarchy{}", getId());
    Rect snapshotBounds;
    builder.forEachVisibleSnapshot([&](std::unique_ptr<frontend::LayerSnapshot>& snapshot) {
        if (!snapshot->hasSomethingToDraw()) {
            return;
        }

        if (bounds.isEmpty() && !snapshot->croppedBufferSize.isEmpty()) {
            if (!snapshotBounds.isValid()) {
                snapshotBounds = Rect(snapshot->transformedBounds);
            } else {
                snapshotBounds.left =
                        std::min(snapshotBounds.left,
                                 static_cast<int32_t>(snapshot->transformedBounds.left));
                snapshotBounds.top =
                        std::min(snapshotBounds.top,
                                 static_cast<int32_t>(snapshot->transformedBounds.top));
                snapshotBounds.right =
                        std::max(snapshotBounds.right,
                                 static_cast<int32_t>(snapshot->transformedBounds.right));
                snapshotBounds.bottom =
                        std::max(snapshotBounds.bottom,
                                 static_cast<int32_t>(snapshot->transformedBounds.bottom));
            }
        }

        auto it = legacyLayers.find(static_cast<uint32_t>(snapshot->sequence));
        Layer* legacyLayer = (it == legacyLayers.end()) ? nullptr : it->second.get();
        sp<LayerFE> layerFE = sp<LayerFE>::make(snapshot->name);
        layerFE->mSnapshot = std::make_unique<frontend::LayerSnapshot>(*snapshot);
        layerFE->mSnapshot->geomLayerTransform = transform * layerFE->mSnapshot->geomLayerTransform;
        layerFE->mSnapshot->geomInverseLayerTransform =
                layerFE->mSnapshot->geomLayerTransform.inverse();
        layers.emplace_back(legacyLayer, std::move(layerFE));
    });

    if (layers.empty()) {
        mSnapshot = nullptr;
        return;
    }

    for (auto& [layer, layerFE] : layers) {
        ftl::Future<FenceResult> futureFence = layerFE->createReleaseFenceFuture();
        if (layer) {
            layer->prepareReleaseCallbacks(std::move(futureFence), ui::UNASSIGNED_LAYER_STACK);
        }
    }

    if (!bounds.isEmpty()) {
        snapshotBounds = bounds;
    }

    if (snapshotBounds.getWidth() <= 0 || snapshotBounds.getHeight() <= 0) {
        mSnapshot = nullptr;
        return;
    }

    auto width = static_cast<uint32_t>(snapshotBounds.getWidth());
    auto height = static_cast<uint32_t>(snapshotBounds.getHeight());

    auto buffer = sp<GraphicBuffer>::make(width, height, PIXEL_FORMAT_RGBA_8888, 1u,

                                          static_cast<uint64_t>(GRALLOC_USAGE_HW_COMPOSER |
                                                                GRALLOC_USAGE_HW_RENDER |
                                                                GRALLOC_USAGE_HW_TEXTURE),
                                          debugName);
    auto status = buffer->initCheck();

    if (status != OK) {
        mSnapshot = nullptr;
        return;
    }

    auto texture = std::make_shared<
            renderengine::impl::
                    ExternalTexture>(buffer, compositionEngine.getRenderEngine(),
                                     renderengine::impl::ExternalTexture::Usage::WRITEABLE |
                                             renderengine::impl::ExternalTexture::Usage::READABLE);

    auto layerStack = layers.front().second->mSnapshot->outputFilter.layerStack;

    std::shared_ptr<ScreenCaptureOutput> output = createScreenCaptureOutput(
            ScreenCaptureOutputArgs{.compositionEngine = compositionEngine,
                                    .colorProfile = {},
                                    .layerStack = layerStack,
                                    .sourceCrop = snapshotBounds,
                                    .buffer = texture,
                                    .displayIdVariant = std::nullopt,
                                    .reqBufferSize = ui::Size(width, height),
                                    .sdrWhitePointNits = -1,
                                    .displayBrightnessNits = -1,
                                    .targetBrightness = -1,
                                    .layerAlpha = 0.0f,
                                    .disableBlur = false,
                                    .treat170mAsSrgb = false,
                                    .dimInGammaSpaceForEnhancedScreenshots = false,
                                    .isSecure = true,
                                    .enableLocalTonemapping = false,
                                    .debugName = debugName});

    std::vector<sp<compositionengine::LayerFE>> layerFes;
    layerFes.reserve(layers.size());
    for (auto& [layer, layerFE] : layers) {
        layerFes.emplace_back(layerFE);
    }

    sp<LayerFE> firstLayer = layers.front().second;

    compositionengine::CompositionRefreshArgs refreshArgs{
            .outputs = {output},
            .layers = std::move(layerFes),
            .updatingOutputGeometryThisFrame = true,
            .updatingGeometryThisFrame = true,
    };
    compositionEngine.present(refreshArgs);

    mSnapshot = std::make_unique<LayerSnapshot>(*rootSnapshot);
    mSnapshot->externalTexture = texture;
    mSnapshot->acquireFence = output->getRenderSurface()->getClientTargetAcquireFence();
    mSnapshot->buffer = texture->getBuffer();
    mSnapshot->name.append(" (flattened)");
    mSnapshot->debugName.append(" (flattened)");
}

void MergeableHierarchy::dump(std::ostream& out) const {
    out << "{id = " << getId() << ", hasSnapshot=" << (mSnapshot != nullptr) << ", Last updated: "
        << std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::nanoseconds(systemTime() - mRoot.lastUpdateTime))
                    .count()
        << " ms Ago}";
}

} // namespace android::surfaceflinger::frontend::caching