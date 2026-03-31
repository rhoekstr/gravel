#include "gravel/analysis/laplacian.h"
#include <vector>

namespace gravel {

Eigen::SparseMatrix<double> build_adjacency(const ArrayGraph& graph) {
    NodeID n = graph.node_count();
    // Symmetrize: for each directed edge (u,v), add both (u,v) and (v,u).
    // Use triplets and let Eigen sum duplicates.
    std::vector<Eigen::Triplet<double>> triplets;
    triplets.reserve(graph.edge_count() * 2);

    for (NodeID u = 0; u < n; ++u) {
        auto targets = graph.outgoing_targets(u);
        auto weights = graph.outgoing_weights(u);
        for (size_t i = 0; i < targets.size(); ++i) {
            NodeID v = targets[i];
            // Use weight 1.0 for unweighted spectral analysis (standard Laplacian)
            triplets.emplace_back(u, v, 1.0);
            triplets.emplace_back(v, u, 1.0);
        }
    }

    Eigen::SparseMatrix<double> A(n, n);
    A.setFromTriplets(triplets.begin(), triplets.end());

    // Duplicates are summed; clamp to 1 for simple graph adjacency
    // (avoid double-counting if both (u,v) and (v,u) exist as directed edges)
    for (int k = 0; k < A.outerSize(); ++k) {
        for (Eigen::SparseMatrix<double>::InnerIterator it(A, k); it; ++it) {
            if (it.value() > 1.0) {
                it.valueRef() = 1.0;
            }
        }
    }

    return A;
}

Eigen::SparseMatrix<double> build_laplacian(const ArrayGraph& graph) {
    Eigen::SparseMatrix<double> A = build_adjacency(graph);
    NodeID n = graph.node_count();

    // L = D - A: diagonal = row sums of A
    std::vector<Eigen::Triplet<double>> diag_triplets;
    diag_triplets.reserve(n);
    for (NodeID i = 0; i < n; ++i) {
        double deg = 0.0;
        for (Eigen::SparseMatrix<double>::InnerIterator it(A, i); it; ++it) {
            deg += it.value();
        }
        diag_triplets.emplace_back(i, i, deg);
    }

    Eigen::SparseMatrix<double> D(n, n);
    D.setFromTriplets(diag_triplets.begin(), diag_triplets.end());

    return D - A;
}

}  // namespace gravel
