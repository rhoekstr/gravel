/// @file geojson_loader.h
/// @brief Load geographic boundary regions from GeoJSON files.
///
/// CRITICAL: GeoJSON coordinates are [longitude, latitude] (x,y).
/// Gravel Coord is {lat, lon}. This loader performs the swap internally —
/// callers always receive Gravel Coord conventions.

#pragma once

#include "gravel/geo/region_assignment.h"
#include <string>
#include <vector>

namespace gravel {

struct GeoJSONLoadConfig {
    /// GeoJSON property name containing the region identifier.
    /// e.g., "GEOID" for TIGER county files, "NAME" for states.
    std::string region_id_property = "GEOID";

    /// GeoJSON property name containing the human-readable label.
    /// e.g., "NAMELSAD" for TIGER files.
    std::string label_property = "NAMELSAD";
};

/// Load regions from a GeoJSON file (FeatureCollection).
/// Each Feature becomes a RegionSpec. Coordinates are swapped from
/// GeoJSON [lon, lat] to Gravel {lat, lon}.
///
/// Supports Polygon and MultiPolygon geometry types.
/// For MultiPolygon, only the first (largest) polygon ring is used.
std::vector<RegionSpec> load_regions_geojson(
    const std::string& geojson_path,
    const GeoJSONLoadConfig& config = {});

/// Load regions from a GeoJSON string (for testing or in-memory use).
std::vector<RegionSpec> load_regions_geojson_string(
    const std::string& geojson_str,
    const GeoJSONLoadConfig& config = {});

}  // namespace gravel
