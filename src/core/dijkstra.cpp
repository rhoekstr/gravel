#include "gravel/core/dijkstra.h"
#include "gravel/core/four_heap.h"
#include <algorithm>

namespace gravel {

DijkstraResult dijkstra(const GravelGraph& graph, NodeID source) {
    NodeID n = graph.node_count();
    DijkstraResult result;
    result.distances.assign(n, INF_WEIGHT);
    result.predecessors.assign(n, INVALID_NODE);

    FourHeap pq;
    pq.reserve(n);
    result.distances[source] = 0.0;
    pq.push(source, 0.0);

    while (!pq.empty()) {
        auto [u, dist_u] = pq.pop();

        if (dist_u > result.distances[u]) continue;

        auto targets = graph.outgoing_targets(u);
        auto weights = graph.outgoing_weights(u);
        for (uint32_t i = 0; i < targets.size(); ++i) {
            Weight new_dist = dist_u + weights[i];
            if (new_dist < result.distances[targets[i]]) {
                result.distances[targets[i]] = new_dist;
                result.predecessors[targets[i]] = u;
                pq.push(targets[i], new_dist);
            }
        }
    }

    return result;
}

Weight dijkstra_pair(const GravelGraph& graph, NodeID source, NodeID target) {
    NodeID n = graph.node_count();
    std::vector<Weight> dist(n, INF_WEIGHT);

    FourHeap pq;
    pq.reserve(n);
    dist[source] = 0.0;
    pq.push(source, 0.0);

    while (!pq.empty()) {
        auto [u, dist_u] = pq.pop();

        if (u == target) return dist_u;
        if (dist_u > dist[u]) continue;

        auto targets = graph.outgoing_targets(u);
        auto weights = graph.outgoing_weights(u);
        for (uint32_t i = 0; i < targets.size(); ++i) {
            Weight new_dist = dist_u + weights[i];
            if (new_dist < dist[targets[i]]) {
                dist[targets[i]] = new_dist;
                pq.push(targets[i], new_dist);
            }
        }
    }

    return INF_WEIGHT;
}

std::vector<NodeID> reconstruct_path(const DijkstraResult& result, NodeID source, NodeID target) {
    if (result.distances[target] == INF_WEIGHT) return {};

    std::vector<NodeID> path;
    for (NodeID v = target; v != INVALID_NODE; v = result.predecessors[v]) {
        path.push_back(v);
    }
    std::reverse(path.begin(), path.end());
    return path;
}

}  // namespace gravel
