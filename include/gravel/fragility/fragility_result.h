#pragma once
#include "gravel/core/types.h"
#include <vector>

namespace gravel {

struct EdgeFragility {
    NodeID source = INVALID_NODE;
    NodeID target = INVALID_NODE;
    Weight replacement_distance = INF_WEIGHT;  // INF if no alternate exists
    double fragility_ratio = 0.0;              // replacement / primary (>= 1.0, INF if bridge-like)
};

struct FragilityResult {
    Weight primary_distance = INF_WEIGHT;
    std::vector<NodeID> primary_path;
    std::vector<EdgeFragility> edge_fragilities;  // one per path edge (path.size()-1)

    bool valid() const { return primary_distance < INF_WEIGHT; }

    size_t bottleneck_index() const {
        size_t best = 0;
        for (size_t i = 1; i < edge_fragilities.size(); ++i) {
            if (edge_fragilities[i].fragility_ratio > edge_fragilities[best].fragility_ratio) {
                best = i;
            }
        }
        return best;
    }

    const EdgeFragility& bottleneck() const {
        return edge_fragilities[bottleneck_index()];
    }
};

struct AlternateRouteResult {
    Weight distance = INF_WEIGHT;
    std::vector<NodeID> path;
    double sharing = 0.0;   // fraction of edges shared with primary
    double stretch = 0.0;   // distance / primary_distance
};

}  // namespace gravel
