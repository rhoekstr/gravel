#include "gravel/geo/border_edges.h"
#include <algorithm>
#include <limits>

namespace gravel {

BorderEdgeResult summarize_border_edges(
    const ArrayGraph& graph,
    const RegionAssignment& assignment) {

    BorderEdgeResult result;
    NodeID n = graph.node_count();

    for (NodeID u = 0; u < n; ++u) {
        int32_t u_region = assignment.region_index[u];
        auto targets = graph.outgoing_targets(u);
        auto weights = graph.outgoing_weights(u);

        for (size_t i = 0; i < targets.size(); ++i) {
            NodeID v = targets[i];
            int32_t v_region = assignment.region_index[v];

            if (u_region == v_region) continue;

            // Edges touching unassigned nodes
            if (u_region < 0 || v_region < 0) {
                result.unassigned_edges++;
                continue;
            }

            // Order regions so region_a < region_b
            RegionPair key{std::min(u_region, v_region), std::max(u_region, v_region)};
            double w = static_cast<double>(weights[i]);

            auto it = result.pair_summaries.find(key);
            if (it == result.pair_summaries.end()) {
                BorderEdgeSummary summary;
                summary.regions = key;
                summary.edge_count = 1;
                summary.total_weight = w;
                summary.min_weight = w;
                summary.max_weight = w;
                result.pair_summaries[key] = summary;
            } else {
                auto& s = it->second;
                s.edge_count++;
                s.total_weight += w;
                s.min_weight = std::min(s.min_weight, w);
                s.max_weight = std::max(s.max_weight, w);
            }
        }
    }

    result.connected_pairs = static_cast<uint32_t>(result.pair_summaries.size());
    result.total_border_edges = 0;
    for (const auto& [_, s] : result.pair_summaries) {
        result.total_border_edges += s.edge_count;
    }

    return result;
}

}  // namespace gravel
