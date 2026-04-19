#include "gravel/fragility/hershberger_suri.h"
#include "gravel/core/dijkstra.h"
#include "gravel/core/lca.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <limits>
#include <utility>

namespace gravel {

FragilityResult hershberger_suri(const ArrayGraph& graph,
                                  NodeID source, NodeID target) {
    FragilityResult result;
    uint32_t n = graph.node_count();

    // 1. Forward Dijkstra from source → SPT.
    auto fwd = dijkstra(graph, source);
    result.primary_distance = fwd.distances[target];
    if (result.primary_distance >= INF_WEIGHT)
        return result;

    // 2. Backward Dijkstra from target (on reverse graph).
    //    Build reverse graph for backward Dijkstra.
    std::vector<Edge> rev_edges;
    for (NodeID u = 0; u < n; ++u) {
        auto targets = graph.outgoing_targets(u);
        auto weights = graph.outgoing_weights(u);
        for (uint32_t i = 0; i < targets.size(); ++i)
            rev_edges.push_back({targets[i], u, weights[i]});
    }
    ArrayGraph rev_graph(n, std::move(rev_edges));
    auto bwd = dijkstra(rev_graph, target);

    // 3. Extract s-t path from SPT.
    result.primary_path = reconstruct_path(fwd, source, target);
    if (result.primary_path.size() < 2)
        return result;

    size_t path_len = result.primary_path.size();
    size_t num_edges = path_len - 1;

    // Map path nodes to their index on the path (0 = source, ..., num_edges = target).
    std::vector<uint32_t> path_index(n, UINT32_MAX);
    for (uint32_t i = 0; i < path_len; ++i)
        path_index[result.primary_path[i]] = i;

    // 4. Build LCA on the forward SPT.
    //    Compute depth for each node in the SPT.
    std::vector<uint32_t> depth(n, 0);
    // BFS/DFS from source to compute depths.
    {
        std::vector<std::vector<NodeID>> children(n);
        for (NodeID v = 0; v < n; ++v) {
            if (fwd.predecessors[v] != INVALID_NODE)
                children[fwd.predecessors[v]].push_back(v);
        }
        // BFS to compute depth.
        std::vector<NodeID> queue;
        queue.push_back(source);
        depth[source] = 0;
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            NodeID u = queue[qi];
            for (NodeID c : children[u]) {
                depth[c] = depth[u] + 1;
                queue.push_back(c);
            }
        }
    }

    LCA lca(fwd.predecessors, depth, source, n);

    // 5. Identify SPT edges (for distinguishing non-tree edges).
    std::unordered_set<uint64_t> tree_edges;
    for (NodeID v = 0; v < n; ++v) {
        if (fwd.predecessors[v] != INVALID_NODE) {
            uint64_t key = (uint64_t(fwd.predecessors[v]) << 32) | uint64_t(v);
            tree_edges.insert(key);
        }
    }

    // 6. For each non-tree edge, compute detour cost and determine which
    //    path edges it can replace.
    //
    //    A non-tree edge (x, y) provides a detour: source →SPT→ x → y →rev_SPT→ target.
    //    Detour cost = dist_fwd[x] + w(x,y) + dist_bwd[y].
    //
    //    This detour avoids all path edges between the path node where the
    //    s→x branch diverges and the path node where the y→t branch converges.
    //    Specifically: let a = path_index of the last path node on the s→x SPT path,
    //    and b = path_index of the first path node on the y→t backward SPT path.
    //    If a < b, the detour avoids edges a..b-1 (0-indexed path edge indices).
    //
    //    To find a: walk from x up the SPT until hitting a path node. The last path
    //    node before x's subtree diverges from the path.
    //    Simplified: a = path_index of LCA(x, target) along the path. If x is
    //    in the subtree rooted at path[a], the detour avoids edges starting at a.
    //    Actually, a = the deepest path node that is an ancestor of x in the SPT.
    //
    //    For efficiency, precompute "nearest path ancestor" for each node.

    // Precompute nearest_path_ancestor[v] = the path_index of the deepest path
    // node that is an ancestor of v (or on the path from source to v) in the SPT.
    std::vector<uint32_t> nearest_path_ancestor(n, UINT32_MAX);
    {
        // BFS in SPT order (parent before children).
        std::vector<std::vector<NodeID>> children(n);
        for (NodeID v = 0; v < n; ++v) {
            if (fwd.predecessors[v] != INVALID_NODE)
                children[fwd.predecessors[v]].push_back(v);
        }
        std::vector<NodeID> queue;
        queue.push_back(source);
        nearest_path_ancestor[source] = path_index[source];  // 0
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            NodeID u = queue[qi];
            for (NodeID c : children[u]) {
                if (path_index[c] != UINT32_MAX) {
                    nearest_path_ancestor[c] = path_index[c];
                } else {
                    nearest_path_ancestor[c] = nearest_path_ancestor[u];
                }
                queue.push_back(c);
            }
        }
    }

    // Similarly, for the backward SPT from target, precompute
    // nearest_path_ancestor_bwd[v] = the path_index of the shallowest (nearest to target)
    // path node that is an ancestor of v in the backward SPT.
    std::vector<uint32_t> nearest_path_ancestor_bwd(n, UINT32_MAX);
    {
        std::vector<std::vector<NodeID>> bwd_children(n);
        for (NodeID v = 0; v < n; ++v) {
            if (bwd.predecessors[v] != INVALID_NODE)
                bwd_children[bwd.predecessors[v]].push_back(v);
        }
        std::vector<NodeID> queue;
        queue.push_back(target);
        nearest_path_ancestor_bwd[target] = path_index[target];  // num_edges
        for (size_t qi = 0; qi < queue.size(); ++qi) {
            NodeID u = queue[qi];
            for (NodeID c : bwd_children[u]) {
                if (path_index[c] != UINT32_MAX) {
                    nearest_path_ancestor_bwd[c] = path_index[c];
                } else {
                    nearest_path_ancestor_bwd[c] = nearest_path_ancestor_bwd[u];
                }
                queue.push_back(c);
            }
        }
    }

    // Initialize replacement distances to INF.
    result.edge_fragilities.resize(num_edges);
    std::vector<Weight> replacement(num_edges, INF_WEIGHT);

    // 7. Scan all edges. For each non-tree edge (x, y), compute detour
    //    and update the applicable path edges.
    for (NodeID x = 0; x < n; ++x) {
        auto targets = graph.outgoing_targets(x);
        auto weights = graph.outgoing_weights(x);
        for (uint32_t i = 0; i < targets.size(); ++i) {
            NodeID y = targets[i];
            Weight w = weights[i];

            // Skip tree edges (they're part of the SPT, not detours).
            uint64_t edge_key = (uint64_t(x) << 32) | uint64_t(y);
            if (tree_edges.count(edge_key)) continue;

            // Detour cost: source →SPT→ x → y →backward_SPT→ target.
            if (fwd.distances[x] >= INF_WEIGHT || bwd.distances[y] >= INF_WEIGHT)
                continue;
            Weight detour = fwd.distances[x] + w + bwd.distances[y];

            // Determine which path edges this detour avoids.
            // a = nearest path ancestor of x in forward SPT (diverge point).
            // b = nearest path ancestor of y in backward SPT (converge point).
            uint32_t a = nearest_path_ancestor[x];
            uint32_t b = nearest_path_ancestor_bwd[y];

            if (a == UINT32_MAX || b == UINT32_MAX) continue;
            if (a >= b) continue;  // No path edges avoided.

            // This detour can replace path edges [a, b-1] (inclusive).
            for (uint32_t e = a; e < b; ++e) {
                if (detour < replacement[e])
                    replacement[e] = detour;
            }
        }
    }

    // 8. Fill in EdgeFragility results.
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
