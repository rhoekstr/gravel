#include "gravel/ch/landmarks.h"
#include <queue>
#include <random>
#include <algorithm>
#include <cmath>
#include <utility>
#include <functional>
#include <vector>

namespace gravel {

Weight LandmarkData::lower_bound(NodeID s, NodeID t) const {
    Weight best = 0;
    for (uint32_t l = 0; l < num_landmarks; ++l) {
        // Forward triangle: |d(l,s) - d(l,t)|
        if (dist_from[l][s] < INF_WEIGHT && dist_from[l][t] < INF_WEIGHT) {
            Weight lb = std::abs(dist_from[l][s] - dist_from[l][t]);
            best = std::max(best, lb);
        }
        // Backward triangle: |d(s,l) - d(t,l)|
        if (dist_to[l][s] < INF_WEIGHT && dist_to[l][t] < INF_WEIGHT) {
            Weight lb = std::abs(dist_to[l][s] - dist_to[l][t]);
            best = std::max(best, lb);
        }
    }
    return best;
}

// Dijkstra from a single source, storing distances to all reachable nodes.
static std::vector<Weight> dijkstra_all(const ArrayGraph& graph, NodeID source) {
    NodeID n = graph.node_count();
    std::vector<Weight> dist(n, INF_WEIGHT);
    using PQItem = std::pair<Weight, NodeID>;
    std::priority_queue<PQItem, std::vector<PQItem>, std::greater<>> pq;

    dist[source] = 0.0;
    pq.push({0.0, source});

    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();
        if (d > dist[u]) continue;

        auto targets = graph.outgoing_targets(u);
        auto weights = graph.outgoing_weights(u);
        for (size_t i = 0; i < targets.size(); ++i) {
            Weight nd = d + weights[i];
            if (nd < dist[targets[i]]) {
                dist[targets[i]] = nd;
                pq.push({nd, targets[i]});
            }
        }
    }
    return dist;
}

// Build reverse graph for backward Dijkstra
static ArrayGraph build_reverse(const ArrayGraph& graph) {
    NodeID n = graph.node_count();
    std::vector<Edge> edges;
    edges.reserve(graph.edge_count());
    for (NodeID u = 0; u < n; ++u) {
        auto targets = graph.outgoing_targets(u);
        auto weights = graph.outgoing_weights(u);
        for (size_t i = 0; i < targets.size(); ++i) {
            edges.push_back({targets[i], u, weights[i]});
        }
    }
    return ArrayGraph(n, std::move(edges));
}

LandmarkData precompute_landmarks(const ArrayGraph& graph,
                                   uint32_t num_landmarks,
                                   uint64_t seed) {
    NodeID n = graph.node_count();
    LandmarkData result;
    result.num_landmarks = 0;

    if (n == 0 || num_landmarks == 0) return result;

    num_landmarks = std::min(num_landmarks, n);

    // Build reverse graph for dist_to computation
    auto rev = build_reverse(graph);

    // Farthest-first landmark selection
    std::mt19937_64 rng(seed);
    std::vector<NodeID> landmarks;
    std::vector<Weight> max_dist(n, INF_WEIGHT);

    // First landmark: random node
    NodeID first = std::uniform_int_distribution<NodeID>(0, n - 1)(rng);
    landmarks.push_back(first);

    // Compute distances from first landmark
    auto dists = dijkstra_all(graph, first);
    for (NodeID v = 0; v < n; ++v) {
        max_dist[v] = dists[v];
    }

    result.dist_from.push_back(std::move(dists));
    result.dist_to.push_back(dijkstra_all(rev, first));

    // Select remaining landmarks: farthest from any existing landmark
    // Note: selection is sequential (depends on previous), but the two
    // Dijkstra calls per landmark (forward + backward) are independent.
    for (uint32_t l = 1; l < num_landmarks; ++l) {
        NodeID farthest = 0;
        Weight best_dist = 0;
        for (NodeID v = 0; v < n; ++v) {
            if (max_dist[v] < INF_WEIGHT && max_dist[v] > best_dist) {
                best_dist = max_dist[v];
                farthest = v;
            }
        }

        if (best_dist == 0) break;  // all reachable nodes covered

        landmarks.push_back(farthest);

        // Run forward and backward Dijkstra in parallel
        std::vector<Weight> new_dists, rev_dists;
        #pragma omp parallel sections if(n > 1000)
        {
            #pragma omp section
            { new_dists = dijkstra_all(graph, farthest); }
            #pragma omp section
            { rev_dists = dijkstra_all(rev, farthest); }
        }

        // Update max_dist = min(max_dist, new_dists) for farthest-first
        for (NodeID v = 0; v < n; ++v) {
            max_dist[v] = std::min(max_dist[v], new_dists[v]);
        }

        result.dist_from.push_back(std::move(new_dists));
        result.dist_to.push_back(std::move(rev_dists));
    }

    result.num_landmarks = static_cast<uint32_t>(landmarks.size());
    return result;
}

}  // namespace gravel
