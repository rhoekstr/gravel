#pragma once
#include "gravel/core/array_graph.h"

namespace gravel {

// Compute natural connectivity via stochastic Lanczos quadrature.
// λ̄ = ln((1/n) * Σ exp(λ_i))
// Approximated without computing individual eigenvalues.
double natural_connectivity(const ArrayGraph& graph,
                             uint32_t num_probes = 20,
                             uint32_t lanczos_steps = 50,
                             uint64_t seed = 42);

}  // namespace gravel
