#pragma once
#include "gravel/fragility/fragility_result.h"
#include "gravel/core/array_graph.h"
#include <vector>

namespace gravel {

struct BernsteinConfig {
    double epsilon = 0.1;  // approximation factor: results within (1+epsilon) of exact
};

// (1+epsilon)-approximate replacement paths via Bernstein's path hitting set approach.
// Faster than exact Hershberger-Suri for batch queries where exact bottleneck IDs
// aren't critical.
//
// Algorithm overview:
// 1. Compute SPT from s, extract s-t path
// 2. Partition path into O(log n / epsilon) geometrically increasing segments
// 3. For each segment boundary, compute detour distances using backward Dijkstra
// 4. Combine to get approximate replacement distances
//
// Results are within (1+epsilon) of exact replacement distances.
FragilityResult bernstein_approx(const ArrayGraph& graph,
                                  NodeID source, NodeID target,
                                  BernsteinConfig config = {});

}  // namespace gravel
