#pragma once
#include "gravel/core/types.h"
#include "gravel/core/array_graph.h"
#include <functional>
#include <unordered_map>
#include <vector>

namespace gravel {

struct CHBuildConfig {
    double edge_diff_weight    = 190.0;
    double deleted_nbr_weight  = 120.0;
    double level_weight        = 1.0;
    int max_settle_for_witness = 500;    // witness search limit for actual contraction
    int max_settle_for_priority = 50;   // cheaper limit for priority estimation
    bool parallel = false;               // use batched parallel contraction (SPoCH-style)
    uint32_t batch_size = 0;             // max independent set size per batch (0 = auto)
};

// The result of contraction hierarchy construction.
// Contains up/down graphs in SoA CSR format for bidirectional Dijkstra.
struct ContractionResult {
    NodeID num_nodes = 0;

    // Upward graph (edges to higher-level nodes) — SoA layout
    std::vector<uint32_t> up_offsets;
    std::vector<NodeID> up_targets;
    std::vector<Weight> up_weights;
    std::vector<NodeID> up_shortcut_mid;  // INVALID_NODE = original edge

    // Downward graph (edges to higher-level nodes, reversed) — SoA layout
    std::vector<uint32_t> down_offsets;
    std::vector<NodeID> down_targets;
    std::vector<Weight> down_weights;
    std::vector<NodeID> down_shortcut_mid;

    // Node metadata
    std::vector<Level> node_levels;  // contraction order level
    std::vector<NodeID> order;       // order[rank] = original node ID

    // Complete unpacking table: maps (source, target) → shortcut_mid for ALL overlay edges.
    // Key = (uint64_t(source) << 32) | target. Value = mid node (INVALID_NODE = original edge).
    std::unordered_map<uint64_t, NodeID> unpack_map;

    static uint64_t pack_edge(NodeID u, NodeID v) {
        return (uint64_t(u) << 32) | uint64_t(v);
    }
};

// Build contraction hierarchy from a graph.
// Progress callback receives percentage (0-100).
ContractionResult build_ch(const ArrayGraph& graph,
                           CHBuildConfig config = {},
                           std::function<void(int)> progress_cb = nullptr);

}  // namespace gravel
