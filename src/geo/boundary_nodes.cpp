#include "gravel/geo/boundary_nodes.h"

namespace gravel {

std::unordered_set<NodeID> boundary_nodes(
    const ArrayGraph& graph,
    const RegionAssignment& assignment) {

    std::unordered_set<NodeID> result;
    NodeID n = graph.node_count();

    for (NodeID u = 0; u < n; ++u) {
        int32_t u_region = assignment.region_index[u];

        // Unassigned nodes are always in the protection set
        if (u_region < 0) {
            result.insert(u);
            continue;
        }

        auto targets = graph.outgoing_targets(u);
        for (NodeID v : targets) {
            int32_t v_region = assignment.region_index[v];
            if (v_region != u_region) {
                result.insert(u);
                result.insert(v);
                break;
            }
        }
    }

    return result;
}

}  // namespace gravel
