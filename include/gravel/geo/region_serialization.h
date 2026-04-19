/// @file region_serialization.h
/// @brief Save/load RegionAssignment to disk for caching.
///
/// Point-in-polygon assignment on 200K+ nodes with 3000+ county polygons
/// takes several seconds. Serializing the result avoids recomputing on
/// every run for the same graph + boundary combination.
///
/// Binary format (little-endian):
///   [4 bytes] magic: "GRAV"
///   [4 bytes] version: 1
///   [4 bytes] n_nodes
///   [4 bytes] n_regions
///   [4 bytes] unassigned_count
///   [n_nodes * 4 bytes] region_index (int32_t per node)
///   [n_regions * 4 bytes] region_node_counts (uint32_t per region)
///   For each region:
///     [4 bytes] region_id length, [N bytes] region_id string
///     [4 bytes] label length, [N bytes] label string

#pragma once

#include "gravel/geo/region_assignment.h"
#include <string>

namespace gravel {

/// Save a RegionAssignment to a binary file.
/// Does NOT save polygon boundaries (they can be re-loaded from GeoJSON).
void save_region_assignment(const RegionAssignment& assignment,
                            const std::string& path);

/// Load a RegionAssignment from a binary file.
/// The loaded assignment has empty polygon boundaries — if polygons are needed
/// for further analysis, re-load them from the original GeoJSON source.
RegionAssignment load_region_assignment(const std::string& path);

}  // namespace gravel
