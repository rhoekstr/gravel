#pragma once
#include "gravel/core/graph_interface.h"
#include <vector>

namespace gravel {

struct DijkstraResult {
    std::vector<Weight> distances;    // distance from source to each node
    std::vector<NodeID> predecessors; // predecessor on shortest path tree
};

// Single-source Dijkstra on the original graph.
// This is the REFERENCE implementation — simple, unoptimized, verifiable by inspection.
// Never used for production queries — only for validation.
DijkstraResult dijkstra(const GravelGraph& graph, NodeID source);

// Single-pair Dijkstra — stops early when target is settled.
Weight dijkstra_pair(const GravelGraph& graph, NodeID source, NodeID target);

// Reconstruct path from source to target using predecessor array.
// Returns empty vector if target is unreachable.
std::vector<NodeID> reconstruct_path(const DijkstraResult& result, NodeID source, NodeID target);

}  // namespace gravel
