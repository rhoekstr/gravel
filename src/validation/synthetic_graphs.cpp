#include "gravel/validation/synthetic_graphs.h"
#include <algorithm>
#include <cassert>
#include <numeric>
#include <random>
#include <memory>
#include <utility>
#include <vector>

namespace gravel {

std::unique_ptr<ArrayGraph> make_grid_graph(uint32_t rows, uint32_t cols) {
    assert(rows > 0 && cols > 0);
    NodeID n = rows * cols;
    std::vector<Edge> edges;
    edges.reserve(n * 4);  // upper bound

    auto node_id = [cols](uint32_t r, uint32_t c) -> NodeID {
        return r * cols + c;
    };

    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            NodeID u = node_id(r, c);
            // Right
            if (c + 1 < cols) {
                NodeID v = node_id(r, c + 1);
                edges.push_back({u, v, 1.0});
                edges.push_back({v, u, 1.0});
            }
            // Down
            if (r + 1 < rows) {
                NodeID v = node_id(r + 1, c);
                edges.push_back({u, v, 1.0});
                edges.push_back({v, u, 1.0});
            }
        }
    }

    return std::make_unique<ArrayGraph>(n, std::move(edges));
}

std::unique_ptr<ArrayGraph> make_random_graph(uint32_t n, uint32_t m,
                                              Weight min_w, Weight max_w,
                                              uint64_t seed) {
    assert(n >= 2);
    assert(m >= n - 1);  // at least a spanning tree
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<Weight> weight_dist(min_w, max_w);

    std::vector<Edge> edges;
    edges.reserve(m * 2);  // bidirectional

    // Build a random spanning tree first (guarantees connectivity)
    std::vector<NodeID> perm(n);
    std::iota(perm.begin(), perm.end(), 0);
    std::shuffle(perm.begin(), perm.end(), rng);

    for (uint32_t i = 1; i < n; ++i) {
        std::uniform_int_distribution<uint32_t> parent_dist(0, i - 1);
        NodeID u = perm[i];
        NodeID v = perm[parent_dist(rng)];
        Weight w = weight_dist(rng);
        edges.push_back({u, v, w});
        edges.push_back({v, u, w});
    }

    // Add remaining random edges
    std::uniform_int_distribution<NodeID> node_dist(0, n - 1);
    uint32_t tree_edges = n - 1;
    for (uint32_t i = tree_edges; i < m; ++i) {
        NodeID u = node_dist(rng);
        NodeID v = node_dist(rng);
        while (v == u) v = node_dist(rng);
        Weight w = weight_dist(rng);
        edges.push_back({u, v, w});
        edges.push_back({v, u, w});
    }

    return std::make_unique<ArrayGraph>(n, std::move(edges));
}

std::unique_ptr<ArrayGraph> make_tree_with_bridges(uint32_t n,
                                                   uint32_t extra_edges,
                                                   uint64_t seed) {
    assert(n >= 2);
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<Weight> weight_dist(1.0, 50.0);

    std::vector<Edge> edges;
    edges.reserve((n - 1 + extra_edges) * 2);

    // Build a random tree
    std::vector<NodeID> perm(n);
    std::iota(perm.begin(), perm.end(), 0);
    std::shuffle(perm.begin(), perm.end(), rng);

    for (uint32_t i = 1; i < n; ++i) {
        std::uniform_int_distribution<uint32_t> parent_dist(0, i - 1);
        NodeID u = perm[i];
        NodeID v = perm[parent_dist(rng)];
        Weight w = weight_dist(rng);
        edges.push_back({u, v, w});
        edges.push_back({v, u, w});
    }

    // Add extra "bridge-bypassing" edges
    std::uniform_int_distribution<NodeID> node_dist(0, n - 1);
    for (uint32_t i = 0; i < extra_edges; ++i) {
        NodeID u = node_dist(rng);
        NodeID v = node_dist(rng);
        while (v == u) v = node_dist(rng);
        Weight w = weight_dist(rng);
        edges.push_back({u, v, w});
        edges.push_back({v, u, w});
    }

    return std::make_unique<ArrayGraph>(n, std::move(edges));
}

}  // namespace gravel
