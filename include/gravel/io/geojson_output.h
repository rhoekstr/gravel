#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/fragility/fragility_result.h"
#include "gravel/analysis/location_fragility.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace gravel {

// Convert a route path to a GeoJSON LineString Feature.
// Properties include distance, bottleneck info if fragility provided.
nlohmann::json path_to_geojson(const ArrayGraph& graph,
                                const std::vector<NodeID>& path,
                                const FragilityResult* fragility = nullptr);

// Convert location fragility result to GeoJSON FeatureCollection.
// Includes: center point with isolation risk and directional coverage.
nlohmann::json location_fragility_to_geojson(const LocationFragilityResult& result,
                                               Coord center);

// Write GeoJSON to file.
void write_geojson(const nlohmann::json& geojson, const std::string& path);

}  // namespace gravel
