#pragma once
#include "gravel/core/types.h"
#include <optional>
#include <span>

namespace gravel {

class GravelGraph {
public:
    virtual ~GravelGraph() = default;

    virtual NodeID node_count() const = 0;
    virtual EdgeID edge_count() const = 0;

    // SoA edge accessors — separate target and weight arrays for cache efficiency
    virtual std::span<const NodeID> outgoing_targets(NodeID node) const = 0;
    virtual std::span<const Weight> outgoing_weights(NodeID node) const = 0;
    virtual uint32_t degree(NodeID node) const = 0;

    // Optional capabilities
    virtual std::optional<Coord> node_coordinate(NodeID /*node*/) const {
        return std::nullopt;
    }
    virtual std::optional<std::string> edge_label(NodeID /*node*/, EdgeID /*local_idx*/) const {
        return std::nullopt;
    }
    virtual std::optional<Weight> edge_secondary_weight(NodeID /*node*/, EdgeID /*local_idx*/) const {
        return std::nullopt;
    }
};

}  // namespace gravel
