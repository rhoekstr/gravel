#include "gravel/analysis/progressive_fragility.h"
#include "progressive_sssp.h"
#include <algorithm>
#include <utility>

namespace gravel {

using namespace progressive;

ProgressiveFragilityResult run_greedy_betweenness(
    const ArrayGraph& /*graph*/, const ContractionResult& /*ch*/, const ShortcutIndex& /*idx*/,
    const ProgressiveFragilityConfig& config,
    const std::vector<std::pair<NodeID, NodeID>>& candidate_edges,
    const SubgraphResult& subgraph,
    double baseline_composite) {

    ProgressiveFragilityResult result;
    uint32_t k_max = std::min(config.k_max,
                               static_cast<uint32_t>(candidate_edges.size()) - 1);
    result.k_max_used = k_max;
    result.curve.resize(k_max + 1);
    result.curve[0].k = 0;
    result.curve[0].mean_composite = baseline_composite;

    // Step 1: Rank candidate edges by betweenness
    BetweennessResult betweenness;
    if (config.precomputed_betweenness) {
        betweenness = *config.precomputed_betweenness;
    } else {
        BetweennessConfig bc;
        bc.sample_sources = config.base_config.betweenness_samples;
        bc.seed = config.base_config.seed;
        betweenness = edge_betweenness(*subgraph.graph, bc);
    }

    // Build edge info map: (local_u→local_v) → (weight, betweenness_score)
    struct EdgeInfo { LocalID lu, lv; Weight weight; double score; };
    std::unordered_map<uint64_t, EdgeInfo> edge_info_map;
    {
        uint32_t eidx = 0;
        for (LocalID u = 0; u < subgraph.graph->node_count(); ++u) {
            auto tgts = subgraph.graph->outgoing_targets(u);
            auto wts  = subgraph.graph->outgoing_weights(u);
            for (size_t i = 0; i < tgts.size(); ++i, ++eidx) {
                LocalID v = static_cast<LocalID>(tgts[i]);
                double s = (eidx < betweenness.edge_scores.size())
                            ? betweenness.edge_scores[eidx] : 0.0;
                uint64_t key = (uint64_t(u) << 32) | uint64_t(v);
                edge_info_map[key] = {u, v, wts[i], s};
            }
        }
    }

    struct ScoredEdge { std::pair<NodeID, NodeID> orig; LocalID lu, lv; Weight weight; double score; };
    std::vector<ScoredEdge> scored;
    scored.reserve(candidate_edges.size());

    for (const auto& [orig_u, orig_v] : candidate_edges) {
        auto itu = subgraph.original_to_new.find(orig_u);
        auto itv = subgraph.original_to_new.find(orig_v);
        if (itu == subgraph.original_to_new.end() || itv == subgraph.original_to_new.end()) continue;
        LocalID lu = static_cast<LocalID>(itu->second);
        LocalID lv = static_cast<LocalID>(itv->second);
        uint64_t key = (uint64_t(lu) << 32) | uint64_t(lv);
        auto it = edge_info_map.find(key);
        if (it == edge_info_map.end()) continue;
        scored.push_back({{orig_u, orig_v}, lu, lv, it->second.weight, it->second.score});
    }

    std::sort(scored.begin(), scored.end(),
              [](const ScoredEdge& a, const ScoredEdge& b) { return a.score > b.score; });

    if (scored.size() < 1) return result;

    // Step 2: Build shared SSSP context
    auto ctx = build_context(*subgraph.graph,
                             std::max(config.base_config.od_sample_count, 20u),
                             config.base_config.seed);
    if (ctx.od_pairs.empty()) return result;

    const size_t ns = ctx.sources.size();

    // Step 3: Reverse SSSP — block all k_max edges, add back one at a time
    std::vector<ScoredEdge*> removal_order;
    removal_order.reserve(k_max);
    for (uint32_t i = 0; i < k_max && i < scored.size(); ++i)
        removal_order.push_back(&scored[i]);

    std::unordered_set<uint64_t> blocked;
    for (const auto* e : removal_order)
        blocked.insert(edge_key(e->lu, e->lv));

    // Full SSSP with all k_max blocked
    std::vector<std::vector<Weight>> dist(ns);
    for (size_t si = 0; si < ns; ++si)
        dist[si] = sssp(ctx.adj, ctx.sources[si], blocked);

    auto pair_dists = collect_pair_dists(ctx, dist);

    result.curve[k_max].k = k_max;
    result.curve[k_max].mean_composite = composite_score(ctx.od_pairs, pair_dists);
    result.curve[k_max].removed_edge = removal_order[k_max - 1]->orig;

    // Add edges back from k_max-1 down to 1
    for (int ki = static_cast<int>(k_max) - 1; ki >= 1; --ki) {
        const auto& restored = *removal_order[ki];
        blocked.erase(edge_key(restored.lu, restored.lv));
        result.removal_sequence.push_back(restored.orig);

        for (size_t si = 0; si < ns; ++si) {
            if (dist[si][restored.lu] == ctx.dist_full[si][restored.lu] &&
                dist[si][restored.lv] == ctx.dist_full[si][restored.lv]) continue;
            bool helps = (dist[si][restored.lu] + restored.weight < dist[si][restored.lv]) ||
                         (dist[si][restored.lv] + restored.weight < dist[si][restored.lu]);
            if (helps)
                incremental_restore(ctx.adj, blocked, dist[si], restored.lu, restored.lv, restored.weight);
        }

        pair_dists = collect_pair_dists(ctx, dist);

        result.curve[ki].k = ki;
        result.curve[ki].mean_composite = composite_score(ctx.od_pairs, pair_dists);
        result.curve[ki].removed_edge = removal_order[ki - 1]->orig;
    }

    // Fix removal_sequence ordering
    std::reverse(result.removal_sequence.begin(), result.removal_sequence.end());
    result.removal_sequence.insert(result.removal_sequence.begin(), removal_order[0]->orig);

    return result;
}

ProgressiveFragilityResult run_greedy_fragility(
    const ArrayGraph& graph, const ContractionResult& ch, const ShortcutIndex& idx,
    const ProgressiveFragilityConfig& config,
    const std::vector<std::pair<NodeID, NodeID>>& candidate_edges,
    double baseline_composite) {

    ProgressiveFragilityResult result;
    uint32_t k_max = std::min(config.k_max,
                               static_cast<uint32_t>(candidate_edges.size()) - 1);
    result.k_max_used = k_max;
    result.curve.resize(k_max + 1);
    result.curve[0].k = 0;
    result.curve[0].mean_composite = baseline_composite;

    std::unordered_set<uint64_t> removed_set;
    std::vector<std::pair<NodeID, NodeID>> blocked;

    bool fast_mode = (config.recompute_mode == RecomputeMode::FAST_APPROXIMATE);

    for (uint32_t k = 1; k <= k_max; ++k) {
        int num_candidates = static_cast<int>(candidate_edges.size());
        std::vector<double> scores(num_candidates, -1.0);

        #pragma omp parallel for schedule(dynamic) if(num_candidates > 8)
        for (int c = 0; c < num_candidates; ++c) {
            auto [u, v] = candidate_edges[c];
            uint64_t key = ContractionResult::pack_edge(u, v);
            if (removed_set.count(key)) continue;

            auto trial_blocked = blocked;
            trial_blocked.push_back({u, v});

            auto cfg = config.base_config;
            cfg.blocked_edges = trial_blocked;
            if (fast_mode) cfg.skip_spectral = true;

            auto fr = county_fragility_index(graph, ch, idx, cfg);
            scores[c] = fr.composite_index;
        }

        int best_idx = -1;
        double best_score = -1.0;
        for (int c = 0; c < num_candidates; ++c) {
            if (scores[c] > best_score) {
                best_score = scores[c];
                best_idx = c;
            }
        }

        if (best_idx < 0) break;

        auto best_edge = candidate_edges[best_idx];
        blocked.push_back(best_edge);
        removed_set.insert(ContractionResult::pack_edge(best_edge.first, best_edge.second));
        result.removal_sequence.push_back(best_edge);

        result.curve[k].k = k;
        result.curve[k].mean_composite = best_score;
        result.curve[k].removed_edge = best_edge;
    }

    return result;
}

}  // namespace gravel
