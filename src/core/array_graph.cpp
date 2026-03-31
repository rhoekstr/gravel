#include "gravel/core/array_graph.h"
#include <algorithm>
#include <cassert>

namespace gravel {

ArrayGraph::ArrayGraph(NodeID num_nodes, std::vector<Edge> edges)
    : coords_() {
    // Sort edges by source for CSR construction
    std::sort(edges.begin(), edges.end(),
              [](const Edge& a, const Edge& b) { return a.source < b.source; });

    offsets_.resize(num_nodes + 1, 0);
    targets_.reserve(edges.size());
    weights_.reserve(edges.size());

    for (const auto& e : edges) {
        assert(e.source < num_nodes);
        assert(e.target < num_nodes);
        offsets_[e.source + 1]++;
        targets_.push_back(e.target);
        weights_.push_back(e.weight);
    }

    // Prefix sum to build offset array
    for (NodeID i = 1; i <= num_nodes; ++i) {
        offsets_[i] += offsets_[i - 1];
    }
}

ArrayGraph::ArrayGraph(std::vector<uint32_t> offsets,
                       std::vector<NodeID> targets,
                       std::vector<Weight> weights,
                       std::vector<Coord> coords)
    : offsets_(std::move(offsets)),
      targets_(std::move(targets)),
      weights_(std::move(weights)),
      coords_(std::move(coords)) {
    assert(offsets_.size() >= 1);
    assert(targets_.size() == weights_.size());
    assert(offsets_.back() == static_cast<uint32_t>(targets_.size()));
}

NodeID ArrayGraph::node_count() const {
    return static_cast<NodeID>(offsets_.size() - 1);
}

EdgeID ArrayGraph::edge_count() const {
    return static_cast<EdgeID>(targets_.size());
}

std::span<const NodeID> ArrayGraph::outgoing_targets(NodeID node) const {
    assert(node < node_count());
    return {targets_.data() + offsets_[node],
            targets_.data() + offsets_[node + 1]};
}

std::span<const Weight> ArrayGraph::outgoing_weights(NodeID node) const {
    assert(node < node_count());
    return {weights_.data() + offsets_[node],
            weights_.data() + offsets_[node + 1]};
}

uint32_t ArrayGraph::degree(NodeID node) const {
    assert(node < node_count());
    return offsets_[node + 1] - offsets_[node];
}

std::optional<Coord> ArrayGraph::node_coordinate(NodeID node) const {
    if (coords_.empty() || node >= coords_.size()) return std::nullopt;
    return coords_[node];
}

}  // namespace gravel
