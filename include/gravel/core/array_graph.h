#pragma once
#include "gravel/core/graph_interface.h"
#include <vector>
#include <optional>
#include <span>

namespace gravel {

// Compressed Sparse Row graph — the canonical in-memory representation.
// Uses Structure-of-Arrays layout: separate target and weight arrays for cache efficiency.
// All graph sources (CSV, OSM, numpy) convert to this format.
class ArrayGraph final : public GravelGraph {
public:
    // Construct from edge list. Nodes are numbered [0, num_nodes).
    ArrayGraph(NodeID num_nodes, std::vector<Edge> edges);

    // Construct from pre-built SoA CSR arrays (for deserialization / pybind11).
    ArrayGraph(std::vector<uint32_t> offsets,
               std::vector<NodeID> targets,
               std::vector<Weight> weights,
               std::vector<Coord> coords = {});

    NodeID node_count() const override;
    EdgeID edge_count() const override;
    std::span<const NodeID> outgoing_targets(NodeID node) const override;
    std::span<const Weight> outgoing_weights(NodeID node) const override;
    uint32_t degree(NodeID node) const override;
    std::optional<Coord> node_coordinate(NodeID node) const override;

    // Direct access for CH builder and serialization
    const std::vector<uint32_t>& raw_offsets() const { return offsets_; }
    const std::vector<NodeID>& raw_targets() const { return targets_; }
    const std::vector<Weight>& raw_weights() const { return weights_; }
    const std::vector<Coord>& raw_coords() const { return coords_; }

private:
    std::vector<uint32_t> offsets_;   // size = node_count + 1
    std::vector<NodeID> targets_;     // size = edge_count
    std::vector<Weight> weights_;     // size = edge_count
    std::vector<Coord> coords_;       // size = node_count or empty
};

}  // namespace gravel
