#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/core/edge_metadata.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace gravel {

// Default speed profiles by OSM highway tag (km/h)
struct SpeedProfile {
    static std::unordered_map<std::string, double> car() {
        return {
            {"motorway",       110.0},
            {"motorway_link",   60.0},
            {"trunk",           90.0},
            {"trunk_link",      50.0},
            {"primary",         70.0},
            {"primary_link",    45.0},
            {"secondary",       60.0},
            {"secondary_link",  40.0},
            {"tertiary",        50.0},
            {"tertiary_link",   35.0},
            {"residential",     30.0},
            {"unclassified",    40.0},
            {"service",         20.0},
            {"living_street",   10.0},
        };
    }
};

struct OSMConfig {
    std::string pbf_path;
    std::unordered_map<std::string, double> speed_profile;  // highway_tag -> km/h
    bool bidirectional = true;  // Add reverse edges for two-way roads
};

// Load a road network from an OSM .pbf file.
// Requires GRAVEL_HAS_OSMIUM=1 (libosmium).
// Returns an ArrayGraph with:
//   - weight = travel time in seconds
//   - secondary_weight = distance in meters (via edge_secondary_weight)
//   - coordinates for each node
std::unique_ptr<ArrayGraph> load_osm_graph(const OSMConfig& config);

/// Result of loading an OSM graph with edge metadata preserved.
struct OSMLoadResult {
    std::unique_ptr<ArrayGraph> graph;

    /// Per-edge metadata in CSR order. Contains all captured OSM tags.
    /// Access highway tags: result.metadata.get("highway")
    /// Access road names:   result.metadata.get("name")
    EdgeMetadata metadata;
};

/// Load an OSM graph WITH all edge tags preserved.
/// Captures highway, name, surface, bridge, tunnel, maxspeed, lanes, ref tags
/// for every edge in CSR order. Enables road-class filtering, bridge identification,
/// surface quality analysis, and any tag-based edge categorization.
///
/// Usage:
/// @code
/// auto result = load_osm_graph_with_labels(config);
///
/// // Road class filtering for simplification
/// auto labels = EdgeCategoryLabels::from_strings(
///     result.metadata.get("highway"), EdgeCategoryLabels::osm_road_ranks());
/// auto filter = make_category_filter(labels, 4);  // keep tertiary+
///
/// // Access any tag
/// const auto& names = result.metadata.get("name");
/// const auto& surfaces = result.metadata.get("surface");
/// @endcode
OSMLoadResult load_osm_graph_with_labels(const OSMConfig& config);

}  // namespace gravel
