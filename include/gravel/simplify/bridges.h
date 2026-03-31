#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/shortcut_index.h"
#include <utility>
#include <vector>

namespace gravel {

struct BridgeResult {
    /// Each bridge is an undirected edge {u, v} with u < v.
    std::vector<std::pair<NodeID, NodeID>> bridges;

    /// Per-bridge replacement cost: shortest detour when this bridge is removed.
    /// INF_WEIGHT if the bridge disconnects the graph (no alternative).
    /// Populated by compute_bridge_costs(), empty after find_bridges() alone.
    /// Why: Bridge density alone treats a bridge with a 30-second detour identically
    /// to one with no alternative. Replacement cost distinguishes critical from redundant.
    std::vector<Weight> replacement_costs;
};

// Find all bridges in the graph using iterative Tarjan's algorithm.
// Treats the directed graph as undirected (any edge u→v or v→u means {u,v} exists).
// Parallel edges between the same pair of nodes are NOT bridges.
// O(V + E) time and space.
BridgeResult find_bridges(const ArrayGraph& graph);

/// Compute replacement cost for each bridge using blocked CH queries.
/// For each bridge {u, v}, computes the shortest path from u to v with {u,v} blocked.
/// Populates bridges.replacement_costs (parallel to bridges.bridges).
void compute_bridge_costs(
    BridgeResult& bridges,
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx);

}  // namespace gravel
