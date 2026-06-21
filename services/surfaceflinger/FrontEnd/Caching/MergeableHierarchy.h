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

#pragma once

#include <utils/Timers.h>
#include <memory>
#include <vector>
#include "FrontEnd/LayerSnapshotBuilder.h"
#include "compositionengine/CompositionEngine.h"

namespace android::surfaceflinger::frontend {

class LayerHierarchy;
struct LayerSnapshot;

namespace caching {

// Represents a set of LayerHierarchies that, combined, can form a new set of LayerHierarchies that
// is visually equivalent to the first set of LayerHierarchies, such that if the new set of
// LayerHierarchies replaced the old set, the overall layer graph would be simpler.
//
// The MergeableHierarchy is conceptually owned by exactly one LayerHierarchy. If that
// LayerHierarchy is destroyed, then the entire MergeableHierarchy must also be destroyed. If any
// other LayerHierarchy composing the MergeableHierarchy is destroyed or mutated, then the
// MergeableHierarchy should also be invalidated.
class MergeableHierarchy {
public:
    // Useful state of a hierarchy
    struct HierarchyState {
        uint32_t layerId;
        const LayerHierarchy* hierarchy;
        nsecs_t lastUpdateTime;

        bool operator==(const HierarchyState&) const = default;
    };

    MergeableHierarchy(HierarchyState state) : mRoot(state) {}

    uint32_t getId() const { return mRoot.layerId; }

    void constructSnapshot(LayerSnapshotBuilder& builder, const LayerSnapshotBuilder::Args& args,
                           compositionengine::CompositionEngine& compositionEngine,
                           std::unordered_map<uint32_t, sp<Layer>>& legacyLayers);

    std::unique_ptr<LayerSnapshot> getSnapshotCopy() const {
        if (!mSnapshot) {
            return nullptr;
        }

        return std::make_unique<LayerSnapshot>(*mSnapshot);
    };

    void dump(std::ostream& out) const;

    bool operator==(const MergeableHierarchy& other) const { return mRoot == other.mRoot; }

private:
    HierarchyState mRoot;
    std::unique_ptr<LayerSnapshot> mSnapshot = nullptr;
};

} // namespace caching
} // namespace android::surfaceflinger::frontend