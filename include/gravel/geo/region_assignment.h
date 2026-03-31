/// @file region_assignment.h
/// @brief Assign graph nodes to geographic boundary regions.
///
/// Enables boundary-aware simplification, geographic coarsening, and
/// batch regional analysis by mapping each graph node to a named region.

#pragma once

#include "gravel/core/types.h"
#include "gravel/core/array_graph.h"
#include "gravel/core/subgraph.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gravel {

/// A named geographic region with a boundary polygon.
struct RegionSpec {
    std::string region_id;   ///< Unique identifier (e.g., FIPS code "37173")
    std::string label;       ///< Human-readable label (e.g., "Swain County, NC")
    Polygon boundary;        ///< Boundary polygon (vertices in {lat, lon})
};

/// Per-node region assignment result.
struct RegionAssignment {
    /// region_index[node] = index into `regions`, or -1 if unassigned.
    std::vector<int32_t> region_index;

    /// The regions that were used for assignment.
    std::vector<RegionSpec> regions;

    /// Convenience: count of nodes assigned to each region.
    std::vector<uint32_t> region_node_counts;

    /// Number of unassigned nodes (region_index == -1).
    uint32_t unassigned_count = 0;

    /// Get region_id for a node, or empty string if unassigned.
    const std::string& region_id_of(NodeID node) const {
        static const std::string empty;
        int32_t idx = region_index[node];
        return (idx >= 0) ? regions[idx].region_id : empty;
    }
};

struct AssignmentConfig {
    uint64_t seed = 42;  ///< For any tie-breaking randomness (unused currently)
};

/// Assign graph nodes to regions based on point-in-polygon containment.
/// Nodes without coordinates or outside all regions get region_index = -1.
RegionAssignment assign_nodes_to_regions(
    const ArrayGraph& graph,
    const std::vector<RegionSpec>& regions,
    const AssignmentConfig& config = {});

}  // namespace gravel
