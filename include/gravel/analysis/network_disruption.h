#pragma once
/// @file network_disruption.h
/// @brief How a road network fragments as edges fail in a given order.

#include "gravel/core/array_graph.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace gravel {

/// Outputs of :func:`network_disruption`: the connectivity-loss curve and, per edge, the stage at
/// which it becomes stranded.
struct NetworkDisruption {
    /// Per-stage fraction of ordered node pairs left disconnected: `1 - Σ(component_size²) / n²`
    /// after removing every edge with `failure_round <= k`. Length `max_round + 1`
    /// (`severed_fraction[k]` for stage `k`); non-decreasing in `k`.
    std::vector<double> severed_fraction;

    /// Per-edge (CSR order) stage at which an intact edge becomes cut off from the network **hub**
    /// (the most-connected node). NaN if the edge never strands (or is itself removed first). Only
    /// edges originally reachable from the hub can strand, so an already-fragmented graph is not
    /// mislabeled.
    std::vector<double> stranded_round;
};

/// Analyze progressive network disruption in a single reverse-incremental union-find pass.
///
/// @param graph          The network (CSR edge order).
/// @param failure_round  Per-edge removal stage (CSR order): the 1-based stage at which each edge is
///                       removed, or NaN for edges that never fail. Length must equal the edge count.
/// @return               Connectivity curve + per-edge stranded rounds (see @ref NetworkDisruption).
///
/// Complexity `O(edges · α + stages · nodes)`. Used by the Python viz layer
/// (`connectivity_curve`, `disconnection_rounds`) to color and chart fragility animations.
NetworkDisruption network_disruption(const ArrayGraph& graph,
                                     const std::vector<double>& failure_round);

/// Per-edge removal stage from a greedy progressive `removal_sequence` (CSR edge order): the 1-based
/// step at which each edge is removed, or NaN if it survived. Parallel edges sharing an `(u, v)` each
/// consume the next removal of that pair, so lengths stay aligned to the edge count.
std::vector<double> edge_failure_round(
    const ArrayGraph& graph,
    const std::vector<std::pair<NodeID, NodeID>>& removal_sequence);

/// Build a per-edge `failure_round` (CSR order; NaN = never removed) from per-edge failure
/// probabilities. When `exposure_order` is false this is one seeded stochastic realization (each
/// edge fails with its own probability); otherwise every positive-probability edge is ordered by
/// probability. Removed edges are ordered worst-exposure first; `limit < 0` = no cap; `stages <= 0`
/// = one round per removed edge, else the order is bucketed into `stages` rounds.
std::vector<double> failure_sequence_from_probabilities(
    const std::vector<double>& edge_probabilities,
    int limit, int stages, std::uint64_t seed, bool exposure_order);

}  // namespace gravel
