/// @file tiger_loader.h
/// @brief US Census TIGER/Line boundary file loaders.
///
/// Thin wrappers around load_regions_geojson() with TIGER-specific
/// property names for each administrative layer.

#pragma once

#include "gravel/geo/geojson_loader.h"
#include <string>
#include <vector>

namespace gravel {

/// Load TIGER/Line county boundaries.
/// Property mapping: GEOID (5-digit FIPS) → region_id, NAMELSAD → label.
inline std::vector<RegionSpec> load_tiger_counties(const std::string& geojson_path) {
    GeoJSONLoadConfig cfg;
    cfg.region_id_property = "GEOID";
    cfg.label_property = "NAMELSAD";
    return load_regions_geojson(geojson_path, cfg);
}

/// Load TIGER/Line state boundaries.
/// Property mapping: STATEFP → region_id, NAME → label.
inline std::vector<RegionSpec> load_tiger_states(const std::string& geojson_path) {
    GeoJSONLoadConfig cfg;
    cfg.region_id_property = "STATEFP";
    cfg.label_property = "NAME";
    return load_regions_geojson(geojson_path, cfg);
}

/// Load TIGER/Line CBSA (Metropolitan/Micropolitan Statistical Area) boundaries.
/// Property mapping: CBSAFP → region_id, NAME → label.
inline std::vector<RegionSpec> load_tiger_cbsas(const std::string& geojson_path) {
    GeoJSONLoadConfig cfg;
    cfg.region_id_property = "CBSAFP";
    cfg.label_property = "NAME";
    return load_regions_geojson(geojson_path, cfg);
}

/// Load TIGER/Line place (city/CDP) boundaries.
/// Property mapping: GEOID (7-digit) → region_id, NAMELSAD → label.
inline std::vector<RegionSpec> load_tiger_places(const std::string& geojson_path) {
    GeoJSONLoadConfig cfg;
    cfg.region_id_property = "GEOID";
    cfg.label_property = "NAMELSAD";
    return load_regions_geojson(geojson_path, cfg);
}

/// Load TIGER/Line urban area boundaries.
/// Property mapping: UACE10 → region_id, NAME10 → label.
inline std::vector<RegionSpec> load_tiger_urban_areas(const std::string& geojson_path) {
    GeoJSONLoadConfig cfg;
    cfg.region_id_property = "UACE10";
    cfg.label_property = "NAME10";
    return load_regions_geojson(geojson_path, cfg);
}

}  // namespace gravel
