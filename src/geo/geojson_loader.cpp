#include "gravel/geo/geojson_loader.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace gravel {

namespace {

/// Extract polygon vertices from a GeoJSON coordinate ring.
/// GeoJSON coordinates are [lon, lat] — swap to Gravel {lat, lon}.
std::vector<Coord> parse_ring(const nlohmann::json& ring) {
    std::vector<Coord> vertices;
    vertices.reserve(ring.size());
    for (const auto& coord : ring) {
        if (coord.size() >= 2) {
            double lon = coord[0].get<double>();
            double lat = coord[1].get<double>();
            vertices.push_back({lat, lon});
        }
    }
    return vertices;
}

std::vector<RegionSpec> parse_feature_collection(
    const nlohmann::json& root,
    const GeoJSONLoadConfig& config) {

    std::vector<RegionSpec> regions;

    if (!root.contains("features") || !root["features"].is_array())
        return regions;

    for (const auto& feature : root["features"]) {
        if (!feature.contains("geometry") || !feature.contains("properties"))
            continue;

        const auto& geom = feature["geometry"];
        const auto& props = feature["properties"];
        std::string geom_type = geom.value("type", "");

        RegionSpec spec;

        // Extract region_id
        if (props.contains(config.region_id_property) &&
            !props[config.region_id_property].is_null()) {
            if (props[config.region_id_property].is_string())
                spec.region_id = props[config.region_id_property].get<std::string>();
            else
                spec.region_id = props[config.region_id_property].dump();
        }

        // Extract label
        if (props.contains(config.label_property) &&
            props[config.label_property].is_string()) {
            spec.label = props[config.label_property].get<std::string>();
        }

        // Extract polygon vertices
        if (geom_type == "Polygon" && geom.contains("coordinates")) {
            const auto& coords = geom["coordinates"];
            if (!coords.empty())
                spec.boundary.vertices = parse_ring(coords[0]);
        } else if (geom_type == "MultiPolygon" && geom.contains("coordinates")) {
            const auto& polys = geom["coordinates"];
            if (!polys.empty() && !polys[0].empty())
                spec.boundary.vertices = parse_ring(polys[0][0]);
        } else {
            continue;
        }

        if (spec.boundary.vertices.size() >= 3)
            regions.push_back(std::move(spec));
    }

    return regions;
}

}  // namespace

std::vector<RegionSpec> load_regions_geojson(
    const std::string& geojson_path,
    const GeoJSONLoadConfig& config) {

    std::ifstream ifs(geojson_path);
    if (!ifs.is_open())
        throw std::runtime_error("Cannot open GeoJSON file: " + geojson_path);

    nlohmann::json root;
    ifs >> root;

    return parse_feature_collection(root, config);
}

std::vector<RegionSpec> load_regions_geojson_string(
    const std::string& geojson_str,
    const GeoJSONLoadConfig& config) {

    auto root = nlohmann::json::parse(geojson_str);
    return parse_feature_collection(root, config);
}

}  // namespace gravel
