/// @file border_edges.h
/// @brief Summarize edges that cross region boundaries.
///
/// Border edges connect nodes in different regions. Their characteristics
/// (count, total weight, distribution across region pairs) are essential
/// inputs for geographic graph coarsening and inter-regional connectivity
/// analysis.

#pragma once

#include "gravel/core/array_graph.h"
#include "gravel/geo/region_assignment.h"
#include "gravel/simplify/reduced_graph.h"  // for RegionPair / RegionPairHash
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gravel {

// RegionPair and RegionPairHash are defined in gravel/simplify/reduced_graph.h
// (moved there during the v2.2 refactor; kept in gravel-simplify since they are
// generic integer-pair types used by both border edges and reduced graphs).

/// Summary statistics for border edges between a pair of regions.
struct BorderEdgeSummary {
    RegionPair regions;
    uint32_t edge_count = 0;          ///< Directed edge count crossing this boundary
    double total_weight = 0.0;        ///< Sum of edge weights
    double min_weight = 0.0;          ///< Lightest crossing edge
    double max_weight = 0.0;          ///< Heaviest crossing edge
    Weight shortest_path = 0;         ///< Shortest path weight between any border node pair (0 = not computed)
};

/// Full result of border edge analysis.
struct BorderEdgeResult {
    /// Per-region-pair summaries, keyed by ordered RegionPair.
    std::unordered_map<RegionPair, BorderEdgeSummary, RegionPairHash> pair_summaries;

    /// Total border edges across all region pairs.
    uint32_t total_border_edges = 0;

    /// Number of distinct region pairs connected by at least one edge.
    uint32_t connected_pairs = 0;

    /// Edges touching unassigned nodes (region_index == -1).
    uint32_t unassigned_edges = 0;
};

/// Summarize all edges that cross region boundaries.
///
/// For each pair of regions (A, B) with A < B, counts directed edges,
/// sums weights, and records min/max weight. Edges touching unassigned
/// nodes are counted separately.
///
/// O(V + E) — single pass over all edges.
BorderEdgeResult summarize_border_edges(
    const ArrayGraph& graph,
    const RegionAssignment& assignment);

}  // namespace gravel
