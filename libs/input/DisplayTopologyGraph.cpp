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

#define LOG_TAG "DisplayTopologyValidator"

#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <com_android_input_flags.h>
#include <ftl/enum.h>
#include <input/DisplayTopologyGraph.h>
#include <input/PrintTools.h>
#include <ui/LogicalDisplayId.h>

#include <algorithm>

#define INDENT "  "

namespace input_flags = com::android::input::flags;

namespace android {

namespace {

std::string logicalDisplayIdToString(const ui::LogicalDisplayId& displayId) {
    return base::StringPrintf("displayId(%d)", displayId.val());
}

std::string adjacentDisplayToString(const DisplayTopologyAdjacentDisplay& adjacentDisplay) {
    return adjacentDisplay.dump();
}

std::string floatRectToString(const FloatRect& floatRect) {
    std::string dump;
    dump += base::StringPrintf("FloatRect(%f, %f, %f, %f)", floatRect.left, floatRect.top,
                               floatRect.right, floatRect.bottom);
    return dump;
}

std::string displayPropertiesToString(const DisplayTopologyGraph::Properties& displayProperties) {
    std::string dump;
    dump += "AdjacentDisplays: ";
    dump += dumpVector(displayProperties.adjacentDisplays, adjacentDisplayToString);
    dump += '\n';
    dump += base::StringPrintf("Density: %d", displayProperties.density);
    dump += '\n';
    dump += "Bounds: ";
    dump += floatRectToString(displayProperties.boundsInGlobalDp);
    dump += '\n';
    return dump;
}

DisplayTopologyPosition getOppositePosition(DisplayTopologyPosition position) {
    switch (position) {
        case DisplayTopologyPosition::LEFT:
            return DisplayTopologyPosition::RIGHT;
        case DisplayTopologyPosition::TOP:
            return DisplayTopologyPosition::BOTTOM;
        case DisplayTopologyPosition::RIGHT:
            return DisplayTopologyPosition::LEFT;
        case DisplayTopologyPosition::BOTTOM:
            return DisplayTopologyPosition::TOP;
    }
}

bool validatePrimaryDisplay(
        ui::LogicalDisplayId primaryDisplayId,
        const std::unordered_map<ui::LogicalDisplayId, DisplayTopologyGraph::Properties>&
                topologyGraph) {
    return primaryDisplayId != ui::LogicalDisplayId::INVALID &&
            topologyGraph.contains(primaryDisplayId);
}

bool validateTopologyGraph(
        const std::unordered_map<ui::LogicalDisplayId, DisplayTopologyGraph::Properties>&
                topologyGraph) {
    for (const auto& [sourceDisplay, displayProperties] : topologyGraph) {
        if (!sourceDisplay.isValid()) {
            LOG(ERROR) << "Invalid display in topology graph: " << sourceDisplay;
            return false;
        }
        if (displayProperties.boundsInGlobalDp.getHeight() <= 0 ||
            displayProperties.boundsInGlobalDp.getWidth() <= 0) {
            LOG(ERROR) << "Invalid display-bounds for " << logicalDisplayIdToString(sourceDisplay)
                       << " in topology graph: "
                       << floatRectToString(displayProperties.boundsInGlobalDp);
            return false;
        }
        if (displayProperties.density <= 0) {
            LOG(ERROR) << "Invalid density for " << logicalDisplayIdToString(sourceDisplay)
                       << "in topology graph: " << displayProperties.density;
            return false;
        }
        for (const DisplayTopologyAdjacentDisplay& adjacentDisplay :
             displayProperties.adjacentDisplays) {
            const auto adjacentGraphIt = topologyGraph.find(adjacentDisplay.displayId);
            if (adjacentGraphIt == topologyGraph.end()) {
                LOG(ERROR) << "Missing adjacent display in topology graph: "
                           << adjacentDisplay.displayId << " for source " << sourceDisplay;
                return false;
            }
            const auto reverseEdgeIt =
                    std::find_if(adjacentGraphIt->second.adjacentDisplays.begin(),
                                 adjacentGraphIt->second.adjacentDisplays.end(),
                                 [sourceDisplay](const DisplayTopologyAdjacentDisplay&
                                                         reverseAdjacentDisplay) {
                                     return sourceDisplay == reverseAdjacentDisplay.displayId;
                                 });
            if (reverseEdgeIt == adjacentGraphIt->second.adjacentDisplays.end()) {
                LOG(ERROR) << "Missing reverse edge in topology graph for: " << sourceDisplay
                           << " -> " << adjacentDisplay.displayId;
                return false;
            }
            DisplayTopologyPosition expectedPosition =
                    getOppositePosition(adjacentDisplay.position);
            if (reverseEdgeIt->position != expectedPosition) {
                LOG(ERROR) << "Unexpected reverse edge for: " << sourceDisplay << " -> "
                           << adjacentDisplay.displayId
                           << " expected position: " << ftl::enum_string(expectedPosition)
                           << " actual " << ftl::enum_string(reverseEdgeIt->position);
                return false;
            }
            if (reverseEdgeIt->offsetDp != -adjacentDisplay.offsetDp) {
                LOG(ERROR) << "Unexpected reverse edge offset: " << sourceDisplay << " -> "
                           << adjacentDisplay.displayId
                           << " expected offset: " << -adjacentDisplay.offsetDp << " actual "
                           << reverseEdgeIt->offsetDp;
                return false;
            }
        }
    }
    return true;
}

bool areTopologyGraphComponentsValid(
        ui::LogicalDisplayId primaryDisplayId,
        const std::unordered_map<ui::LogicalDisplayId, DisplayTopologyGraph::Properties>&
                topologyGraph) {
    if (!input_flags::enable_display_topology_validation()) {
        return true;
    }
    return validatePrimaryDisplay(primaryDisplayId, topologyGraph) &&
            validateTopologyGraph(topologyGraph);
}

std::string dumpTopologyGraphComponents(
        ui::LogicalDisplayId primaryDisplayId,
        const std::unordered_map<ui::LogicalDisplayId, DisplayTopologyGraph::Properties>&
                topologyGraph) {
    std::string dump;
    dump += base::StringPrintf("PrimaryDisplayId: %d\n", primaryDisplayId.val());
    dump += base::StringPrintf("TopologyGraph:\n");
    dump += addLinePrefix(dumpMap(topologyGraph, logicalDisplayIdToString,
                                  displayPropertiesToString),
                          INDENT);
    dump += "\n";
    return dump;
}

} // namespace

std::string DisplayTopologyAdjacentDisplay::dump() const {
    std::string dump;
    dump += base::StringPrintf("DisplayTopologyAdjacentDisplay: {displayId: %d, position: %s, "
                               "offsetDp: %f}",
                               displayId.val(), ftl::enum_string(position).c_str(), offsetDp);
    return dump;
}

DisplayTopologyGraph::DisplayTopologyGraph(
        ui::LogicalDisplayId primaryDisplay,
        std::unordered_map<ui::LogicalDisplayId, Properties>&& topologyGraph)
      : primaryDisplayId(primaryDisplay), graph(std::move(topologyGraph)) {}

std::string DisplayTopologyGraph::dump() const {
    return dumpTopologyGraphComponents(primaryDisplayId, graph);
}

base::Result<const DisplayTopologyGraph> DisplayTopologyGraph::create(
        ui::LogicalDisplayId primaryDisplay,
        std::unordered_map<ui::LogicalDisplayId, Properties>&& topologyGraph) {
    if (areTopologyGraphComponentsValid(primaryDisplay, topologyGraph)) {
        return DisplayTopologyGraph(primaryDisplay, std::move(topologyGraph));
    }
    return base::Error() << "Invalid display topology components: "
                         << dumpTopologyGraphComponents(primaryDisplay, topologyGraph);
}

} // namespace android
