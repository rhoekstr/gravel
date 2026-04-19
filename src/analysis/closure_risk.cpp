#include "gravel/analysis/closure_risk.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>

namespace gravel {

double ClosureRiskData::tier_fraction(ClosureRiskTier tier) const {
    if (edge_tiers.empty()) return 0.0;
    uint32_t count = 0;
    for (auto t : edge_tiers) {
        if (t == tier) count++;
    }
    return static_cast<double>(count) / edge_tiers.size();
}

ClosureRiskTier ClosureRiskData::max_tier_on_path(
    const std::vector<uint32_t>& edge_indices) const {
    ClosureRiskTier max_t = ClosureRiskTier::LOW;
    for (uint32_t idx : edge_indices) {
        if (idx < edge_tiers.size() && edge_tiers[idx] > max_t) {
            max_t = edge_tiers[idx];
        }
    }
    return max_t;
}

ClosureRiskData classify_closure_risk(
    const ArrayGraph& graph,
    const ElevationData& elevation,
    const std::vector<std::string>& edge_labels,
    ClosureRiskConfig config) {

    ClosureRiskData result;
    EdgeID m = graph.edge_count();
    result.edge_tiers.resize(m, ClosureRiskTier::LOW);

    NodeID n = graph.node_count();
    uint32_t edge_idx = 0;

    for (NodeID u = 0; u < n; ++u) {
        auto targets = graph.outgoing_targets(u);
        for (size_t i = 0; i < targets.size(); ++i) {
            NodeID v = targets[i];

            // Get max elevation along this edge
            double max_elev = elevation.edge_max_elevation(u, v);
            bool has_elev = !std::isnan(max_elev);

            // Get road class rank
            uint8_t road_rank = 5;  // default: minor road
            if (edge_idx < edge_labels.size() && !edge_labels[edge_idx].empty()) {
                auto it = config.road_class_rank.find(edge_labels[edge_idx]);
                if (it != config.road_class_rank.end()) {
                    road_rank = it->second;
                }
            }

            // Classification logic
            ClosureRiskTier tier = ClosureRiskTier::LOW;

            if (has_elev) {
                if (max_elev >= config.severe_elevation && road_rank >= 4) {
                    tier = ClosureRiskTier::SEVERE;
                } else if (max_elev >= config.high_elevation && road_rank >= 3) {
                    tier = ClosureRiskTier::HIGH;
                } else if (max_elev >= config.moderate_elevation) {
                    tier = ClosureRiskTier::MODERATE;
                }
            } else {
                // No elevation data: minor roads get moderate risk
                if (road_rank >= 5) {
                    tier = ClosureRiskTier::MODERATE;
                }
            }

            result.edge_tiers[edge_idx] = tier;
            edge_idx++;
        }
    }

    return result;
}

std::vector<double> seasonal_weight_multipliers(
    const ClosureRiskData& risk,
    double tier1_multiplier,
    double tier2_multiplier,
    double tier3_multiplier) {

    std::vector<double> multipliers(risk.edge_tiers.size(), 1.0);

    for (size_t i = 0; i < risk.edge_tiers.size(); ++i) {
        switch (risk.edge_tiers[i]) {
            case ClosureRiskTier::LOW:      multipliers[i] = 1.0; break;
            case ClosureRiskTier::MODERATE:  multipliers[i] = tier1_multiplier; break;
            case ClosureRiskTier::HIGH:      multipliers[i] = tier2_multiplier; break;
            case ClosureRiskTier::SEVERE:    multipliers[i] = tier3_multiplier; break;
        }
    }

    return multipliers;
}

}  // namespace gravel
