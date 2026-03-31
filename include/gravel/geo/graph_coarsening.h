/// @file graph_coarsening.h
/// @brief Geographic graph coarsening — collapse regions to single nodes.
///
/// Produces a compact "meta-graph" where each node represents one region
/// (e.g., a county) and edges represent the best inter-regional connections.
/// Edge weights are the minimum border crossing weight between region pairs.
///
/// Use cases:
/// - Rapid multi-county fragility screening (run analysis on small coarsened graph)
/// - Visualization of regional connectivity structure
/// - Input to higher-level network analysis (county-to-county travel times)

#pragma once

#include "gravel/core/array_graph.h"
#include "gravel/geo/region_assignment.h"
#include "gravel/geo/border_edges.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace gravel {

/// Configuration for graph coarsening.
struct CoarseningConfig {
    /// How to set edge weights between region nodes.
    enum class EdgeWeightMode {
        MIN_BORDER_WEIGHT,     ///< Lightest border edge (fastest crossing)
        SHORTEST_PATH,         ///< Shortest path between closest border nodes (requires CH)
    };

    EdgeWeightMode weight_mode = EdgeWeightMode::MIN_BORDER_WEIGHT;

    /// If true, compute representative coordinates for each region node
    /// as the centroid of all nodes in that region.
    bool compute_centroids = true;

    /// Minimum edge count between two regions to create a coarsened edge.
    /// Filters out spurious single-edge connections.
    uint32_t min_border_edges = 1;
};

/// Result of geographic graph coarsening.
struct CoarseningResult {
    /// The coarsened graph. Node i corresponds to regions[i] in the assignment.
    /// Null if coarsening produced no regions.
    std::unique_ptr<ArrayGraph> graph;

    /// Region index for each coarsened node (identity mapping: node i = region i).
    /// Provided for API consistency with RegionAssignment.
    std::vector<int32_t> region_indices;

    /// Region labels, one per coarsened node.
    std::vector<std::string> region_labels;

    /// Region IDs, one per coarsened node.
    std::vector<std::string> region_ids;

    /// Number of original graph nodes per coarsened node.
    std::vector<uint32_t> node_counts;

    /// Number of original graph edges internal to each region.
    std::vector<uint32_t> internal_edge_counts;
};

/// Coarsen a graph by collapsing each region to a single node.
///
/// Requires a pre-computed RegionAssignment and BorderEdgeResult.
/// Unassigned nodes (region_index == -1) are excluded from the coarsened graph.
///
/// @param graph         Original graph
/// @param assignment    Node-to-region mapping
/// @param border_edges  Pre-computed border edge summary
/// @param config        Coarsening configuration
CoarseningResult coarsen_graph(
    const ArrayGraph& graph,
    const RegionAssignment& assignment,
    const BorderEdgeResult& border_edges,
    const CoarseningConfig& config = {});

}  // namespace gravel
