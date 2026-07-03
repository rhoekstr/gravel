#pragma once
/// @file graph_build.h
/// @brief Build a graph from per-edge endpoint coordinates, snapping shared nodes.

#include "gravel/core/array_graph.h"

#include <memory>
#include <vector>

namespace gravel {

/// Build a graph from per-edge endpoint coordinates. Endpoints that round to the same
/// (lat, lon) at `precision` decimal places are snapped into one node (ids assigned in first-seen
/// order; coordinates preserved). When `directed` is false, each input edge also gets a reverse
/// edge. `src_coords`, `tgt_coords`, and `weights` are parallel, one entry per input edge.
std::shared_ptr<ArrayGraph> graph_from_endpoints(
    const std::vector<Coord>& src_coords,
    const std::vector<Coord>& tgt_coords,
    const std::vector<double>& weights,
    int precision,
    bool directed);

}  // namespace gravel
