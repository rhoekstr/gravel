#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/core/subgraph.h"
#include "gravel/fragility/fragility_result.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/ch/contraction.h"
#include <vector>

namespace gravel {

struct EntryPoint {
    NodeID original_id = INVALID_NODE;
    NodeID subgraph_id = INVALID_NODE;
    std::vector<NodeID> external_neighbors;  // in original graph
};

struct AccessibilityResult {
    std::vector<EntryPoint> entry_points;
    std::vector<FragilityResult> corridor_fragilities;  // between entry point pairs
    double accessibility_score = 0.0;                   // aggregate metric
};

// Analyze external accessibility of a subgraph within a larger network.
// Identifies entry points and measures corridor fragility.
AccessibilityResult analyze_accessibility(
    const ArrayGraph& full_graph,
    const ContractionResult& full_ch,
    const ShortcutIndex& full_idx,
    const SubgraphResult& subgraph);

}  // namespace gravel
