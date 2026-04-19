#pragma once
#include "gravel/core/array_graph.h"
#include <vector>

namespace gravel {

struct BetweennessConfig {
    uint32_t sample_sources = 0;  // 0 = exact (all sources), >0 = sample this many
    double range_limit = 0.0;     // 0 = unlimited, >0 = Dijkstra cutoff distance
    uint64_t seed = 42;
};

struct BetweennessResult {
    std::vector<double> edge_scores;  // indexed by edge position in CSR
    std::vector<double> node_scores;  // indexed by node ID (same length as graph.node_count())
    uint32_t sources_used = 0;        // how many source nodes were processed
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

}  // namespace gravel
