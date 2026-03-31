#pragma once
#include "gravel/core/array_graph.h"

namespace gravel {

struct KirchhoffConfig {
    uint32_t num_probes = 30;  // Hutchinson's estimator probe count
    uint64_t seed = 42;
};

// Compute the Kirchhoff index (effective graph resistance) R_G = n * trace(L^+).
// Uses stochastic trace estimation (Hutchinson's method) for scalability.
// For small graphs (<1000 nodes), uses direct computation.
double kirchhoff_index(const ArrayGraph& graph, KirchhoffConfig config = {});

}  // namespace gravel
