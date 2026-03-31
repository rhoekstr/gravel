#pragma once
#include "gravel/core/array_graph.h"
#include <memory>
#include <vector>

namespace gravel {

// Compute a Hilbert curve-based permutation for node ordering.
// Requires a graph with coordinates. Returns perm where perm[new_id] = old_id.
// Returns empty vector if graph has no coordinates.
std::vector<NodeID> hilbert_permutation(const ArrayGraph& graph);

// Reorder a graph according to a permutation (perm[new_id] = old_id).
// Returns a new graph with remapped node IDs.
std::unique_ptr<ArrayGraph> reorder_graph(const ArrayGraph& graph,
                                           const std::vector<NodeID>& perm);

}  // namespace gravel
