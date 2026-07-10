#include "gravel/analysis/cascade_fragility.h"

#include <limits>
#include <unordered_set>
#include <vector>

namespace gravel {

namespace {

// Find the CSR edge index for directed edge (u, v). Returns edge_count() if absent.
EdgeID find_edge(const ArrayGraph& g, NodeID u, NodeID v) {
    if (u >= g.node_count()) return g.edge_count();
    const auto& off = g.raw_offsets();
    const auto& tgt = g.raw_targets();
    for (uint32_t e = off[u]; e < off[u + 1]; ++e)
        if (tgt[e] == v) return e;
    return g.edge_count();
}

}  // namespace

CascadeFragilityResult cascade_fragility(const ArrayGraph& graph,
                                          const CascadeFragilityConfig& config) {
    const EdgeID m = graph.edge_count();
    CascadeFragilityResult result;
    if (m == 0) return result;

    const auto& offsets = graph.raw_offsets();
    const auto& targets = graph.raw_targets();
    const auto& weights = graph.raw_weights();
    const auto& coords = graph.raw_coords();

    std::vector<NodeID> src_of_edge(m);
    for (NodeID u = 0; u + 1 < offsets.size(); ++u)
        for (uint32_t e = offsets[u]; e < offsets[u + 1]; ++e)
            src_of_edge[e] = u;

    // Initial load = betweenness on the intact graph.
    const BetweennessResult init = edge_betweenness(graph, config.betweenness_config);
    const std::vector<double>& load0 = init.edge_scores;

    // Capacity = (1+α)·initial_load. Zero-initial-load edges get infinite capacity (never
    // overloaded — they carry no load in normal operation), which avoids a degenerate
    // runaway cascade.
    std::vector<double> capacity(m, std::numeric_limits<double>::infinity());
    for (EdgeID e = 0; e < m; ++e)
        if (load0[e] > 0.0)
            capacity[e] = (1.0 + config.alpha) * load0[e];

    // Trigger the initial failure(s).
    std::unordered_set<EdgeID> failed;
    if (config.trigger_edges.empty()) {
        EdgeID best = 0;
        double best_load = -1.0;
        for (EdgeID e = 0; e < m; ++e)
            if (load0[e] > best_load) { best_load = load0[e]; best = e; }
        failed.insert(best);
    } else {
        for (const auto& [u, v] : config.trigger_edges) {
            EdgeID e = find_edge(graph, u, v);
            if (e < m) failed.insert(e);
        }
    }
    result.trigger_size = static_cast<uint32_t>(failed.size());

    // Iterate: recompute betweenness on the degraded graph (failed edges masked with
    // infinite weight, so edge indexing is preserved), fail newly-overloaded edges.
    for (uint32_t it = 0; it < config.max_iterations; ++it) {
        std::vector<Weight> wmod(weights);
        for (EdgeID e : failed) wmod[e] = INF_WEIGHT;
        ArrayGraph degraded(std::vector<uint32_t>(offsets),
                            std::vector<NodeID>(targets),
                            std::move(wmod),
                            std::vector<Coord>(coords));

        const BetweennessResult b = edge_betweenness(degraded, config.betweenness_config);

        std::vector<EdgeID> newly;
        for (EdgeID e = 0; e < m; ++e) {
            if (failed.count(e)) continue;
            if (b.edge_scores[e] > capacity[e]) newly.push_back(e);
        }
        if (newly.empty()) break;
        for (EdgeID e : newly) failed.insert(e);
        result.iterations = it + 1;
    }

    // Fragmentation severity: the largest connected component of the surviving (non-failed)
    // undirected graph, as a fraction of all nodes. Union-find over the endpoints of every
    // edge that did not fail; a purely topological reading of how shattered the network is.
    const NodeID n = graph.node_count();
    std::vector<NodeID> parent(n);
    for (NodeID v = 0; v < n; ++v) parent[v] = v;
    auto find = [&](NodeID x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    for (EdgeID e = 0; e < m; ++e) {
        if (failed.count(e)) continue;
        const NodeID a = find(src_of_edge[e]), b = find(targets[e]);
        if (a != b) parent[a] = b;
    }
    std::vector<uint32_t> comp_size(n, 0);
    uint32_t largest = 0;
    for (NodeID v = 0; v < n; ++v) {
        const uint32_t s = ++comp_size[find(v)];
        if (s > largest) largest = s;
    }

    result.cascade_size = static_cast<uint32_t>(failed.size());
    result.cascade_fraction = static_cast<double>(failed.size()) / static_cast<double>(m);
    result.largest_component_fraction =
        n ? static_cast<double>(largest) / static_cast<double>(n) : 1.0;
    result.failed_edges.reserve(failed.size());
    for (EdgeID e : failed) result.failed_edges.push_back({src_of_edge[e], targets[e]});
    return result;
}

std::vector<CascadeAlphaPoint> cascade_vs_alpha(const ArrayGraph& graph,
                                                 CascadeFragilityConfig config,
                                                 const std::vector<double>& alphas) {
    std::vector<CascadeAlphaPoint> out;
    out.reserve(alphas.size());
    for (double a : alphas) {
        config.alpha = a;
        const CascadeFragilityResult r = cascade_fragility(graph, config);
        out.push_back({a, r.cascade_fraction, r.iterations});
    }
    return out;
}

}  // namespace gravel
