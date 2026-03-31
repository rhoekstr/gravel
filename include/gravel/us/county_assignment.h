/// @file county_assignment.h
/// @brief Typed wrapper around RegionAssignment for US FIPS counties.
///
/// Provides county-specific accessors (FIPS code, county name) on top
/// of the generic RegionAssignment. Constructed from TIGER county
/// boundaries loaded via load_tiger_counties().

#pragma once

#include "gravel/geo/region_assignment.h"
#include "gravel/us/tiger_loader.h"
#include <string>

namespace gravel {

/// Typed wrapper providing county-specific accessors.
struct CountyAssignment {
    RegionAssignment assignment;

    /// Get the 5-digit FIPS code for a node, or empty string if unassigned.
    const std::string& fips_code(NodeID node) const {
        return assignment.region_id_of(node);
    }

    /// Get the county label (e.g., "Swain County") for a node.
    const std::string& county_name(NodeID node) const {
        static const std::string empty;
        int32_t idx = assignment.region_index[node];
        return (idx >= 0) ? assignment.regions[idx].label : empty;
    }

    /// Get the 2-digit state FIPS prefix for a node.
    std::string state_fips(NodeID node) const {
        const auto& fips = fips_code(node);
        return (fips.size() >= 2) ? fips.substr(0, 2) : "";
    }

    /// Number of counties with at least one assigned node.
    uint32_t active_county_count() const {
        uint32_t count = 0;
        for (uint32_t c : assignment.region_node_counts) {
            if (c > 0) count++;
        }
        return count;
    }
};

/// Build a CountyAssignment from a graph and TIGER county GeoJSON file.
inline CountyAssignment assign_counties(
    const ArrayGraph& graph,
    const std::string& tiger_counties_geojson,
    const AssignmentConfig& config = {}) {

    auto regions = load_tiger_counties(tiger_counties_geojson);
    CountyAssignment ca;
    ca.assignment = assign_nodes_to_regions(graph, regions, config);
    return ca;
}

}  // namespace gravel
