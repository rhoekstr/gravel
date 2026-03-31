#pragma once
#include "gravel/core/array_graph.h"

namespace gravel {

// Compute the algebraic connectivity (Fiedler value, second smallest eigenvalue of Laplacian).
// Returns 0.0 for disconnected graphs.
// County/state scale only — national scale is infeasible.
double algebraic_connectivity(const ArrayGraph& graph);

}  // namespace gravel
