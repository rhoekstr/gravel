/// @file cbsa_assignment.h
/// @brief Typed wrapper around RegionAssignment for US CBSA regions.
///
/// Core Based Statistical Areas (Metropolitan and Micropolitan Statistical
/// Areas) group counties into labor market regions. This wrapper provides
/// CBSA-specific accessors.

#pragma once

#include "gravel/geo/region_assignment.h"
#include "gravel/us/tiger_loader.h"
#include <string>

namespace gravel {

/// Typed wrapper providing CBSA-specific accessors.
struct CBSAAssignment {
    RegionAssignment assignment;

    /// Get the CBSA code for a node, or empty string if unassigned.
    const std::string& cbsa_code(NodeID node) const {
        return assignment.region_id_of(node);
    }

    /// Get the CBSA name (e.g., "Asheville, NC") for a node.
    const std::string& cbsa_name(NodeID node) const {
        static const std::string empty;
        int32_t idx = assignment.region_index[node];
        return (idx >= 0) ? assignment.regions[idx].label : empty;
    }

    /// Number of CBSAs with at least one assigned node.
    uint32_t active_cbsa_count() const {
        uint32_t count = 0;
        for (uint32_t c : assignment.region_node_counts) {
            if (c > 0) count++;
        }
        return count;
    }
};

/// Build a CBSAAssignment from a graph and TIGER CBSA GeoJSON file.
inline CBSAAssignment assign_cbsas(
    const ArrayGraph& graph,
    const std::string& tiger_cbsa_geojson,
    const AssignmentConfig& config = {}) {

    auto regions = load_tiger_cbsas(tiger_cbsa_geojson);
    CBSAAssignment ca;
    ca.assignment = assign_nodes_to_regions(graph, regions, config);
    return ca;
}

}  // namespace gravel
