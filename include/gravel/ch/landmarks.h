#pragma once
#include "gravel/core/array_graph.h"
#include <vector>

namespace gravel {

struct LandmarkData {
    uint32_t num_landmarks = 0;
    // dist_from[l][v] = distance from landmark l to node v
    std::vector<std::vector<Weight>> dist_from;
    // dist_to[l][v] = distance from node v to landmark l
    std::vector<std::vector<Weight>> dist_to;

    // ALT lower bound on d(s, t): max over landmarks of |d(l,s) - d(l,t)|
    Weight lower_bound(NodeID s, NodeID t) const;
};

// Precompute landmark distances using farthest-first selection.
// num_landmarks: typically 16 per S2-9.4.
LandmarkData precompute_landmarks(const ArrayGraph& graph,
                                   uint32_t num_landmarks = 16,
                                   uint64_t seed = 42);

}  // namespace gravel
