#pragma once
#include "gravel/fragility/fragility_result.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/ch/contraction.h"
#include "gravel/core/array_graph.h"
#include <vector>
#include <utility>

namespace gravel {

struct FragilityConfig {
    // No config needed yet; placeholder for future options (e.g., max_replacement_distance).
};

// Compute replacement path fragility for every edge on the primary s-t path.
// Uses CH query for the primary path, then BlockedCHQuery for each edge.
FragilityResult route_fragility(const ContractionResult& ch,
                                 const ShortcutIndex& shortcut_idx,
                                 const ArrayGraph& graph,
                                 NodeID source, NodeID target,
                                 FragilityConfig config = {});

// Batch version: compute fragility for multiple O-D pairs (parallelized with OpenMP).
std::vector<FragilityResult> batch_fragility(
    const ContractionResult& ch,
    const ShortcutIndex& shortcut_idx,
    const ArrayGraph& graph,
    const std::vector<std::pair<NodeID, NodeID>>& od_pairs,
    FragilityConfig config = {});

}  // namespace gravel
