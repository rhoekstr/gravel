/// @file boundary_nodes.h
/// @brief Identify graph nodes at region boundaries.
///
/// Returns the set of nodes that must not be contracted during
/// degree-2 simplification to preserve inter-regional connectivity.

#pragma once

#include "gravel/core/array_graph.h"
#include "gravel/geo/region_assignment.h"
#include <unordered_set>

namespace gravel {

/// Returns the set of nodes at region boundaries.
///
/// A node is a boundary node if:
/// - It has at least one neighbor assigned to a different region, OR
/// - It is unassigned (region_index == -1) regardless of neighbor regions.
///
/// O(V + E) — single pass over all edges.
std::unordered_set<NodeID> boundary_nodes(
    const ArrayGraph& graph,
    const RegionAssignment& assignment);

}  // namespace gravel
