#include "gravel/core/lca.h"
#include <algorithm>
#include <stack>

namespace gravel {

LCA::LCA(const std::vector<NodeID>& parent, const std::vector<uint32_t>& depth,
         NodeID root, uint32_t num_nodes)
    : num_nodes_(num_nodes), depth_(depth) {

    // Build child adjacency from parent array.
    std::vector<std::vector<NodeID>> children(num_nodes);
    for (NodeID v = 0; v < num_nodes; ++v) {
        if (parent[v] != INVALID_NODE)
            children[parent[v]].push_back(v);
    }

    // Euler tour via iterative DFS.
    euler_.reserve(2 * num_nodes);
    first_occurrence_.resize(num_nodes, UINT32_MAX);

    struct Frame { NodeID node; uint32_t child_idx; };
    std::stack<Frame> stk;
    stk.push({root, 0});
    first_occurrence_[root] = 0;
    euler_.push_back(root);

    while (!stk.empty()) {
        auto& [node, ci] = stk.top();
        if (ci < children[node].size()) {
            NodeID child = children[node][ci++];
            first_occurrence_[child] = static_cast<uint32_t>(euler_.size());
            euler_.push_back(child);
            stk.push({child, 0});
        } else {
            stk.pop();
            if (!stk.empty()) {
                euler_.push_back(stk.top().node);
            }
        }
    }

    // Build euler_depth_.
    uint32_t tour_len = static_cast<uint32_t>(euler_.size());
    euler_depth_.resize(tour_len);
    for (uint32_t i = 0; i < tour_len; ++i)
        euler_depth_[i] = depth_[euler_[i]];

    // Precompute log2 table.
    log2_.resize(tour_len + 1, 0);
    for (uint32_t i = 2; i <= tour_len; ++i)
        log2_[i] = log2_[i / 2] + 1;

    // Build sparse table for RMQ.
    uint32_t max_log = log2_[tour_len] + 1;
    sparse_.resize(max_log);
    sparse_[0].resize(tour_len);
    for (uint32_t i = 0; i < tour_len; ++i)
        sparse_[0][i] = i;

    for (uint32_t k = 1; k < max_log; ++k) {
        uint32_t half = 1u << (k - 1);
        uint32_t len = tour_len - (1u << k) + 1;
        sparse_[k].resize(len);
        for (uint32_t i = 0; i < len; ++i) {
            uint32_t a = sparse_[k - 1][i];
            uint32_t b = sparse_[k - 1][i + half];
            sparse_[k][i] = (euler_depth_[a] <= euler_depth_[b]) ? a : b;
        }
    }
}

uint32_t LCA::rmq(uint32_t l, uint32_t r) const {
    uint32_t k = log2_[r - l + 1];
    uint32_t a = sparse_[k][l];
    uint32_t b = sparse_[k][r - (1u << k) + 1];
    return (euler_depth_[a] <= euler_depth_[b]) ? a : b;
}

NodeID LCA::query(NodeID u, NodeID v) const {
    uint32_t l = first_occurrence_[u];
    uint32_t r = first_occurrence_[v];
    if (l > r) std::swap(l, r);
    return euler_[rmq(l, r)];
}

}  // namespace gravel
