#pragma once
#include "gravel/core/array_graph.h"
#include <vector>

namespace gravel {

struct BetweennessConfig {
    uint32_t sample_sources = 0;  // 0 = exact (all sources), >0 = sample this many
    double range_limit = 0.0;     // 0 = unlimited, >0 = Dijkstra cutoff distance
    uint64_t seed = 42;
    // Reproducible mode: accumulate source contributions serially in a fixed order so the
    // result is bit-identical across runs and thread counts. The parallel path sums
    // per-thread partials in nondeterministic completion order (tiny FP differences). Use
    // this when betweenness feeds a published/covariate value. Slower (single-threaded).
    bool deterministic = false;
    // Optional per-edge capacity (CSR edge order, e.g. from gravel-geo estimate_capacity).
    // When non-empty (length == edge_count), the result's `criticality` field is populated
    // as edge betweenness ÷ capacity — "how close to saturation" an edge's load is. Left
    // empty by default; the derivation lives in geo/Python (fragility just consumes it).
    std::vector<double> edge_capacity;
};

struct BetweennessResult {
    std::vector<double> edge_scores;  // indexed by edge position in CSR
    std::vector<double> node_scores;  // indexed by node ID (same length as graph.node_count())
    uint32_t sources_used = 0;        // how many source nodes were processed
    // Capacity-normalized betweenness (edge_scores / capacity), populated only when
    // BetweennessConfig.edge_capacity was supplied. Empty otherwise.
    std::vector<double> criticality;
};

// Compute edge and node betweenness centrality via Brandes' algorithm.
// Exact for county scale, sampling-based for state/national.
// Range-limited variant cuts Dijkstra at range_limit distance.
// OpenMP-parallelized over source nodes.
//
// Node betweenness = fraction of shortest paths passing through a node.
// Useful for picking "central" nodes that are structurally important.
BetweennessResult edge_betweenness(const ArrayGraph& graph,
                                    BetweennessConfig config = {});

// Capacity-weighted edge importance: betweenness × capacity, in CSR edge order.
// Ranks high-throughput corridors above low-capacity streets of equal betweenness
// ("high-capacity cuts rank higher"). `capacity` must be length edge_count; throws
// std::invalid_argument on a size mismatch. Complements BetweennessResult.criticality
// (betweenness ÷ capacity), which measures saturation instead of consequence.
std::vector<double> capacity_weighted_importance(const BetweennessResult& betweenness,
                                                 const std::vector<double>& capacity);

}  // namespace gravel
