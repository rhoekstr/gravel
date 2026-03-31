#include "gravel/fragility/route_fragility.h"
#include "gravel/ch/blocked_ch_query.h"
#include "gravel/ch/ch_query.h"
#include <cmath>

namespace gravel {

FragilityResult route_fragility(const ContractionResult& ch,
                                 const ShortcutIndex& shortcut_idx,
                                 const ArrayGraph& graph,
                                 NodeID source, NodeID target,
                                 FragilityConfig /*config*/) {
    FragilityResult result;

    // 1. Primary path via CH query.
    CHQuery query(ch);
    auto route = query.route(source, target);
    result.primary_distance = route.distance;
    result.primary_path = std::move(route.path);

    if (result.primary_distance >= INF_WEIGHT || result.primary_path.size() < 2)
        return result;

    size_t num_edges = result.primary_path.size() - 1;
    result.edge_fragilities.resize(num_edges);

    // 2. For each path edge, compute replacement distance.
    BlockedCHQuery blocked(ch, shortcut_idx, graph);

    for (size_t i = 0; i < num_edges; ++i) {
        NodeID u = result.primary_path[i];
        NodeID v = result.primary_path[i + 1];

        EdgeFragility& ef = result.edge_fragilities[i];
        ef.source = u;
        ef.target = v;
        ef.replacement_distance = blocked.distance_blocking(source, target, {{u, v}});

        if (ef.replacement_distance >= INF_WEIGHT) {
            ef.fragility_ratio = std::numeric_limits<double>::infinity();
        } else {
            ef.fragility_ratio = ef.replacement_distance / result.primary_distance;
        }
    }

    return result;
}

std::vector<FragilityResult> batch_fragility(
    const ContractionResult& ch,
    const ShortcutIndex& shortcut_idx,
    const ArrayGraph& graph,
    const std::vector<std::pair<NodeID, NodeID>>& od_pairs,
    FragilityConfig config) {
    std::vector<FragilityResult> results(od_pairs.size());

    #pragma omp parallel
    {
        // Thread-local instances for parallel safety.
        CHQuery local_query(ch);
        BlockedCHQuery local_blocked(ch, shortcut_idx, graph);

        #pragma omp for schedule(dynamic)
        for (size_t idx = 0; idx < od_pairs.size(); ++idx) {
            auto [s, t] = od_pairs[idx];
            FragilityResult& result = results[idx];

            auto route = local_query.route(s, t);
            result.primary_distance = route.distance;
            result.primary_path = std::move(route.path);

            if (result.primary_distance >= INF_WEIGHT || result.primary_path.size() < 2)
                continue;

            size_t num_edges = result.primary_path.size() - 1;
            result.edge_fragilities.resize(num_edges);

            for (size_t i = 0; i < num_edges; ++i) {
                NodeID u = result.primary_path[i];
                NodeID v = result.primary_path[i + 1];

                EdgeFragility& ef = result.edge_fragilities[i];
                ef.source = u;
                ef.target = v;
                ef.replacement_distance = local_blocked.distance_blocking(s, t, {{u, v}});

                if (ef.replacement_distance >= INF_WEIGHT) {
                    ef.fragility_ratio = std::numeric_limits<double>::infinity();
                } else {
                    ef.fragility_ratio = ef.replacement_distance / result.primary_distance;
                }
            }
        }
    }

    return results;
}

}  // namespace gravel
