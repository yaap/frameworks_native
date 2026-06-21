/*
 * Copyright 2026 The Android Open Source Project
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <com_android_graphics_surfaceflinger_flags.h>
#include <common/test/FlagUtils.h>

#include "FrontEnd/LayerSnapshotBuilder.h"
#include "LayerHierarchyTest.h"

namespace android::surfaceflinger::frontend {

using namespace com::android::graphics::surfaceflinger;

class LayerSnapshotMirrorWithCropTest : public LayerSnapshotTestBase {
protected:
    SET_FLAG_FOR_TEST(flags::mirror_with_crop, true);

    LayerSnapshotMirrorWithCropTest() : LayerSnapshotTestBase() {
    }

    void mirrorLayerWithCrop(uint32_t id, uint32_t parentId, uint32_t layerIdToMirror,
                               uint32_t cropLayerId) {
        LayerCreationArgs args =
                createArgs(/*id=*/id, /*canBeRoot=*/false, /*parent=*/parentId,
                           /*mirror=*/layerIdToMirror);
        args.croppedByLayerId = cropLayerId;
        std::vector<std::unique_ptr<RequestedLayerState>> layers;
        layers.emplace_back(std::make_unique<RequestedLayerState>(args));
        mLifecycleManager.addLayers(std::move(layers));
    }

    LayerSnapshot* getSnapshot(uint32_t layerId) { return mSnapshotBuilder.getSnapshot(layerId); }

    LayerSnapshotBuilder mSnapshotBuilder;
};

TEST_F(LayerSnapshotMirrorWithCropTest, mirrorCrop) {
    // Use existing hierarchy setup from LayerHierarchyTestBase. Assignment as follows:
    //
    // ROOT
    // ├── 1 (parent)
    // │   ├── 11 (mirror source)
    // │   │   └── 111
    // │   └── 12 (crop)
    // └── 2 (mirror destination parent)
    //     └── 21 (mirror layer)

    // Source layer 11 is at (50, 50)
    setPosition(11, 50, 50);
    // Crop layer 12 is at (100, 100) with size (100, 100)
    setPosition(12, 100, 100);
    setCrop(12, Rect(0, 0, 100, 100));

    mirrorLayerWithCrop(21, 2, 11, 12);

    update(mSnapshotBuilder);

    // Crop in global space: (100, 100) to (200, 200)
    // Source in global space: (50, 50) onwards
    // Mirror source relative to crop: source is at (-50, -50) relative to crop.
    auto* snapshot = getSnapshot(21);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot->mirrorCrop, FloatRect(50, 50, 150, 150));
    EXPECT_EQ(snapshot->geomLayerCrop, FloatRect(50, 50, 150, 150));
}

TEST_F(LayerSnapshotMirrorWithCropTest, mirrorCropWithScale) {
    // Source layer 11 is at (0, 0)
    // Crop layer 12 is at (100, 100) with size (100, 100), but scaled by 2
    setPosition(12, 100, 100);
    setMatrix(12, 2.0f, 0, 0, 2.0f);
    setCrop(12, Rect(0, 0, 100, 100));

    mirrorLayerWithCrop(21, 2, 11, 12);

    update(mSnapshotBuilder);

    // geomLayerTransform for 11: Identity
    // geomLayerTransform for 12: translate(100, 100) * scale(2, 2)
    // transform = Identity * translate(100, 100) * scale(2, 2) = translate(100, 100) * scale(2, 2)
    // cropSnapshot->croppedBufferSize for 12 is (100, 100)
    // crop = translate(100, 100) * scale(2, 2).transform(Rect(0, 0, 100, 100))
    //      = translate(100, 100).transform(Rect(0, 0, 200, 200))
    //      = Rect(100, 100, 300, 300)
    // mirrorOffset.x = -100, mirrorOffset.y = -100
    // normalizedCrop = (0, 0, 200, 200)
    auto* snapshot = getSnapshot(21);

    ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot->mirrorCrop, FloatRect(100, 100, 300, 300));
    EXPECT_EQ(snapshot->geomLayerCrop, FloatRect(100, 100, 300, 300));
}

TEST_F(LayerSnapshotMirrorWithCropTest, mirrorCropWithInvalidTransform) {
    // Source layer 11 is at (50, 50) but has invalid transform (scale 0)
    setPosition(11, 50, 50);
    setMatrix(11, 0.f, 0.f, 0.f, 0.f);

    // Crop layer 12 is at (100, 100) with size (100, 100)
    setPosition(12, 100, 100);
    setCrop(12, Rect(0, 0, 100, 100));

    mirrorLayerWithCrop(21, 2, 11, 12);

    update(mSnapshotBuilder);

    auto* snapshot = getSnapshot(21);
    ASSERT_NE(snapshot, nullptr);

    // Should be skipped in updateMirrorLayerCrops, so defaults to 0
    EXPECT_TRUE(snapshot->mirrorCrop->isEmpty());
}

TEST_F(LayerSnapshotMirrorWithCropTest, noChangesEarlyReturn) {
    // Source layer 11 is at (50, 50)
    setPosition(11, 50, 50);
    // Crop layer 12 is at (100, 100) with size (100, 100)
    setPosition(12, 100, 100);
    setCrop(12, Rect(0, 0, 100, 100));

    mirrorLayerWithCrop(21, 2, 11, 12);
    update(mSnapshotBuilder);

    // Verify initial values
    auto* snapshot = getSnapshot(21);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot->mirrorCrop, FloatRect(50, 50, 150, 150));
    EXPECT_EQ(snapshot->geomLayerCrop, FloatRect(50, 50, 150, 150));

    // After update, mLifecycleManager has no global changes.
    // Use ForceUpdateFlags::HIERARCHY to trigger updateSnapshots but hit the early return in
    // updateMirrorLayerCrops.
    LayerSnapshotBuilder::Args args{.root = mHierarchyBuilder.getHierarchy(),
                                    .layerLifecycleManager = mLifecycleManager,
                                    .forceUpdate =
                                        LayerSnapshotBuilder::ForceUpdateFlags::HIERARCHY,
                                    .includeMetadata = false,
                                    .displays = mFrontEndDisplayInfos,
                                    .displayChanges = false,
                                    .globalShadowSettings = globalShadowSettings,
                                    .supportsBlur = true,
                                    .supportedLayerGenericMetadata = {},
                                    .genericLayerMetadataKeyMap = {}};

    // Ensure no global changes are present that affect crop
    ASSERT_FALSE(mLifecycleManager.getGlobalChanges().any(
        RequestedLayerState::Changes::Geometry |
        RequestedLayerState::Changes::Hierarchy |
        RequestedLayerState::Changes::Created |
        RequestedLayerState::Changes::Visibility));

    mSnapshotBuilder.update(args);
    // This should trigger Line 1723 in LayerSnapshotBuilder.cpp

    snapshot = getSnapshot(21);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot->mirrorCrop, FloatRect(50, 50, 150, 150));
    EXPECT_EQ(snapshot->geomLayerCrop, FloatRect(50, 50, 150, 150));
}

TEST_F(LayerSnapshotMirrorWithCropTest, updateWithRootLayerAndStopLayers) {
    // Source layer 11 is at (50, 50)
    setPosition(11, 50, 50);
    // Crop layer 12 is at (100, 100) with size (100, 100)
    setPosition(12, 100, 100);
    setCrop(12, Rect(0, 0, 100, 100));

    mirrorLayerWithCrop(21, 2, 11, 12);
    // Set a stop layer to cover applyStopLayers. Layer 1 says stop at 12.
    setStopLayer(1, 12);
    update(mSnapshotBuilder);

    // Change crop layer position so that updateMirrorLayerCrops returns true
    setPosition(12, 110, 110);
    mHierarchyBuilder.update(mLifecycleManager);

    auto& rootHierarchy = mHierarchyBuilder.getHierarchy();
    // find hierarchy for layer 1
    auto it = std::find_if(rootHierarchy.mChildren.begin(), rootHierarchy.mChildren.end(),
                           [](auto& child) { return child.first->getLayer()->id == 1; });
    ASSERT_NE(it, rootHierarchy.mChildren.end());
    LayerHierarchy* layer1Hierarchy = it->first;

    // Use root layer in Args to trigger Line 471 in LayerSnapshotBuilder.cpp
    LayerSnapshotBuilder::Args args{.root = *layer1Hierarchy,
                                    .layerLifecycleManager = mLifecycleManager,
                                    .includeMetadata = false,
                                    .displays = mFrontEndDisplayInfos,
                                    .displayChanges = false,
                                    .globalShadowSettings = globalShadowSettings,
                                    .supportsBlur = true,
                                    .supportedLayerGenericMetadata = {},
                                    .genericLayerMetadataKeyMap = {}};
    mSnapshotBuilder.update(args);

    // Verify mirror offset updated
    auto* snapshot21 = getSnapshot(21);
    ASSERT_NE(snapshot21, nullptr);
    EXPECT_EQ(snapshot21->mirrorCrop, FloatRect(60, 60, 160, 160));

    // Verify stop layer logic: 12 and 13 should be hidden.
    // 11 is before 12 in Z order (default).
    auto* snapshot11 = getSnapshot(11);
    auto* snapshot12 = getSnapshot(12);
    auto* snapshot13 = getSnapshot(13);
    ASSERT_NE(snapshot11, nullptr);
    ASSERT_NE(snapshot12, nullptr);
    ASSERT_NE(snapshot13, nullptr);

    EXPECT_FALSE(snapshot11->isHiddenByPolicyFromParent);
    EXPECT_TRUE(snapshot12->isHiddenByPolicyFromParent);
    EXPECT_TRUE(snapshot13->isHiddenByPolicyFromParent);
}

TEST_F(LayerSnapshotMirrorWithCropTest, cropByLayerDestroyed) {
    setPosition(11, 50, 50);
    setPosition(12, 100, 100);
    setCrop(12, Rect(0, 0, 100, 100));

    mirrorLayerWithCrop(21, 2, 11, 12);

    update(mSnapshotBuilder);

    auto* snapshot = getSnapshot(21);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot->mirrorCrop, FloatRect(50, 50, 150, 150));

    // Destroy crop layer 12
    reparentLayer(12, UNASSIGNED_LAYER_ID);
    destroyLayerHandle(12);

    update(mSnapshotBuilder);

    EXPECT_EQ(getSnapshot(12), nullptr);

    snapshot = getSnapshot(21);
    ASSERT_NE(snapshot, nullptr);
    // These should be reset
    EXPECT_FALSE(snapshot->mirrorCrop.has_value());
}

TEST_F(LayerSnapshotMirrorWithCropTest, mirrorFromLayerDestroyed) {
    setPosition(11, 50, 50);
    setPosition(12, 100, 100);
    setCrop(12, Rect(0, 0, 100, 100));

    mirrorLayerWithCrop(21, 2, 11, 12);

    update(mSnapshotBuilder);

    auto* snapshot = getSnapshot(21);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot->mirrorCrop, FloatRect(50, 50, 150, 150));

    // Destroy mirror source layer 11
    reparentLayer(11, UNASSIGNED_LAYER_ID);
    destroyLayerHandle(11);

    update(mSnapshotBuilder);

    EXPECT_EQ(getSnapshot(11), nullptr);

    snapshot = getSnapshot(21);
    ASSERT_NE(snapshot, nullptr);
    // These should be reset
    EXPECT_FALSE(snapshot->mirrorCrop.has_value());
}

TEST_F(LayerSnapshotMirrorWithCropTest, mirrorLayerDestroyed) {
    setPosition(11, 50, 50);
    setPosition(12, 100, 100);
    setCrop(12, Rect(0, 0, 100, 100));

    mirrorLayerWithCrop(21, 2, 11, 12);

    update(mSnapshotBuilder);

    // Destroy mirror layer 21
    reparentLayer(21, UNASSIGNED_LAYER_ID);
    destroyLayerHandle(21);

    update(mSnapshotBuilder);

    EXPECT_EQ(getSnapshot(21), nullptr);
}

TEST_F(LayerSnapshotMirrorWithCropTest, mirrorCropWithRotationOnSourceLayer) {
    // Source layer 11 is at (50, 50) AND Rotated 90 degrees (conceptually).
    setPosition(11, 50, 50);
    // Observed behavior: setMatrix(0, 1, -1, 0) results in [0][1]=1 (Rot270 matrix)
    // but produces Rot90-like bounds in other tests. Aligning to [0][1]=1.
    setMatrix(11, 0, 1, -1, 0);

    // Crop layer 12 is at (100, 100) with size (100, 100)
    setPosition(12, 100, 100);
    setCrop(12, Rect(0, 0, 100, 100));

    mirrorLayerWithCrop(21, 2, 11, 12);

    update(mSnapshotBuilder);

    // Find the clone snapshot (child of 21)
    LayerSnapshot* cloneSnapshot = nullptr;
    for (const auto& snap : mSnapshotBuilder.getSnapshots()) {
        if (snap->path.id == 11 && snap->path.isClone()) {
            cloneSnapshot = snap.get();
            break;
        }
    }
    ASSERT_NE(cloneSnapshot, nullptr);

    // We do not expect rotation to be applied since we are mirroring contents of the layer,
    // detached from any transform.
    EXPECT_FALSE(cloneSnapshot->localTransform.getType() & ui::Transform::ROTATE);

    // Verify the CLONE does not retain rotation.
    EXPECT_NEAR(cloneSnapshot->localTransform[0][0], 1.f, 0.001f);
    EXPECT_NEAR(cloneSnapshot->localTransform[0][1], 0.f, 0.001f);
    EXPECT_NEAR(cloneSnapshot->localTransform[1][0], 0.f, 0.001f);
    EXPECT_NEAR(cloneSnapshot->localTransform[1][1], 1.f, 0.001f);

    EXPECT_EQ(cloneSnapshot->localTransform.tx(), 0.f);
    EXPECT_EQ(cloneSnapshot->localTransform.ty(), 0.f);
}

TEST_F(LayerSnapshotMirrorWithCropTest, mirrorCropWithRotationOnCropLayer) {
    // Source layer 11 is at (0, 0)
    setPosition(11, 0, 0);
    // Crop layer 12 is at (100, 100) with size (100, 100)
    setPosition(12, 100, 100);
    // Rotate crop layer.
    // Observed behavior: setMatrix(0, 1, -1, 0) results in Rot90-like bounds.
    setMatrix(12, 0, 1, -1, 0);
    setCrop(12, Rect(0, 0, 100, 50));

    mirrorLayerWithCrop(21, 2, 11, 12);

    update(mSnapshotBuilder);

    auto* snapshot = getSnapshot(21);
    ASSERT_NE(snapshot, nullptr);

    // Bounding box of rotated crop
    // Observed behavior from failures: FloatRect(50, 100, 100, 200).
    EXPECT_EQ(snapshot->mirrorCrop, FloatRect(50, 100, 100, 200));
}

class LayerSnapshotMirrorWithCropDisabledTest : public LayerSnapshotTestBase {
protected:
    SET_FLAG_FOR_TEST(flags::mirror_with_crop, false);

    LayerSnapshotMirrorWithCropDisabledTest() : LayerSnapshotTestBase() {}

    LayerSnapshotBuilder mSnapshotBuilder;

    LayerSnapshot* getSnapshot(uint32_t layerId) { return mSnapshotBuilder.getSnapshot(layerId); }
};

TEST_F(LayerSnapshotMirrorWithCropDisabledTest, flagDisabledEarlyReturn) {
    LayerCreationArgs args = createArgs(/*id=*/21, /*canBeRoot=*/false, /*parent=*/2,
                                        /*mirror=*/11);
    args.croppedByLayerId = 12;
    std::vector<std::unique_ptr<RequestedLayerState>> layers;
    layers.emplace_back(std::make_unique<RequestedLayerState>(args));
    mLifecycleManager.addLayers(std::move(layers));

    mHierarchyBuilder.update(mLifecycleManager);
    LayerSnapshotBuilder::Args snapshotArgs{.root = mHierarchyBuilder.getHierarchy(),
                                            .layerLifecycleManager = mLifecycleManager,
                                            .includeMetadata = false,
                                            .displays = mFrontEndDisplayInfos,
                                            .displayChanges = true,
                                            .globalShadowSettings = globalShadowSettings,
                                            .supportsBlur = true,
                                            .supportedLayerGenericMetadata = {},
                                            .genericLayerMetadataKeyMap = {}};
    mSnapshotBuilder.update(snapshotArgs);

    auto* snapshot = getSnapshot(21);
    ASSERT_NE(snapshot, nullptr);
    EXPECT_FALSE(snapshot->mirrorCrop.has_value());
}

} // namespace android::surfaceflinger::frontend
