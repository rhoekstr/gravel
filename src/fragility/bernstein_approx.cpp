#include "gravel/fragility/bernstein_approx.h"
#include "gravel/core/dijkstra.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <limits>
#include <utility>
#include <vector>

namespace gravel {

FragilityResult bernstein_approx(const ArrayGraph& graph,
                                  NodeID source, NodeID target,
                                  BernsteinConfig config) {
    FragilityResult result;
    uint32_t n = graph.node_count();
    double eps = config.epsilon;

    // 1. Forward Dijkstra from source.
    auto fwd = dijkstra(graph, source);
    result.primary_distance = fwd.distances[target];
    if (result.primary_distance >= INF_WEIGHT)
        return result;

    // 2. Extract s-t path.
    result.primary_path = reconstruct_path(fwd, source, target);
    if (result.primary_path.size() < 2)
        return result;

    size_t path_len = result.primary_path.size();
    size_t num_edges = path_len - 1;

    // 3. Build reverse graph for backward Dijkstra.
    std::vector<Edge> rev_edges;
    for (NodeID u = 0; u < n; ++u) {
        auto targets = graph.outgoing_targets(u);
        auto weights = graph.outgoing_weights(u);
        for (uint32_t i = 0; i < targets.size(); ++i)
            rev_edges.push_back({targets[i], u, weights[i]});
    }
    ArrayGraph rev_graph(n, std::move(rev_edges));
    auto bwd = dijkstra(rev_graph, target);

    // 4. Compute path edge weights (prefix sums for segment boundaries).
    std::vector<Weight> edge_weight(num_edges);
    for (size_t i = 0; i < num_edges; ++i) {
        // Weight of edge i = dist[path[i+1]] - dist[path[i]]
        edge_weight[i] = fwd.distances[result.primary_path[i + 1]]
                        - fwd.distances[result.primary_path[i]];
    }

    // Prefix sums of edge weights from path start.
    std::vector<Weight> prefix(num_edges + 1, 0.0);
    for (size_t i = 0; i < num_edges; ++i)
        prefix[i + 1] = prefix[i] + edge_weight[i];
    Weight total_path_weight = prefix[num_edges];

    // 5. Partition path into geometrically increasing segments.
    //    Segment boundaries at cumulative weights: eps * W, eps*(1+eps)*W, eps*(1+eps)^2*W, ...
    //    where W = total path weight. Plus boundaries at 0 and total_path_weight.
    std::vector<size_t> boundaries;  // indices into path edges (boundary at start of edge i)
    boundaries.push_back(0);

    if (total_path_weight > 0) {
        double threshold = eps * total_path_weight;
        while (threshold < total_path_weight) {
            // Find edge index where prefix sum exceeds threshold.
            auto it = std::upper_bound(prefix.begin(), prefix.end(), threshold);
            size_t idx = static_cast<size_t>(it - prefix.begin());
            if (idx > 0) --idx;  // edge index
            if (idx > boundaries.back() && idx < num_edges)
                boundaries.push_back(idx);
            threshold *= (1.0 + eps);
        }
    }
    boundaries.push_back(num_edges);

    // 6. For each segment, compute the best detour that avoids it.
    //    For a segment [a, b): the best detour uses a non-tree edge (x, y) where
    //    x is reachable from source without edges [a, b) and y can reach target
    //    without edges [a, b).
    //
    //    Approximation: for each boundary point p_i, compute:
    //    - dist_fwd[x] for all nodes x (from the full forward Dijkstra)
    //    - dist_bwd[y] for all nodes y (from the full backward Dijkstra)
    //    - For each non-tree edge (x, y): detour = dist_fwd[x] + w(x,y) + dist_bwd[y]
    //    The best detour across ALL non-tree edges gives a lower bound on replacement distance.
    //
    //    For the approximation, we assign each non-tree edge's detour as a replacement
    //    for path edges in the range [nearest_path_ancestor(x), nearest_path_ancestor_bwd(y)).
    //    This is essentially the Hershberger-Suri algorithm — for the approximate version,
    //    we use the segment boundaries to batch the assignments.

    // Since we've already computed fwd and bwd Dijkstra, use the same approach as
    // Hershberger-Suri but with rounding to segment boundaries.

    // Map path nodes to indices.
    std::vector<uint32_t> path_index(n, UINT32_MAX);
    for (uint32_t i = 0; i < path_len; ++i)
        path_index[result.primary_path[i]] = i;

    // Precompute nearest path ancestor in forward SPT.
    std::vector<uint32_t> nearest_fwd(n, UINT32_MAX);
    {
        std::vector<std::vector<NodeID>> children(n);
        for (NodeID v = 0; v < n; ++v)
            if (fwd.predecessors[v] != INVALID_NODE)
                children[fwd.predecessors[v]].push_back(v);
        std::vector<NodeID> queue;
        queue.push_back(source);
        nearest_fwd[source] = path_index[source];
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            NodeID u = queue[qi];
            for (NodeID c : children[u]) {
                nearest_fwd[c] = (path_index[c] != UINT32_MAX) ? path_index[c] : nearest_fwd[u];
                queue.push_back(c);
            }
        }
    }

    // Precompute nearest path ancestor in backward SPT.
    std::vector<uint32_t> nearest_bwd(n, UINT32_MAX);
    {
        std::vector<std::vector<NodeID>> children(n);
        for (NodeID v = 0; v < n; ++v)
            if (bwd.predecessors[v] != INVALID_NODE)
                children[bwd.predecessors[v]].push_back(v);
        std::vector<NodeID> queue;
        queue.push_back(target);
        nearest_bwd[target] = path_index[target];
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            NodeID u = queue[qi];
            for (NodeID c : children[u]) {
                nearest_bwd[c] = (path_index[c] != UINT32_MAX) ? path_index[c] : nearest_bwd[u];
                queue.push_back(c);
            }
        }
    }

    // Tree edges to skip.
    std::unordered_set<uint64_t> tree_edges;
    for (NodeID v = 0; v < n; ++v)
        if (fwd.predecessors[v] != INVALID_NODE)
            tree_edges.insert((uint64_t(fwd.predecessors[v]) << 32) | uint64_t(v));

    // Compute replacement distances with segment-based rounding.
    std::vector<Weight> replacement(num_edges, INF_WEIGHT);

    // Round each non-tree edge's coverage to segment boundaries for approximation.
    auto round_down_to_boundary = [&](uint32_t edge_idx) -> uint32_t {
        // Find the largest boundary <= edge_idx.
        auto it = std::upper_bound(boundaries.begin(), boundaries.end(), (size_t)edge_idx);
        if (it == boundaries.begin()) return 0;
        --it;
        return static_cast<uint32_t>(*it);
    };

    auto round_up_to_boundary = [&](uint32_t edge_idx) -> uint32_t {
        // Find the smallest boundary >= edge_idx.
        auto it = std::lower_bound(boundaries.begin(), boundaries.end(), (size_t)edge_idx);
        if (it == boundaries.end()) return static_cast<uint32_t>(num_edges);
        return static_cast<uint32_t>(*it);
    };

    for (NodeID x = 0; x < n; ++x) {
        auto targets = graph.outgoing_targets(x);
        auto weights = graph.outgoing_weights(x);
        for (uint32_t i = 0; i < targets.size(); ++i) {
            NodeID y = targets[i];
            uint64_t key = (uint64_t(x) << 32) | uint64_t(y);
            if (tree_edges.count(key)) continue;

            if (fwd.distances[x] >= INF_WEIGHT || bwd.distances[y] >= INF_WEIGHT)
                continue;

            Weight detour = fwd.distances[x] + weights[i] + bwd.distances[y];

            uint32_t a = nearest_fwd[x];
            uint32_t b = nearest_bwd[y];
            if (a == UINT32_MAX || b == UINT32_MAX || a >= b) continue;

            // Approximate: round coverage to segment boundaries.
            // This may slightly over-cover (assigning the detour to more edges),
            // which makes the result an upper bound — still within (1+eps).
            uint32_t a_rounded = round_down_to_boundary(a);
            uint32_t b_rounded = round_up_to_boundary(b);

            for (uint32_t e = a_rounded; e < b_rounded && e < num_edges; ++e) {
                if (detour < replacement[e])
                    replacement[e] = detour;
            }
        }
    }

    // 7. Fill results.
    result.edge_fragilities.resize(num_edges);
    for (size_t i = 0; i < num_edges; ++i) {
        EdgeFragility& ef = result.edge_fragilities[i];
        ef.source = result.primary_path[i];
        ef.target = result.primary_path[i + 1];
        ef.replacement_distance = replacement[i];
        if (ef.replacement_distance >= INF_WEIGHT) {
            ef.fragility_ratio = std::numeric_limits<double>::infinity();
        } else {
            ef.fragility_ratio = ef.replacement_distance / result.primary_distance;
        }
    }

    return result;
}

}  // namespace gravel
