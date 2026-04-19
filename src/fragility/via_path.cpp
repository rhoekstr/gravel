#include "gravel/fragility/via_path.h"
#include "gravel/ch/ch_query.h"
#include "gravel/core/four_heap.h"
#include <algorithm>
#include <unordered_set>
#include <utility>

namespace gravel {

namespace {

// Full one-directional CH search (no early termination).
// Returns (distances, parents) via generation-counter workspace.
struct FullSearchResult {
    std::vector<Weight> dist;
    std::vector<NodeID> parent;
};

FullSearchResult full_ch_forward(const ContractionResult& ch, NodeID source) {
    uint32_t n = ch.num_nodes;
    FullSearchResult r;
    r.dist.assign(n, INF_WEIGHT);
    r.parent.assign(n, INVALID_NODE);
    r.dist[source] = 0.0;

    FourHeap pq;
    pq.push(source, 0.0);

    while (!pq.empty()) {
        auto [u, du] = pq.pop();
        if (du > r.dist[u]) continue;  // stale

        uint32_t start = ch.up_offsets[u];
        uint32_t end = ch.up_offsets[u + 1];
        for (uint32_t i = start; i < end; ++i) {
            NodeID v = ch.up_targets[i];
            Weight nd = du + ch.up_weights[i];
            if (nd < r.dist[v]) {
                r.dist[v] = nd;
                r.parent[v] = u;
                pq.push(v, nd);
            }
        }
    }
    return r;
}

FullSearchResult full_ch_backward(const ContractionResult& ch, NodeID target) {
    uint32_t n = ch.num_nodes;
    FullSearchResult r;
    r.dist.assign(n, INF_WEIGHT);
    r.parent.assign(n, INVALID_NODE);
    r.dist[target] = 0.0;

    FourHeap pq;
    pq.push(target, 0.0);

    while (!pq.empty()) {
        auto [u, du] = pq.pop();
        if (du > r.dist[u]) continue;

        uint32_t start = ch.down_offsets[u];
        uint32_t end = ch.down_offsets[u + 1];
        for (uint32_t i = start; i < end; ++i) {
            NodeID v = ch.down_targets[i];
            Weight nd = du + ch.down_weights[i];
            if (nd < r.dist[v]) {
                r.dist[v] = nd;
                r.parent[v] = u;
                pq.push(v, nd);
            }
        }
    }
    return r;
}

}  // namespace

std::vector<AlternateRouteResult> find_alternative_routes(
    const ContractionResult& ch,
    NodeID source, NodeID target,
    ViaPathConfig config) {
    if (source == target) return {};

    // 1. Get primary path and distance.
    CHQuery query(ch);
    auto primary = query.route(source, target);
    if (primary.distance >= INF_WEIGHT) return {};

    // 2. Full forward and backward CH searches.
    auto fwd = full_ch_forward(ch, source);
    auto bwd = full_ch_backward(ch, target);

    Weight max_dist = config.max_stretch * primary.distance;

    // Build set of primary path edges for sharing computation.
    std::unordered_set<uint64_t> primary_edges;
    for (size_t i = 0; i + 1 < primary.path.size(); ++i) {
        primary_edges.insert(ContractionResult::pack_edge(
            primary.path[i], primary.path[i + 1]));
    }

    // Set of primary path nodes (to skip via nodes on the primary path).
    std::unordered_set<NodeID> primary_nodes(primary.path.begin(), primary.path.end());

    // 3. Collect candidate via nodes.
    struct Candidate {
        NodeID via;
        Weight total_dist;
    };
    std::vector<Candidate> candidates;

    uint32_t n = ch.num_nodes;
    for (NodeID v = 0; v < n; ++v) {
        if (fwd.dist[v] >= INF_WEIGHT || bwd.dist[v] >= INF_WEIGHT) continue;
        Weight total = fwd.dist[v] + bwd.dist[v];
        if (total > max_dist) continue;
        if (primary_nodes.count(v)) continue;
        candidates.push_back({v, total});
    }

    // Sort by total distance (stretch).
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.total_dist < b.total_dist;
              });

    // 4. For each candidate, unpack path and check sharing.
    std::vector<AlternateRouteResult> results;

    for (const auto& cand : candidates) {
        if (results.size() >= config.max_alternatives) break;

        // Unpack s→via path (up-graph from source to via).
        auto s_via = query.route(source, cand.via);
        auto via_t = query.route(cand.via, target);

        if (s_via.distance >= INF_WEIGHT || via_t.distance >= INF_WEIGHT) continue;

        // Concatenate paths (remove duplicate via node).
        AlternateRouteResult alt;
        alt.path = std::move(s_via.path);
        for (size_t i = 1; i < via_t.path.size(); ++i)
            alt.path.push_back(via_t.path[i]);
        alt.distance = s_via.distance + via_t.distance;
        alt.stretch = alt.distance / primary.distance;

        if (alt.stretch > config.max_stretch) continue;

        // Compute sharing: fraction of alternative edges that are also in primary.
        uint32_t shared = 0;
        uint32_t total_edges = 0;
        for (size_t i = 0; i + 1 < alt.path.size(); ++i) {
            uint64_t key = ContractionResult::pack_edge(alt.path[i], alt.path[i + 1]);
            if (primary_edges.count(key)) ++shared;
            ++total_edges;
        }
        alt.sharing = total_edges > 0 ? double(shared) / double(total_edges) : 0.0;

        if (alt.sharing >= config.max_sharing) continue;

        // Check for duplicate paths (same via might give similar results).
        bool duplicate = false;
        for (const auto& existing : results) {
            if (existing.path == alt.path) { duplicate = true; break; }
        }
        if (duplicate) continue;

        results.push_back(std::move(alt));
    }

    return results;
}

}  // namespace gravel
