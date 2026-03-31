/// @file progressive_sssp.h
/// @brief Shared SSSP infrastructure for progressive fragility strategies.
///
/// Internal header — not part of the public API. Contains the local subgraph
/// SSSP engine, OD pair sampling, incremental edge restoration, and composite
/// score computation shared by Monte Carlo and greedy betweenness strategies.

#pragma once

#include "gravel/core/array_graph.h"
#include "gravel/core/subgraph.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gravel {
namespace progressive {

using LocalID = uint32_t;
static constexpr Weight INF = std::numeric_limits<Weight>::max() / 2;

struct LocalEdge { LocalID to; Weight weight; };
using LocalAdj = std::vector<std::vector<LocalEdge>>;

/// Canonical undirected edge key for blocked-set lookups.
inline uint64_t edge_key(LocalID u, LocalID v) {
    if (u > v) std::swap(u, v);
    return (uint64_t(u) << 32) | uint64_t(v);
}

/// Full SSSP from `source`, skipping edges in `blocked`.
inline std::vector<Weight> sssp(
    const LocalAdj& adj,
    LocalID source,
    const std::unordered_set<uint64_t>& blocked) {

    const uint32_t N = static_cast<uint32_t>(adj.size());
    std::vector<Weight> dist(N, INF);
    dist[source] = 0;

    using Entry = std::pair<Weight, LocalID>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;
    pq.push({0, source});

    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > dist[u]) continue;
        for (const auto& e : adj[u]) {
            if (blocked.count(edge_key(u, e.to))) continue;
            Weight nd = dist[u] + e.weight;
            if (nd < dist[e.to]) {
                dist[e.to] = nd;
                pq.push({nd, e.to});
            }
        }
    }
    return dist;
}

/// Incremental update after restoring edge (u, v, w).
/// `blocked` must already have the edge removed. `dist` updated in-place.
/// Propagation stops when no improvement is possible.
inline void incremental_restore(
    const LocalAdj& adj,
    const std::unordered_set<uint64_t>& blocked,
    std::vector<Weight>& dist,
    LocalID u, LocalID v, Weight w) {

    using Entry = std::pair<Weight, LocalID>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;

    auto try_seed = [&](LocalID node, Weight candidate) {
        if (candidate < dist[node]) {
            dist[node] = candidate;
            pq.push({candidate, node});
        }
    };
    try_seed(v, dist[u] + w);
    try_seed(u, dist[v] + w);

    while (!pq.empty()) {
        auto [d, node] = pq.top(); pq.pop();
        if (d > dist[node]) continue;
        for (const auto& e : adj[node]) {
            if (blocked.count(edge_key(node, e.to))) continue;
            Weight nd = dist[node] + e.weight;
            if (nd < dist[e.to]) {
                dist[e.to] = nd;
                pq.push({nd, e.to});
            }
        }
    }
}

/// Build compact local adjacency from a subgraph.
inline LocalAdj build_local_adj(const ArrayGraph& subgraph) {
    const uint32_t N = subgraph.node_count();
    LocalAdj adj(N);
    for (LocalID u = 0; u < N; ++u) {
        auto targets = subgraph.outgoing_targets(u);
        auto weights = subgraph.outgoing_weights(u);
        adj[u].reserve(targets.size());
        for (size_t i = 0; i < targets.size(); ++i)
            adj[u].push_back({static_cast<LocalID>(targets[i]), weights[i]});
    }
    return adj;
}

/// An origin-destination pair in local subgraph IDs.
struct ODPair {
    LocalID source, target;
    Weight primary_dist;  ///< Unblocked shortest distance (baseline)
};

/// Shared context for SSSP-based progressive evaluation.
struct StrategyContext {
    LocalAdj adj;
    std::vector<ODPair> od_pairs;

    /// Unique source nodes and their index.
    std::vector<LocalID> sources;
    std::unordered_map<LocalID, uint32_t> source_idx;

    /// Full-graph (unblocked) SSSP from each unique source.
    std::vector<std::vector<Weight>> dist_full;
};

/// Build strategy context: local adjacency, OD pairs, baseline SSSP.
inline StrategyContext build_context(
    const ArrayGraph& subgraph,
    uint32_t od_count,
    uint64_t seed) {

    StrategyContext ctx;
    ctx.adj = build_local_adj(subgraph);

    const uint32_t N = subgraph.node_count();
    const std::unordered_set<uint64_t> empty_blocked;

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<LocalID> ndist(0, N - 1);
    if (od_count == 0) od_count = 20;

    std::unordered_map<LocalID, std::vector<Weight>> sssp_cache;

    for (uint32_t att = 0; att < od_count * 5 && ctx.od_pairs.size() < od_count; ++att) {
        LocalID s = ndist(rng), t = ndist(rng);
        if (s == t) continue;
        auto& ds = sssp_cache[s];
        if (ds.empty()) ds = sssp(ctx.adj, s, empty_blocked);
        if (ds[t] < INF) ctx.od_pairs.push_back({s, t, ds[t]});
    }

    // Collect unique sources
    for (const auto& p : ctx.od_pairs) {
        if (!ctx.source_idx.count(p.source)) {
            ctx.source_idx[p.source] = static_cast<uint32_t>(ctx.sources.size());
            ctx.sources.push_back(p.source);
        }
    }

    // Build full-graph SSSP per source
    ctx.dist_full.resize(ctx.sources.size());
    for (size_t si = 0; si < ctx.sources.size(); ++si) {
        auto it = sssp_cache.find(ctx.sources[si]);
        if (it != sssp_cache.end())
            ctx.dist_full[si] = std::move(it->second);
        else
            ctx.dist_full[si] = sssp(ctx.adj, ctx.sources[si], empty_blocked);
    }

    return ctx;
}

/// Compute composite degradation score from OD pair distances.
/// 0 = no degradation, 1 = fully degraded.
/// Formula: 0.6 * (1 - reachability) + 0.4 * min(1, (avg_stretch - 1) / 5)
inline double composite_score(
    const std::vector<ODPair>& od_pairs,
    const std::vector<Weight>& pair_dists) {

    const size_t np = od_pairs.size();
    if (np == 0) return 0.0;

    uint32_t reachable = 0;
    double sum_ratio = 0.0;
    for (size_t p = 0; p < np; ++p) {
        if (pair_dists[p] < INF) {
            ++reachable;
            sum_ratio += static_cast<double>(pair_dists[p]) /
                         static_cast<double>(od_pairs[p].primary_dist);
        }
    }
    double reach_frac = static_cast<double>(reachable) / np;
    double avg_ratio  = reachable > 0 ? sum_ratio / reachable : 0.0;
    return 0.6 * (1.0 - reach_frac) +
           0.4 * std::min(1.0, (avg_ratio - 1.0) / 5.0);
}

/// Collect current OD pair distances from per-source distance arrays.
inline std::vector<Weight> collect_pair_dists(
    const StrategyContext& ctx,
    const std::vector<std::vector<Weight>>& dist) {

    std::vector<Weight> pair_dists(ctx.od_pairs.size());
    for (size_t p = 0; p < ctx.od_pairs.size(); ++p) {
        uint32_t si = ctx.source_idx.at(ctx.od_pairs[p].source);
        pair_dists[p] = dist[si][ctx.od_pairs[p].target];
    }
    return pair_dists;
}

}  // namespace progressive
}  // namespace gravel
