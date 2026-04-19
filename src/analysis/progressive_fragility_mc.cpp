/// @file progressive_fragility_mc.cpp
/// @brief Monte Carlo progressive elimination — reverse incremental SSSP algorithm.
///
/// Algorithm (per run):
///   1. Shuffle candidates → pick first k_max as the removal set for this run.
///   2. Block ALL k_max edges; run one full Dijkstra per OD source on the blocked subgraph
///      → dist[source][*] for every node.  This is the k=k_max (worst case) level.
///   3. Add edges back one at a time (reverse of removal order):
///        restore edge (u, v, w):
///          - remove from blocked set
///          - for each source: check if dist[s][u]+w < dist[s][v] or vice versa
///          - if so, run incremental Dijkstra from the improved endpoint, propagating
///            improvements outward and stopping as soon as the relaxation no longer wins
///          - skip source entirely if both endpoints are already at their unblocked
///            (full-graph) distance — no improvement is possible through this edge
///   4. Record composite score at each level.
///
/// Why this is efficient:
/// - Initial SSSP cost is amortised: one full Dijkstra per source per run (not per k level).
/// - Incremental updates are bounded: propagation stops the instant the through-new-edge
///   path is no longer shorter than the existing path.
/// - The blocked set shrinks each step, so propagation naturally reaches fewer nodes
///   over time (closer to unblocked optimum = less room to improve).
/// - OD pairs and sources are shared across runs for statistical comparability.

#include "gravel/analysis/progressive_fragility.h"
#include "progressive_sssp.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>
#include <utility>
#include <unordered_set>
#include <random>

namespace gravel {

using namespace progressive;

ProgressiveFragilityResult run_monte_carlo(
    const ArrayGraph& /*graph*/,
    const ContractionResult& /*ch*/,
    const ShortcutIndex& /*idx*/,
    const ProgressiveFragilityConfig& config,
    const std::vector<std::pair<NodeID, NodeID>>& candidate_edges,
    const SubgraphResult& sub,
    double baseline_composite) {

    ProgressiveFragilityResult result;
    uint32_t k_max = std::min(config.k_max,
                               static_cast<uint32_t>(candidate_edges.size()) - 1);
    result.k_max_used = k_max;

    result.curve.resize(k_max + 1);
    for (uint32_t k = 0; k <= k_max; ++k) result.curve[k].k = k;
    result.curve[0].mean_composite = baseline_composite;

    std::vector<std::vector<double>> per_k_values(k_max + 1);
    for (auto& v : per_k_values) v.reserve(config.monte_carlo_runs);
    per_k_values[0].assign(config.monte_carlo_runs, baseline_composite);

    // Build shared context: local adjacency, OD pairs, baseline SSSP
    auto ctx = build_context(*sub.graph, config.base_config.od_sample_count,
                             config.base_seed + 999999ULL);

    if (ctx.od_pairs.empty()) return result;

    // Map candidate_edges (original IDs) → local subgraph IDs with weights
    struct LocalCandidate { LocalID u, v; Weight weight; };
    std::vector<LocalCandidate> local_candidates;
    local_candidates.reserve(candidate_edges.size());

    for (const auto& [orig_u, orig_v] : candidate_edges) {
        auto it_u = sub.original_to_new.find(orig_u);
        auto it_v = sub.original_to_new.find(orig_v);
        if (it_u == sub.original_to_new.end() || it_v == sub.original_to_new.end()) continue;

        LocalID lu = static_cast<LocalID>(it_u->second);
        LocalID lv = static_cast<LocalID>(it_v->second);

        Weight w = INF;
        for (const auto& e : ctx.adj[lu]) {
            if (e.to == lv) { w = e.weight; break; }
        }
        if (w == INF) continue;

        local_candidates.push_back({lu, lv, w});
    }

    if (local_candidates.size() < 2) return result;

    const size_t ns = ctx.sources.size();
    const size_t np = ctx.od_pairs.size();

    // Monte Carlo runs — embarrassingly parallel
    int num_runs = static_cast<int>(config.monte_carlo_runs);

    #pragma omp parallel for schedule(dynamic) if(num_runs > 2)
    for (int run = 0; run < num_runs; ++run) {
        std::mt19937_64 rng(config.base_seed + static_cast<uint64_t>(run));

        auto pool = local_candidates;
        std::shuffle(pool.begin(), pool.end(), rng);
        std::vector<LocalCandidate> removal_order(
            pool.begin(), pool.begin() + k_max);

        // Block ALL k_max edges; full SSSP per source → level k_max
        std::unordered_set<uint64_t> blocked;
        blocked.reserve(k_max * 2);
        for (const auto& e : removal_order)
            blocked.insert(edge_key(e.u, e.v));

        std::vector<std::vector<Weight>> dist(ns);
        for (size_t si = 0; si < ns; ++si)
            dist[si] = sssp(ctx.adj, ctx.sources[si], blocked);

        auto pair_dists = collect_pair_dists(ctx, dist);

        #pragma omp critical
        { per_k_values[k_max].push_back(composite_score(ctx.od_pairs, pair_dists)); }

        // Add edges back: level k_max-1 down to k=1
        for (int ki = static_cast<int>(k_max) - 1; ki >= 1; --ki) {
            const auto& restored = removal_order[ki];
            blocked.erase(edge_key(restored.u, restored.v));

            for (size_t si = 0; si < ns; ++si) {
                if (dist[si][restored.u] == ctx.dist_full[si][restored.u] &&
                    dist[si][restored.v] == ctx.dist_full[si][restored.v])
                    continue;
                bool helps = (dist[si][restored.u] + restored.weight < dist[si][restored.v]) ||
                             (dist[si][restored.v] + restored.weight < dist[si][restored.u]);
                if (helps)
                    incremental_restore(ctx.adj, blocked, dist[si],
                                        restored.u, restored.v, restored.weight);
            }

            for (size_t p = 0; p < np; ++p) {
                uint32_t si = ctx.source_idx.at(ctx.od_pairs[p].source);
                pair_dists[p] = dist[si][ctx.od_pairs[p].target];
            }

            #pragma omp critical
            { per_k_values[ki].push_back(composite_score(ctx.od_pairs, pair_dists)); }
        }
    }

    // Compute per-k statistics
    for (uint32_t k = 1; k <= k_max; ++k) {
        auto& vals  = per_k_values[k];
        auto& level = result.curve[k];
        level.k = k;
        level.run_values = vals;
        if (vals.empty()) continue;

        std::sort(vals.begin(), vals.end());
        const size_t n = vals.size();

        const double sum = std::accumulate(vals.begin(), vals.end(), 0.0);
        level.mean_composite = sum / static_cast<double>(n);

        double sq = 0.0;
        for (double v : vals) sq += (v - level.mean_composite) * (v - level.mean_composite);
        level.std_composite = (n > 1) ? std::sqrt(sq / (n - 1)) : 0.0;

        level.p25 = vals[n * 25 / 100];
        level.p50 = vals[n / 2];
        level.p75 = vals[n * 75 / 100];
        level.p90 = vals[std::min(n - 1, n * 9 / 10)];
        level.iqr = level.p75 - level.p25;

        uint32_t disconnected = 0;
        for (double v : vals) if (v >= 0.99) ++disconnected;
        level.fraction_disconnected = static_cast<double>(disconnected) / n;
    }

    return result;
}

}  // namespace gravel
