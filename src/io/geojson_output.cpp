#include "gravel/io/geojson_output.h"
#include <fstream>
#include <cmath>
#include <stdexcept>
#include <vector>
#include <string>

namespace gravel {

nlohmann::json path_to_geojson(const ArrayGraph& graph,
                                const std::vector<NodeID>& path,
                                const FragilityResult* fragility) {
    nlohmann::json coords = nlohmann::json::array();
    for (NodeID v : path) {
        auto c = graph.node_coordinate(v);
        if (c) {
            coords.push_back({c->lon, c->lat});  // GeoJSON is [lon, lat]
        }
    }

    nlohmann::json props;
    props["path_length"] = path.size();

    if (fragility && fragility->valid()) {
        props["primary_distance"] = fragility->primary_distance;
        if (!fragility->edge_fragilities.empty()) {
            const auto& bn = fragility->bottleneck();
            props["bottleneck_source"] = bn.source;
            props["bottleneck_target"] = bn.target;
            if (std::isfinite(bn.fragility_ratio)) {
                props["bottleneck_ratio"] = bn.fragility_ratio;
            }
        }
    }

    return {
        {"type", "Feature"},
        {"geometry", {{"type", "LineString"}, {"coordinates", coords}}},
        {"properties", props}
    };
}

nlohmann::json location_fragility_to_geojson(const LocationFragilityResult& result,
                                               Coord center) {
    nlohmann::json features = nlohmann::json::array();

    // Center point with summary
    features.push_back({
        {"type", "Feature"},
        {"geometry", {{"type", "Point"}, {"coordinates", {center.lon, center.lat}}}},
        {"properties", {
            {"type", "center"},
            {"isolation_risk", result.isolation_risk},
            {"baseline_isolation_risk", result.baseline_isolation_risk},
            {"auc_normalized", result.auc_normalized},
            {"directional_coverage", result.directional_coverage},
            {"directional_asymmetry", result.directional_asymmetry},
            {"reachable_nodes", result.reachable_nodes},
            {"sp_edges_total", result.sp_edges_total},
            {"sp_edges_removed", result.sp_edges_removed}
        }}
    });

    return {
        {"type", "FeatureCollection"},
        {"features", features}
    };
}

void write_geojson(const nlohmann::json& geojson, const std::string& path) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot open file for writing: " + path);
    out << geojson.dump(2) << "\n";
}

}  // namespace gravel
