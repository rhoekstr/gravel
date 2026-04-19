#include "gravel/geo/edge_confidence.h"
#include "gravel/core/edge_labels.h"
#include <algorithm>
#include <utility>
#include <vector>

namespace gravel {

EdgeConfidence estimate_osm_confidence(
    const ArrayGraph& graph,
    const EdgeMetadata& metadata) {

    EdgeConfidence result;
    EdgeID m = graph.edge_count();
    result.scores.resize(m, 0.2);  // base confidence for all edges

    // Get tag vectors (empty if tag not captured)
    const auto& highway = metadata.get("highway");
    const auto& name = metadata.get("name");
    const auto& surface = metadata.get("surface");
    const auto& lanes = metadata.get("lanes");
    const auto& maxspeed = metadata.get("maxspeed");

    auto ranks = EdgeCategoryLabels::osm_road_ranks();

    for (EdgeID e = 0; e < m; ++e) {
        double score = 0.2;  // base

        // Major road class bonus
        if (e < highway.size() && !highway[e].empty()) {
            auto it = ranks.find(highway[e]);
            if (it != ranks.end() && it->second <= 3) {
                score += 0.25;  // secondary or above
            }
        }

        // Named road bonus
        if (e < name.size() && !name[e].empty()) {
            score += 0.20;
        }

        // Surface tag bonus
        if (e < surface.size() && !surface[e].empty()) {
            score += 0.15;
        }

        // Lanes tag bonus
        if (e < lanes.size() && !lanes[e].empty()) {
            score += 0.10;
        }

        // Maxspeed tag bonus
        if (e < maxspeed.size() && !maxspeed[e].empty()) {
            score += 0.10;
        }

        result.scores[e] = std::clamp(score, 0.0, 1.0);
    }

    return result;
}

EdgeConfidence confidence_from_array(std::vector<double> values) {
    EdgeConfidence result;
    result.scores = std::move(values);
    return result;
}

}  // namespace gravel
