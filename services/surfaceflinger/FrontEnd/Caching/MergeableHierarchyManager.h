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

#include <chrono>
#include <memory>
#include "FrontEnd/Caching/MergeableHierarchy.h"
#include "compositionengine/CompositionEngine.h"

namespace android::surfaceflinger::frontend {

namespace caching {
using namespace std::chrono_literals;

// Manages the lifecycle of MergeableHierarchies constructed from the layer graph
class MergeableHierarchyManager {
public:
    // Collection of tunables which are backed by sysprops
    struct Tunables {
        // Tunables that are specific to scheduling when a mergeable hierarchy should be rendered
        struct RenderScheduling {
            // This default assumes that rendering a mergeable hierarchy takes about 3ms. That time
            // is then cut in half - the next frame using the mergeable hierarchy would have the
            // same workload, meaning that composition cost is the same. This is best illustrated
            // with the following example:
            //
            // Suppose we're at a 120hz cadence so SurfaceFlinger is budgeted 8.3ms per-frame. If
            // renderMergeableHierarchies costs 3ms, then two consecutive frames have timings:
            //
            // First frame: Start at 0ms, end at 6.8ms.
            // renderMergeableHierarchies: Start at 6.8ms, end at 9.8ms.
            // Second frame: Start at 9.8ms, end at 16.6ms.
            //
            // Now the second frame won't render a mergeable hierarchy afterwards, but the first
            // frame didn't really steal time from the second frame.
            static const constexpr std::chrono::nanoseconds
                    kDefaultMergeableHierarchyRenderDuration = 1500us;

            static const constexpr size_t kDefaultMaxDeferRenderAttempts = 240;

            // Duration allocated for rendering a mergeable hierarchy. If we don't have enough time
            // for rendering a mergeable hierarchy, then rendering is deferred to another frame.
            const std::chrono::nanoseconds mergeableHierarchyRenderDuration;
            // Maximum of times that we defer rendering a mergeable hierarchy. If we defer rendering
            // a mergeable hierarchy too many times, then render it anyways so that future frames
            // would benefit from the flattened mergeable hierarchy.
            const size_t maxDeferRenderAttempts;
        };

        static const constexpr std::chrono::milliseconds kDefaultActiveLayerTimeout = 150ms;

        static const constexpr bool kDefaultEnableHolePunch = true;

        // Threshold for determing whether a layer is active. A layer whose properties, including
        // the buffer, have not changed in at least this time is considered inactive and is
        // therefore a candidate for flattening.
        const std::chrono::milliseconds mActiveLayerTimeout;

        // Toggles for scheduling when it's safe to render a mergeable hierarchy.
        // See: RenderScheduling
        const std::optional<RenderScheduling> mRenderScheduling;

        // True if the hole punching feature should be enabled.
        const bool mEnableHolePunch;
    };

    void update(const LayerHierarchy& hierarchy);

    bool canAddNode(const LayerHierarchy* hierarchy, nsecs_t lastUpdateTime) {
        if (!hierarchy->getLayer()) {
            return false;
        }

        return systemTime(SYSTEM_TIME_MONOTONIC) - lastUpdateTime >
                std::chrono::nanoseconds(Tunables::kDefaultActiveLayerTimeout).count();
    }

    void constructSnapshots(LayerSnapshotBuilder& builder, const LayerSnapshotBuilder::Args& args,
                            compositionengine::CompositionEngine& compositionEngine,
                            std::unordered_map<uint32_t, sp<Layer>>& legacyLayers);

    std::unique_ptr<LayerSnapshot> findLayerSnapshotCopy(uint32_t id) {
        auto hierarchy = findTopHierarchy(id);

        if (hierarchy == mMergeableHierarchies.end()) {
            return nullptr;
        }

        return hierarchy->hierarchy.getSnapshotCopy();
    }

    bool isMemberOfAnyHierarchy(uint32_t id) const {
        return findHierarchy(id) != mMergeableHierarchies.end();
    }

    // Dumps all tracked MergeableHiearchies to a string
    std::string dump() const {
        std::ostringstream os;
        dump(os);
        return os.str();
    }

    // Dumps all tracked MergeableHiearchies to the supplied ostream
    void dump(std::ostream& out) const {
        out << "\nMergeable Hierarchies\n";
        for (const auto& mergeableHierarchy : mMergeableHierarchies) {
            out << "  ";
            mergeableHierarchy.dump(out);
            out << "\n";
        }
    }

private:
    struct HierarchyEntry;

    struct UpdatePayload {
        nsecs_t lastUpdateTimeIncludingChilderen;
        bool containsMergedHierarchy;
    };
    UpdatePayload update(const LayerHierarchy* hierarchy,
                         std::vector<HierarchyEntry>& topHierarchies,
                         std::vector<HierarchyEntry>& dependentHierarchies);

    std::vector<HierarchyEntry>::iterator findHierarchy(uint32_t id) {
        return std::find_if(mMergeableHierarchies.begin(), mMergeableHierarchies.end(),
                            [id](const auto& hierarchy) {
                                return hierarchy.hierarchy.getId() == id;
                            });
    }

    std::vector<HierarchyEntry>::const_iterator findHierarchy(uint32_t id) const {
        return std::find_if(mMergeableHierarchies.cbegin(), mMergeableHierarchies.cend(),
                            [id](const auto& hierarchy) {
                                return hierarchy.hierarchy.getId() == id;
                            });
    }

    std::vector<HierarchyEntry>::iterator findTopHierarchy(uint32_t id) {
        return std::find_if(mMergeableHierarchies.begin(), mMergeableHierarchies.end(),
                            [id](const auto& hierarchy) {
                                return hierarchy.hierarchy.getId() == id && hierarchy.isTop;
                            });
    }

    std::vector<HierarchyEntry>::iterator findPreexistingHierarchy(
            const MergeableHierarchy& mergeableHierarchy) {
        auto candidate = findHierarchy(mergeableHierarchy.getId());

        if (candidate == mMergeableHierarchies.end()) {
            return mMergeableHierarchies.end();
        }

        if (candidate->hierarchy != mergeableHierarchy) {
            return mMergeableHierarchies.end();
        }

        return candidate;
    }

    MergeableHierarchy createOrReuseHierarchy(MergeableHierarchy::HierarchyState newState) {
        auto candidate = MergeableHierarchy(newState);

        auto preexistingHierarchy = findPreexistingHierarchy(candidate);

        if (preexistingHierarchy != mMergeableHierarchies.end()) {
            return std::move(preexistingHierarchy->hierarchy);
        } else {
            return candidate;
        }
    }

    // TODO: use a better data structure for this. Conceptually we want a set of sets
    // so that destroying a LayerHierarchy won't cause a linear time search.
    struct HierarchyEntry {
        MergeableHierarchy hierarchy;
        bool isTop;

        void dump(std::ostream& out) const {
            out << "{Hierarchy=";
            hierarchy.dump(out);
            out << ", isTop=" << isTop << "}";
        }
    };
    std::vector<HierarchyEntry> mMergeableHierarchies;
};

} // namespace caching

} // namespace android::surfaceflinger::frontend