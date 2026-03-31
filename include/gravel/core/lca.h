#pragma once
#include "gravel/core/types.h"
#include <vector>

namespace gravel {

// Lowest Common Ancestor on a rooted tree via Euler tour + sparse table RMQ.
// O(n log n) preprocessing, O(1) per query.
class LCA {
public:
    // Build from a parent array. parent[root] must be INVALID_NODE.
    // depth[v] is the depth of node v in the tree.
    LCA(const std::vector<NodeID>& parent, const std::vector<uint32_t>& depth,
        NodeID root, uint32_t num_nodes);

    // Return the LCA of u and v.
    NodeID query(NodeID u, NodeID v) const;

    // Return depth of node v.
    uint32_t depth(NodeID v) const { return depth_[v]; }

private:
    uint32_t num_nodes_;
    std::vector<uint32_t> depth_;
    std::vector<uint32_t> first_occurrence_;  // first index of node in Euler tour
    std::vector<NodeID> euler_;               // Euler tour (2n-1 entries)
    std::vector<uint32_t> euler_depth_;       // depth at each Euler tour position

    // Sparse table for RMQ on euler_depth_
    std::vector<std::vector<uint32_t>> sparse_;  // sparse_[k][i] = index of min in euler_depth_[i..i+2^k)
    std::vector<uint32_t> log2_;                 // precomputed floor(log2(x))

    uint32_t rmq(uint32_t l, uint32_t r) const;
};

}  // namespace gravel
