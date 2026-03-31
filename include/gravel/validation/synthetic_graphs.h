#pragma once
#include "gravel/core/array_graph.h"
#include <cstdint>
#include <memory>

namespace gravel {

// Grid graph: rows x cols, bidirectional edges.
// Horizontal and vertical weights = 1.0.
// Predictable structure for exhaustive testing.
std::unique_ptr<ArrayGraph> make_grid_graph(uint32_t rows, uint32_t cols);

// Random connected graph: n nodes, m edges, weights uniform in [min_w, max_w].
// Seeded PRNG for reproducibility. Guaranteed connected via initial spanning tree.
std::unique_ptr<ArrayGraph> make_random_graph(uint32_t n, uint32_t m,
                                              Weight min_w = 1.0,
                                              Weight max_w = 100.0,
                                              uint64_t seed = 42);

// Tree with extra edges: generates a random spanning tree, then adds extra_edges
// random shortcuts. Tests articulation-point sensitivity (fragile topologies).
std::unique_ptr<ArrayGraph> make_tree_with_bridges(uint32_t n,
                                                   uint32_t extra_edges,
                                                   uint64_t seed = 42);

}  // namespace gravel
