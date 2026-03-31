#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/core/types.h"
#include <vector>
#include <unordered_map>

namespace gravel {

struct Polygon {
    std::vector<Coord> vertices;  // closed ring (first == last, or auto-closed)
};

struct SubgraphResult {
    std::shared_ptr<ArrayGraph> graph;
    std::vector<NodeID> new_to_original;               // new_id → original_id
    std::unordered_map<NodeID, NodeID> original_to_new; // original_id → new_id
};

// Extract the subgraph induced by nodes within the polygon boundary.
// Requires the source graph to have node coordinates.
SubgraphResult extract_subgraph(const ArrayGraph& graph, const Polygon& boundary);

}  // namespace gravel
