#include "gravel/fragility/edge_fragility.h"

#include "gravel/ch/blocked_ch_query.h"
#include "gravel/simplify/bridges.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gravel {
namespace {

inline uint64_t pack(NodeID a, NodeID b) {
    return (static_cast<uint64_t>(a) << 32) | static_cast<uint64_t>(b);
}

}  // namespace

EdgeFragilityResult edge_fragility(const ContractionResult& ch,
                                   const ShortcutIndex& shortcut_idx,
                                   const ArrayGraph& graph,
                                   EdgeFragilityConfig config) {
    const NodeID n = graph.node_count();
    const EdgeID m = graph.edge_count();
    const auto& off = graph.raw_offsets();
    const auto& tgt = graph.raw_targets();

    EdgeFragilityResult r;
    r.fragility_ratio.assign(m, 1.0);
    r.replacement_distance.assign(m, INF_WEIGHT);
    r.is_bridge.assign(m, 0);
    r.stranded_count.assign(m, 0);

    // Bridges + cut sizes in one vector-indexed Tarjan pass (per CSR edge, O(V+E)).
    const EdgeBridgeInfo bi = bridge_edge_info(graph);
    r.is_bridge = bi.is_bridge;
    if (config.compute_stranded) r.stranded_count = bi.cut_size;

    if (!config.compute_ratio) return r;

    // Path-inflation ratio: block each non-bridge undirected edge and re-query its endpoints.
    // Dedup to undirected edges (each carries its bridge flag), then scatter the result onto
    // every CSR edge (both directed halves + any parallel circuits).
    std::vector<std::pair<NodeID, NodeID>> uedges;
    std::vector<uint8_t> ubridge;
    std::unordered_map<uint64_t, uint32_t> uidx;  // pack(min,max) -> index into uedges
    uidx.reserve(static_cast<size_t>(m) + 1);
    for (NodeID u = 0; u < n; ++u) {
        for (uint32_t e = off[u]; e < off[u + 1]; ++e) {
            if (u == tgt[e]) continue;  // self-loop: leave defaults (ratio 1.0)
            const NodeID a = std::min(u, tgt[e]), b = std::max(u, tgt[e]);
            if (uidx.emplace(pack(a, b), static_cast<uint32_t>(uedges.size())).second) {
                uedges.push_back({a, b});
                ubridge.push_back(bi.is_bridge[e]);
            }
        }
    }

    constexpr double INF_RATIO = std::numeric_limits<double>::infinity();
    const int64_t ne = static_cast<int64_t>(uedges.size());
    std::vector<double> u_ratio(ne, INF_RATIO);
    std::vector<Weight> u_repl(ne, INF_WEIGHT);

    #pragma omp parallel if(ne > 8)
    {
        BlockedCHQuery blocked(ch, shortcut_idx, graph);
        #pragma omp for schedule(dynamic)
        for (int64_t i = 0; i < ne; ++i) {
            if (ubridge[i]) continue;  // bridge: replacement + ratio stay INF (the defaults)
            const NodeID a = uedges[i].first, b = uedges[i].second;
            const Weight primary = blocked.distance_blocking(a, b, {});
            const Weight repl = blocked.distance_blocking(a, b, {{a, b}, {b, a}});
            u_repl[i] = repl;
            u_ratio[i] = (repl >= INF_WEIGHT || primary <= 0)
                             ? INF_RATIO
                             : static_cast<double>(repl) / static_cast<double>(primary);
        }
    }

    for (NodeID u = 0; u < n; ++u) {
        for (uint32_t e = off[u]; e < off[u + 1]; ++e) {
            if (u == tgt[e]) continue;
            const uint32_t i = uidx[pack(std::min(u, tgt[e]), std::max(u, tgt[e]))];
            r.fragility_ratio[e] = u_ratio[i];
            r.replacement_distance[e] = u_repl[i];
        }
    }
    return r;
}

}  // namespace gravel
