#pragma once
#include "gravel/fragility/fragility_result.h"
#include "gravel/core/array_graph.h"
#include <vector>

namespace gravel {

// Hershberger-Suri-style exact replacement paths via SPT sensitivity analysis.
// O(m + n log n) for a single source-target pair.
//
// Algorithm:
// 1. Forward Dijkstra from s → SPT + distances
// 2. Reverse Dijkstra from t → backward distances
// 3. Extract s-t path from SPT
// 4. For each non-tree edge (x,y), compute detour cost: dist_fwd[x] + w(x,y) + dist_bwd[y]
// 5. Use LCA on SPT to determine which path edge each non-tree edge can replace
// 6. For each path edge, replacement distance = min detour cost among applicable non-tree edges
FragilityResult hershberger_suri(const ArrayGraph& graph,
                                  NodeID source, NodeID target);

}  // namespace gravel
