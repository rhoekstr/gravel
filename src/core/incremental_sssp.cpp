#include "gravel/core/incremental_sssp.h"
#include "gravel/core/array_graph.h"
#include <queue>
#include <utility>
#include <functional>
#include <unordered_set>
#include <vector>

namespace gravel {

LocalGraph build_local_graph(const ArrayGraph& subgraph) {
    LocalGraph g;
    g.n_nodes = subgraph.node_count();
    g.adj.resize(g.n_nodes);

    for (uint32_t u = 0; u < g.n_nodes; ++u) {
        auto targets = subgraph.outgoing_targets(u);
        auto weights = subgraph.outgoing_weights(u);
        g.adj[u].reserve(targets.size());
        for (size_t i = 0; i < targets.size(); ++i)
            g.adj[u].push_back({static_cast<uint32_t>(targets[i]), weights[i]});
    }

    g.local_to_original.resize(g.n_nodes);
    for (uint32_t i = 0; i < g.n_nodes; ++i)
        g.local_to_original[i] = i;  // identity mapping for direct subgraph

    return g;
}

std::vector<Weight> IncrementalSSSP::sssp(
    const LocalGraph& g, uint32_t source,
    const std::unordered_set<uint64_t>& blocked) {

    std::vector<Weight> d(g.n_nodes, INF);
    d[source] = 0;

    using Entry = std::pair<Weight, uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;
    pq.push({0, source});

    while (!pq.empty()) {
        auto [dist, u] = pq.top(); pq.pop();
        if (dist > d[u]) continue;
        for (const auto& e : g.adj[u]) {
            if (blocked.count(edge_key(u, e.to))) continue;
            Weight nd = d[u] + e.weight;
            if (nd < d[e.to]) {
                d[e.to] = nd;
                pq.push({nd, e.to});
            }
        }
    }
    return d;
}

void IncrementalSSSP::propagate(
    const LocalGraph& g,
    const std::unordered_set<uint64_t>& blocked,
    std::vector<Weight>& dist,
    uint32_t u, uint32_t v, Weight w) {

    using Entry = std::pair<Weight, uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;

    auto try_seed = [&](uint32_t node, Weight candidate) {
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
        for (const auto& e : g.adj[node]) {
            if (blocked.count(edge_key(node, e.to))) continue;
            Weight nd = dist[node] + e.weight;
            if (nd < dist[e.to]) {
                dist[e.to] = nd;
                pq.push({nd, e.to});
            }
        }
    }
}

IncrementalSSSP::IncrementalSSSP(
    const LocalGraph& graph,
    const std::vector<uint32_t>& sources,
    const std::unordered_set<uint64_t>& blocked)
    : graph_(graph), sources_(sources), blocked_(blocked) {

    const size_t ns = sources_.size();

    // Compute full-graph (unblocked) distances
    const std::unordered_set<uint64_t> empty;
    dist_full_.resize(ns);
    for (size_t si = 0; si < ns; ++si)
        dist_full_[si] = sssp(graph_, sources_[si], empty);

    // Compute blocked distances
    dist_.resize(ns);
    for (size_t si = 0; si < ns; ++si)
        dist_[si] = sssp(graph_, sources_[si], blocked_);
}

IncrementalSSSP::IncrementalSSSP(
    const LocalGraph& graph,
    const std::vector<uint32_t>& sources,
    const std::unordered_set<uint64_t>& blocked,
    const std::vector<std::vector<Weight>>& dist_full)
    : graph_(graph), sources_(sources), blocked_(blocked), dist_full_(dist_full) {

    const size_t ns = sources_.size();
    dist_.resize(ns);
    for (size_t si = 0; si < ns; ++si)
        dist_[si] = sssp(graph_, sources_[si], blocked_);
}

void IncrementalSSSP::restore_edge(uint32_t u, uint32_t v, Weight w) {
    blocked_.erase(edge_key(u, v));

    for (size_t si = 0; si < sources_.size(); ++si) {
        // Skip if both endpoints are already at unblocked optimum
        if (dist_[si][u] == dist_full_[si][u] &&
            dist_[si][v] == dist_full_[si][v])
            continue;

        // Check if restored edge can shorten any path from this source
        bool helps = (dist_[si][u] + w < dist_[si][v]) ||
                     (dist_[si][v] + w < dist_[si][u]);
        if (helps)
            propagate(graph_, blocked_, dist_[si], u, v, w);
    }
}

}  // namespace gravel
