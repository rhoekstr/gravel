#include "gravel/analysis/kirchhoff.h"
#include "gravel/analysis/laplacian.h"
#include <Eigen/Dense>
#include <Eigen/SparseCholesky>
#include <random>

namespace gravel {

double kirchhoff_index(const ArrayGraph& graph, KirchhoffConfig config) {
    NodeID n = graph.node_count();
    if (n <= 1) return 0.0;

    auto L = build_laplacian(graph);

    // Kirchhoff index R_G = n * trace(L⁺) where L⁺ is the pseudoinverse.
    // Equivalently, R_G = n * Σ_{i=1}^{n-1} (1/λᵢ) where λᵢ are nonzero eigenvalues.
    //
    // For small graphs, use dense eigendecomposition directly.
    if (n <= 64) {
        Eigen::MatrixXd Ld = Eigen::MatrixXd(L);
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(Ld);
        auto eigenvalues = solver.eigenvalues();
        double trace_pinv = 0.0;
        for (int i = 1; i < eigenvalues.size(); ++i) {
            if (eigenvalues(i) > 1e-10) {
                trace_pinv += 1.0 / eigenvalues(i);
            }
        }
        return n * trace_pinv;
    }

    // Hutchinson's estimator for trace(L⁺):
    // trace(L⁺) ≈ (1/k) Σ zᵀ L⁺ z, where z are Rademacher vectors.
    //
    // To compute L⁺z, we solve Lx = z in the orthogonal complement of the nullspace.
    // Since L has nullspace = span(1), we project z to be zero-mean, then solve.
    //
    // Use regularized Laplacian: L_reg = L + (1/n)·11ᵀ ≈ L + εI
    // This makes L invertible. trace(L_reg⁻¹) ≈ trace(L⁺) + 1 (from the added eigenvalue).

    // Regularize: L + (1/n)I to lift the zero eigenvalue
    Eigen::SparseMatrix<double> L_reg = L;
    double eps = 1.0 / n;
    for (NodeID i = 0; i < n; ++i) {
        L_reg.coeffRef(i, i) += eps;
    }

    Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> solver;
    solver.compute(L_reg);
    if (solver.info() != Eigen::Success) {
        return 0.0;  // decomposition failed (disconnected graph)
    }

    std::mt19937_64 rng(config.seed);
    std::uniform_int_distribution<int> coin(0, 1);

    double trace_est = 0.0;
    Eigen::VectorXd z(n), x(n);

    for (uint32_t probe = 0; probe < config.num_probes; ++probe) {
        // Generate Rademacher vector
        for (NodeID i = 0; i < n; ++i) {
            z(i) = coin(rng) ? 1.0 : -1.0;
        }

        // Project out mean (nullspace of L)
        double mean = z.sum() / n;
        z.array() -= mean;

        // Solve L_reg x = z
        x = solver.solve(z);

        trace_est += z.dot(x);
    }

    trace_est /= config.num_probes;

    // R_G = n * trace(L⁺)
    return n * trace_est;
}

}  // namespace gravel
