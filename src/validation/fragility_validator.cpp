#include "gravel/validation/fragility_validator.h"
#include "gravel/fragility/route_fragility.h"
#include "gravel/ch/blocked_ch_query.h"
#include "gravel/ch/ch_query.h"
#include "gravel/core/dijkstra.h"
#include <random>
#include <cmath>

namespace gravel {

static std::unique_ptr<ArrayGraph> remove_edge_local(const ArrayGraph& g, NodeID u, NodeID v) {
    std::vector<Edge> edges;
    for (NodeID src = 0; src < g.node_count(); ++src) {
        auto targets = g.outgoing_targets(src);
        auto weights = g.outgoing_weights(src);
        for (uint32_t i = 0; i < targets.size(); ++i) {
            if (src == u && targets[i] == v) continue;
            edges.push_back({src, targets[i], weights[i]});
        }
    }
    return std::make_unique<ArrayGraph>(g.node_count(), std::move(edges));
}

FragilityValidationReport validate_fragility(
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const ArrayGraph& graph,
    FragilityValidationConfig config) {
    FragilityValidationReport report;
    uint32_t n = graph.node_count();
    std::mt19937 rng(config.seed);

    for (uint32_t p = 0; p < config.sample_count; ++p) {
        NodeID s = rng() % n;
        NodeID t = rng() % n;
        if (s == t) continue;

        auto result = route_fragility(ch, idx, graph, s, t);
        if (!result.valid()) continue;

        ++report.pairs_tested;

        for (const auto& ef : result.edge_fragilities) {
            ++report.edges_tested;

            auto modified = remove_edge_local(graph, ef.source, ef.target);
            Weight oracle = dijkstra_pair(*modified, s, t);

            double err = 0.0;
            if (oracle >= INF_WEIGHT && ef.replacement_distance >= INF_WEIGHT) {
                // Both INF — ok
            } else if (oracle >= INF_WEIGHT || ef.replacement_distance >= INF_WEIGHT) {
                report.passed = false;
                ++report.mismatches;
            } else {
                err = std::abs(ef.replacement_distance - oracle);
                if (err > config.tolerance) {
                    report.passed = false;
                    ++report.mismatches;
                }
            }
            if (err > report.max_absolute_error)
                report.max_absolute_error = err;
        }
    }
    return report;
}

FragilityValidationReport validate_shortcut_interaction(
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const ArrayGraph& graph,
    FragilityValidationConfig config) {
    FragilityValidationReport report;
    uint32_t n = graph.node_count();
    std::mt19937 rng(config.seed);

    BlockedCHQuery blocked(ch, idx, graph);
    CHQuery query(ch);

    for (uint32_t p = 0; p < config.sample_count; ++p) {
        NodeID s = rng() % n;
        NodeID t = rng() % n;
        if (s == t) continue;

        auto route = query.route(s, t);
        if (route.distance >= INF_WEIGHT || route.path.size() < 2) continue;

        ++report.pairs_tested;

        // Block each edge on the path and verify against oracle.
        for (size_t i = 0; i + 1 < route.path.size(); ++i) {
            NodeID u = route.path[i];
            NodeID v = route.path[i + 1];
            ++report.edges_tested;

            Weight ch_blocked = blocked.distance_blocking(s, t, {{u, v}});

            auto modified = remove_edge_local(graph, u, v);
            Weight oracle = dijkstra_pair(*modified, s, t);

            double err = 0.0;
            if (oracle >= INF_WEIGHT && ch_blocked >= INF_WEIGHT) {
                // ok
            } else if (oracle >= INF_WEIGHT || ch_blocked >= INF_WEIGHT) {
                report.passed = false;
                ++report.mismatches;
            } else {
                err = std::abs(ch_blocked - oracle);
                if (err > config.tolerance) {
                    report.passed = false;
                    ++report.mismatches;
                }
            }
            if (err > report.max_absolute_error)
                report.max_absolute_error = err;
        }
    }
    return report;
}

}  // namespace gravel
