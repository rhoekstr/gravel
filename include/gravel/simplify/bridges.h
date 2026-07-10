#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/shortcut_index.h"
#include <cstdint>
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

/// Per-CSR-edge bridge analysis: for each directed edge (aligned with the graph's CSR /
/// `to_coo()` order), whether it is a bridge and, if so, how many nodes it strands (the
/// smaller side of the cut it opens). Parallel edges are not bridges. Computed in a single
/// iterative-Tarjan DFS: the cut size is the DFS subtree size at the bridge, so no separate
/// bridge tree is built and the working state is all vector-indexed (no per-node hash maps).
struct EdgeBridgeInfo {
    std::vector<uint8_t> is_bridge;   ///< per CSR edge: 1 if removing it disconnects its endpoints
    std::vector<uint32_t> cut_size;   ///< per CSR edge: nodes stranded if it fails (0 if not a bridge)
};
EdgeBridgeInfo bridge_edge_info(const ArrayGraph& graph);

/// Compute replacement cost for each bridge using blocked CH queries.
/// For each bridge {u, v}, computes the shortest path from u to v with {u,v} blocked.
/// Populates bridges.replacement_costs (parallel to bridges.bridges).
void compute_bridge_costs(
    BridgeResult& bridges,
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx);

}  // namespace gravel
