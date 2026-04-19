/// @file geography_skeleton.h
/// @brief Thin adapter: build a ReducedGraph from a RegionAssignment.
///
/// The core graph-reduction algorithm lives in `gravel/simplify/reduced_graph.h`
/// and operates on generic int32 region indices. This adapter unwraps a
/// `RegionAssignment` (geography-specific type from gravel-geo) into the
/// generic inputs and forwards to `build_reduced_graph()`.
///
/// For custom region types (ISO country codes, NUTS codes, etc.), skip this
/// adapter and call `build_reduced_graph()` directly.

#pragma once

#include "gravel/simplify/reduced_graph.h"
#include "gravel/geo/region_assignment.h"
#include "gravel/geo/border_edges.h"

namespace gravel {

/// Build a reduced graph from a RegionAssignment.
///
/// Thin wrapper around `build_reduced_graph()` — extracts the per-node region
/// index and per-region metadata from the assignment.
///
/// @param border  Accepted for API compatibility; currently unused (the reduction
///                recomputes inter-region edges from the full graph + assignment).
ReducedGraph build_reduced_geography_graph(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const RegionAssignment& assignment,
    const BorderEdgeResult& border,
    const ReducedGraphConfig& config = {});

}  // namespace gravel
