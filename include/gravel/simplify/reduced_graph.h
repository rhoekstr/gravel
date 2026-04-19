/// @file reduced_graph.h
/// @brief Region-aware graph reduction — one central node per region + border nodes.
///
/// Transforms a full graph into a much smaller graph preserving inter-regional
/// structure:
///   - One "central" node per region (selected by centrality method)
///   - All "border" nodes (nodes with edges extending to a different region)
///   - Intra-region edges: central → each border node, weighted by CH distance
///   - Inter-region edges: original border-crossing edges, preserved
///
/// The reduced graph has 10-1000x fewer nodes than the original, making
/// inter-regional analysis fast even at national scale.
///
/// This module is region-agnostic: it operates on int32 region indices and
/// Polygon boundaries. Thin adapters in gravel-geo provide typed wrappers for
/// `RegionAssignment` (gravel/geo/geography_skeleton.h).

#pragma once

#include "gravel/core/array_graph.h"
#include "gravel/core/subgraph.h"        // for Polygon
#include "gravel/ch/contraction.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gravel {

/// Ordered pair of region indices (region_a < region_b).
struct RegionPair {
    int32_t region_a;
    int32_t region_b;

    bool operator==(const RegionPair& o) const {
        return region_a == o.region_a && region_b == o.region_b;
    }
};

struct RegionPairHash {
    size_t operator()(const RegionPair& p) const {
        return std::hash<int64_t>{}(
            (static_cast<int64_t>(p.region_a) << 32) |
            static_cast<uint32_t>(p.region_b));
    }
};

/// Per-region input metadata needed by the reduction.
struct RegionInfo {
    std::string region_id;   ///< Unique identifier (caller-defined, e.g., FIPS or ISO code)
    Polygon boundary;        ///< Optional. Required only if centrality = GEOMETRIC_CENTROID.
};

/// Configuration for central-node selection.
struct ReducedGraphConfig {
    enum class Centrality {
        /// Pick node nearest to the region's polygon centroid. Requires `RegionInfo::boundary`.
        GEOMETRIC_CENTROID,
        /// Pick the node with the highest out-degree.
        HIGHEST_DEGREE,
        /// Use caller-provided central nodes.
        PROVIDED,
    };

    Centrality method = Centrality::GEOMETRIC_CENTROID;

    /// For method == PROVIDED: one central node per region (size == regions.size()).
    /// Use INVALID_NODE for regions with no valid central.
    std::vector<NodeID> precomputed_centrals;

    uint64_t seed = 42;
};

/// Result of building a reduced graph from a partitioned network.
struct ReducedGraph {
    /// The reduced graph. Nodes are laid out as: [all centrals] + [all border nodes].
    std::unique_ptr<ArrayGraph> graph;

    /// Region ID for each reduced node (empty if the original node was unassigned).
    std::vector<std::string> node_region;

    /// True for central nodes, false for border nodes.
    std::vector<bool> is_central;

    /// Region ID → central node ID in the reduced graph.
    std::unordered_map<std::string, NodeID> central_of;

    /// reduced_to_original[reduced_id] = original graph node ID.
    std::vector<NodeID> reduced_to_original;

    /// original_to_reduced[orig_id] = reduced node ID (absent if not in reduced graph).
    std::unordered_map<NodeID, NodeID> original_to_reduced;

    /// Inter-region edges keyed by region pair (region_a < region_b).
    /// Each entry is a list of directed (reduced_src, reduced_tgt) pairs.
    std::unordered_map<RegionPair, std::vector<std::pair<NodeID, NodeID>>, RegionPairHash>
        inter_region_edges;

    bool valid() const { return graph && graph->node_count() > 0; }
};

/// Build a reduced graph from a partitioned network.
///
/// @param graph          Original full graph (with coordinates).
/// @param ch             Pre-built CH on the full graph (for central-to-border distances).
/// @param node_region    Per-node region index. Size == graph.node_count(). Use -1 for unassigned.
/// @param regions        Per-region metadata (id + optional boundary).
/// @param config         Configuration (centrality method, etc.).
ReducedGraph build_reduced_graph(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const std::vector<int32_t>& node_region,
    const std::vector<RegionInfo>& regions,
    const ReducedGraphConfig& config = {});

}  // namespace gravel
