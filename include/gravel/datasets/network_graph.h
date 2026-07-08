#pragma once
/// @file network_graph.h
/// @brief Result type for the 2.7 network-substrate loaders.

#include "gravel/core/array_graph.h"

#include <memory>
#include <vector>

namespace gravel {

/// A loaded infrastructure-network substrate: the graph plus an optional per-edge
/// capacity attribute (CSR-aligned; empty when the source carries none).
///
/// Produced by the network parsers in `gravel-datasets` (GridSFM, OPFData, CAIDA
/// ITDK, OpenFlights, GTFS) and consumed by the same fragility / cascade analyses
/// as any other `ArrayGraph`. Capacity, where present, feeds
/// `stochastic_fragility` / `cascade_fragility` as the existing per-edge input
/// array — the network parsers are just new *producers* of a graph (+ capacity),
/// not a new analysis paradigm. Node coordinates are populated when the source
/// carries them (all but the synthetic OPFData test cases).
struct NetworkGraph {
    std::unique_ptr<ArrayGraph> graph;
    std::vector<double> capacity;  ///< per-edge capacity in CSR order; empty if unavailable.
};

}  // namespace gravel
