#pragma once
#include "gravel/fragility/fragility_result.h"
#include "gravel/ch/contraction.h"
#include <vector>

namespace gravel {

struct ViaPathConfig {
    double max_stretch = 1.25;    // max ratio alternative/primary distance
    double max_sharing = 0.80;    // max fraction of edges shared with primary
    uint32_t max_alternatives = 3;
};

// Find alternative routes via the via-path method (Abraham et al.).
// 1. Full forward CH search from s (no early termination)
// 2. Full backward CH search from t
// 3. For each node v settled in both with dist(s,v)+dist(v,t) <= max_stretch * d*:
//    - Skip if v on primary path
//    - Compute path s→v→t, check sharing and stretch
//    - Keep if sharing < max_sharing
// 4. Return top-k by stretch
std::vector<AlternateRouteResult> find_alternative_routes(
    const ContractionResult& ch,
    NodeID source, NodeID target,
    ViaPathConfig config = {});

}  // namespace gravel
