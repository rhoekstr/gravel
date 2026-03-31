#pragma once
#include "gravel/core/array_graph.h"
#include <Eigen/Sparse>

namespace gravel {

// Build the graph Laplacian L = D - A as a sparse matrix.
// Treats the directed graph as undirected (symmetrizes edges).
Eigen::SparseMatrix<double> build_laplacian(const ArrayGraph& graph);

// Build the adjacency matrix A (symmetrized).
Eigen::SparseMatrix<double> build_adjacency(const ArrayGraph& graph);

}  // namespace gravel
