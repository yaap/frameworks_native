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
#include "FrontEnd/Caching/MergeableHierarchy.h"

#include "MergeableHierarchyManager.h"

namespace android::surfaceflinger::frontend::caching {

void MergeableHierarchyManager::update(const LayerHierarchy& hierarchy) {
    std::vector<HierarchyEntry> topHierarchies;
    std::vector<HierarchyEntry> dependentHierarchies;
    update(&hierarchy, topHierarchies, dependentHierarchies);
    mMergeableHierarchies.clear();
    mMergeableHierarchies.insert(mMergeableHierarchies.end(),
                                 std::make_move_iterator(topHierarchies.begin()),
                                 std::make_move_iterator(topHierarchies.end()));
    mMergeableHierarchies.insert(mMergeableHierarchies.end(),
                                 std::make_move_iterator(dependentHierarchies.begin()),
                                 std::make_move_iterator(dependentHierarchies.end()));
}

MergeableHierarchyManager::UpdatePayload MergeableHierarchyManager::update(
        const LayerHierarchy* hierarchy, std::vector<HierarchyEntry>& topHierarchies,
        std::vector<HierarchyEntry>& dependentHierarchies) {
    nsecs_t lastUpdateTime = 0;
    int32_t numInterestingChildren = 0;
    std::vector<HierarchyEntry> descendentHierarchies;
    for (auto& [childHierarchy, _] : hierarchy->mChildren) {
        auto updatePayload = update(childHierarchy, descendentHierarchies, dependentHierarchies);

        if (updatePayload.containsMergedHierarchy) {
            numInterestingChildren++;
        }

        lastUpdateTime = std::max(lastUpdateTime, updatePayload.lastUpdateTimeIncludingChilderen);
    }

    if (hierarchy->getLayer() != nullptr) {
        lastUpdateTime = std::max(lastUpdateTime, hierarchy->getLayer()->lastUpdateTime);
    }

    if (lastUpdateTime > 0 && canAddNode(hierarchy, lastUpdateTime)) {
        // Don't generate equivalent hierarchies that aren't interesting.
        // So check that the root node actually has stuff to draw, or that there's multiple children
        // that also can be updated.
        if (hierarchy->getLayer()->hasSomethingToDraw() || numInterestingChildren > 1) {
            auto newHierarchy =
                    createOrReuseHierarchy({hierarchy->getLayer()->id, hierarchy, lastUpdateTime});

            for (auto& descendentHierarchy : descendentHierarchies) {
                descendentHierarchy.isTop = false;
            }

            dependentHierarchies.insert(dependentHierarchies.end(),
                                        std::make_move_iterator(descendentHierarchies.begin()),
                                        std::make_move_iterator(descendentHierarchies.end()));

            topHierarchies.emplace_back(HierarchyEntry{std::move(newHierarchy), true});
            return {lastUpdateTime, true};
        }
    }

    topHierarchies.insert(topHierarchies.end(),
                          std::make_move_iterator(descendentHierarchies.begin()),
                          std::make_move_iterator(descendentHierarchies.end()));

    return {lastUpdateTime, false};
}

void MergeableHierarchyManager::constructSnapshots(
        LayerSnapshotBuilder& builder, const LayerSnapshotBuilder::Args& args,
        compositionengine::CompositionEngine& compositionEngine,
        std::unordered_map<uint32_t, sp<Layer>>& legacyLayers) {
    for (auto& hierarchy : mMergeableHierarchies) {
        hierarchy.hierarchy.constructSnapshot(builder, args, compositionEngine, legacyLayers);
    }
}

} // namespace android::surfaceflinger::frontend::caching