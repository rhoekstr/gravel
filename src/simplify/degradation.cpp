#include "gravel/simplify/simplify.h"
#include "gravel/ch/ch_query.h"
#include "gravel/simplify/bridges.h"
#include "gravel/core/dijkstra.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <random>
#include <unordered_map>
#include <functional>

namespace gravel {

namespace {

// Find the nearest node in the simplified graph to an original-graph node.
// Uses the mapping if the node is preserved; otherwise BFS on original graph.
NodeID find_nearest_simplified(
    NodeID orig_node,
    const ArrayGraph& orig_graph,
    const std::unordered_map<NodeID, NodeID>& original_to_new,
    Weight& access_cost) {

    auto it = original_to_new.find(orig_node);
    if (it != original_to_new.end()) {
        access_cost = 0.0;
        return it->second;
    }

    // BFS/Dijkstra from orig_node, find closest kept node
    // Bounded to prevent excessive exploration
    static constexpr Weight MAX_ACCESS = 600.0;  // 10 minutes max access

    struct PQEntry {
        Weight dist;
        NodeID node;
        bool operator>(const PQEntry& o) const { return dist > o.dist; }
    };
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<>> pq;
    std::unordered_map<NodeID, Weight> visited;

    pq.push({0.0, orig_node});
    visited[orig_node] = 0.0;

    while (!pq.empty()) {
        auto top = pq.top();
        pq.pop();
        Weight d = top.dist;
        NodeID u = top.node;

        if (d > visited[u]) continue;
        if (d > MAX_ACCESS) break;

        auto jt = original_to_new.find(u);
        if (jt != original_to_new.end()) {
            access_cost = d;
            return jt->second;
        }

        auto targets = orig_graph.outgoing_targets(u);
        auto weights = orig_graph.outgoing_weights(u);
        for (size_t i = 0; i < targets.size(); ++i) {
            Weight nd = d + weights[i];
            if (nd < MAX_ACCESS) {
                auto vit = visited.find(targets[i]);
                if (vit == visited.end() || nd < vit->second) {
                    visited[targets[i]] = nd;
                    pq.push({nd, targets[i]});
                }
            }
        }
    }

    access_cost = INF_WEIGHT;
    return INVALID_NODE;
}

}  // namespace

DegradationReport estimate_degradation(
    const ArrayGraph& orig_graph,
    const ContractionResult& orig_ch,
    const ArrayGraph& simp_graph,
    const std::vector<NodeID>& new_to_original,
    const std::unordered_map<NodeID, NodeID>& original_to_new,
    uint32_t num_samples,
    uint64_t seed) {

    DegradationReport report;

    // Build CH on simplified graph
    auto simp_ch = build_ch(simp_graph);
    CHQuery orig_q(orig_ch);
    CHQuery simp_q(simp_ch);

    // Sample O-D pairs from original graph
    std::mt19937_64 rng(seed);
    NodeID n = orig_graph.node_count();
    std::uniform_int_distribution<NodeID> node_dist(0, n - 1);

    std::vector<double> stretches;
    stretches.reserve(num_samples);

    for (uint32_t i = 0; i < num_samples * 3 && report.od_pairs_sampled < num_samples; ++i) {
        NodeID s = node_dist(rng);
        NodeID t = node_dist(rng);
        if (s == t) continue;

        Weight d_orig = orig_q.distance(s, t);
        if (d_orig >= INF_WEIGHT) continue;  // skip unreachable pairs

        report.pairs_connected_before++;

        // Map to simplified graph
        Weight access_s = 0, access_t = 0;
        NodeID s_new = find_nearest_simplified(s, orig_graph, original_to_new, access_s);
        NodeID t_new = find_nearest_simplified(t, orig_graph, original_to_new, access_t);

        if (s_new == INVALID_NODE || t_new == INVALID_NODE ||
            access_s >= INF_WEIGHT || access_t >= INF_WEIGHT) {
            report.pairs_disconnected++;
            report.od_pairs_sampled++;
            continue;
        }

        Weight d_simp = simp_q.distance(s_new, t_new);
        if (d_simp >= INF_WEIGHT) {
            report.pairs_disconnected++;
            report.od_pairs_sampled++;
            continue;
        }

        report.pairs_connected_after++;
        Weight d_total = access_s + d_simp + access_t;
        double stretch = d_total / d_orig;
        stretch = std::max(1.0, stretch);  // clamp (rounding can produce <1.0)
        stretches.push_back(stretch);
        report.od_pairs_sampled++;
    }

    // Compute stretch statistics
    if (!stretches.empty()) {
        std::sort(stretches.begin(), stretches.end());
        size_t ns = stretches.size();
        report.median_stretch = stretches[ns / 2];
        report.p90_stretch = stretches[std::min(ns - 1, ns * 9 / 10)];
        report.p95_stretch = stretches[std::min(ns - 1, ns * 95 / 100)];
        report.p99_stretch = stretches[std::min(ns - 1, ns * 99 / 100)];
        report.max_stretch = stretches.back();

        double sum = 0;
        for (double s : stretches) sum += s;
        report.mean_stretch = sum / ns;
    }

    // Connectivity ratio
    report.connectivity_ratio = (report.pairs_connected_before > 0) ?
        static_cast<double>(report.pairs_connected_after) / report.pairs_connected_before : 0.0;

    // Bridge preservation check
    auto orig_bridges = find_bridges(orig_graph);
    report.original_bridges = static_cast<uint32_t>(orig_bridges.bridges.size());

    for (const auto& [u, v] : orig_bridges.bridges) {
        auto it_u = original_to_new.find(u);
        auto it_v = original_to_new.find(v);
        if (it_u != original_to_new.end() && it_v != original_to_new.end()) {
            // Both endpoints preserved — check if edge exists
            NodeID nu = it_u->second, nv = it_v->second;
            auto targets = simp_graph.outgoing_targets(nu);
            bool found = false;
            for (NodeID t : targets) {
                if (t == nv) { found = true; break; }
            }
            if (found) report.preserved_bridges++;
        }
    }
    report.all_bridges_preserved = (report.preserved_bridges == report.original_bridges);

    return report;
}

}  // namespace gravel
