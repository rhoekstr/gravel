/// @file incremental_sssp.h
/// @brief Reverse incremental SSSP for progressive edge removal analysis.
///
/// Manages a multi-source distance matrix on a compact local graph.
/// Supports the reverse incremental pattern: initialize from a degraded
/// state (all k edges removed), then restore edges one at a time with
/// efficient incremental updates that stop as soon as no improvement
/// is possible.
///
/// Lives in gravel-core because it uses Dijkstra on a local adjacency
/// structure with no CH dependency.

#pragma once

#include "gravel/core/types.h"
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <vector>
#include <utility>

namespace gravel {

/// Compact local adjacency list for IncrementalSSSP.
struct LocalGraph {
    uint32_t n_nodes = 0;

    struct Edge { uint32_t to; Weight weight; };
    std::vector<std::vector<Edge>> adj;

    /// Mapping between local and original node IDs.
    std::vector<NodeID> local_to_original;
};

/// Build a LocalGraph from an ArrayGraph subgraph.
/// The ArrayGraph's node IDs become local IDs directly (0..node_count-1).
class ArrayGraph;  // forward declaration
LocalGraph build_local_graph(const ArrayGraph& subgraph);

/// Multi-source incremental SSSP engine.
///
/// Usage pattern:
///   1. Construct with local graph, source nodes, and initially blocked edges.
///   2. dist(src_idx, target) gives current distance with all blocked edges absent.
///   3. Call restore_edge() to add edges back one at a time.
///   4. After each restore, distances are updated incrementally (not recomputed).
///
/// The unblocked distance (dist_full) serves as a strict lower bound.
/// Once dist reaches dist_full for a node, no future restoration can improve it.
class IncrementalSSSP {
public:
    static constexpr Weight INF = std::numeric_limits<Weight>::max() / 2;

    /// Construct and compute initial distances.
    /// @param graph     Compact local adjacency.
    /// @param sources   Local node IDs to compute distances FROM.
    /// @param blocked   Initial blocked edge keys (from edge_key()).
    IncrementalSSSP(const LocalGraph& graph,
                    const std::vector<uint32_t>& sources,
                    const std::unordered_set<uint64_t>& blocked);

    /// Construct with precomputed unblocked distances (avoids redundant Dijkstra).
    /// @param dist_full Precomputed unblocked distances (one vector per source).
    IncrementalSSSP(const LocalGraph& graph,
                    const std::vector<uint32_t>& sources,
                    const std::unordered_set<uint64_t>& blocked,
                    const std::vector<std::vector<Weight>>& dist_full);

    /// Restore one edge. Updates dist in place with bounded propagation.
    /// The edge must have been in the blocked set at construction.
    void restore_edge(uint32_t u, uint32_t v, Weight w);

    /// Current distance from sources[src_idx] to target.
    Weight dist(uint32_t src_idx, uint32_t target) const {
        return dist_[src_idx][target];
    }

    /// Unblocked (full-graph) distance — strict lower bound.
    Weight dist_full(uint32_t src_idx, uint32_t target) const {
        return dist_full_[src_idx][target];
    }

    /// Direct access to per-source distance arrays.
    const std::vector<std::vector<Weight>>& dist_matrix() const { return dist_; }
    const std::vector<std::vector<Weight>>& dist_full_matrix() const { return dist_full_; }

    /// Canonical undirected edge key for blocked-set lookups.
    static uint64_t edge_key(uint32_t u, uint32_t v) {
        if (u > v) std::swap(u, v);
        return (uint64_t(u) << 32) | uint64_t(v);
    }

    uint32_t source_count() const { return static_cast<uint32_t>(sources_.size()); }
    uint32_t node_count() const { return graph_.n_nodes; }

private:
    LocalGraph graph_;
    std::vector<uint32_t> sources_;
    std::unordered_set<uint64_t> blocked_;

    std::vector<std::vector<Weight>> dist_;       // dist_[src_idx][node]
    std::vector<std::vector<Weight>> dist_full_;   // unblocked lower bounds

    static std::vector<Weight> sssp(const LocalGraph& g, uint32_t source,
                                     const std::unordered_set<uint64_t>& blocked);
    static void propagate(const LocalGraph& g,
                          const std::unordered_set<uint64_t>& blocked,
                          std::vector<Weight>& dist,
                          uint32_t u, uint32_t v, Weight w);
};

}  // namespace gravel
