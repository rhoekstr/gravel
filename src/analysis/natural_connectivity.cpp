#include "gravel/analysis/natural_connectivity.h"
#include "gravel/analysis/laplacian.h"
#include <random>
#include <cmath>
#include <Eigen/Dense>
#include <algorithm>

namespace gravel {

double natural_connectivity(const ArrayGraph& graph,
                             uint32_t num_probes,
                             uint32_t lanczos_steps,
                             uint64_t seed) {
    NodeID n = graph.node_count();
    if (n <= 1) return 0.0;

    auto A = build_adjacency(graph);

    // For small graphs, use dense eigendecomposition
    if (n <= 64) {
        Eigen::MatrixXd Ad = Eigen::MatrixXd(A);
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(Ad);
        auto eigenvalues = solver.eigenvalues();
        double sum_exp = 0.0;
        for (int i = 0; i < eigenvalues.size(); ++i) {
            sum_exp += std::exp(eigenvalues(i));
        }
        return std::log(sum_exp / n);
    }

    // Stochastic Lanczos Quadrature (SLQ):
    // λ̄ = ln((1/n) tr(exp(A)))
    // tr(exp(A)) ≈ (n/k) Σ_{j=1}^{k} vⱼᵀ exp(A) vⱼ
    //            ≈ (n/k) Σ_{j=1}^{k} Σ_{i=1}^{m} (e₁ᵀ Qᵢ)² exp(θᵢ)
    // where Qᵢ, θᵢ are eigenvectors/eigenvalues of the tridiagonal Lanczos matrix T.

    std::mt19937_64 rng(seed);
    std::normal_distribution<double> normal(0.0, 1.0);

    uint32_t m = std::min(lanczos_steps, static_cast<uint32_t>(n));
    double trace_exp = 0.0;

    Eigen::VectorXd v(n), w(n), v_prev(n);

    for (uint32_t probe = 0; probe < num_probes; ++probe) {
        // Generate random starting vector and normalize
        for (NodeID i = 0; i < n; ++i) {
            v(i) = normal(rng);
        }
        v.normalize();

        // Lanczos iteration: build tridiagonal T
        std::vector<double> alpha(m), beta(m + 1, 0.0);
        v_prev.setZero();
        double beta_prev = 0.0;

        for (uint32_t j = 0; j < m; ++j) {
            // w = A * v
            w = A * v;

            // Subtract previous: w = w - beta * v_prev
            if (j > 0) {
                w -= beta_prev * v_prev;
            }

            alpha[j] = v.dot(w);
            w -= alpha[j] * v;

            // Reorthogonalize (partial — just against current)
            double correction = v.dot(w);
            w -= correction * v;
            alpha[j] += correction;

            beta_prev = w.norm();
            beta[j + 1] = beta_prev;

            if (beta_prev < 1e-14) {
                // Invariant subspace found, truncate
                m = j + 1;
                break;
            }

            v_prev = v;
            v = w / beta_prev;
        }

        // Build tridiagonal matrix T and compute its eigendecomposition
        Eigen::MatrixXd T = Eigen::MatrixXd::Zero(m, m);
        for (uint32_t j = 0; j < m; ++j) {
            T(j, j) = alpha[j];
            if (j + 1 < m) {
                T(j, j + 1) = beta[j + 1];
                T(j + 1, j) = beta[j + 1];
            }
        }

        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eig(T);
        auto theta = eig.eigenvalues();
        auto Q = eig.eigenvectors();

        // tr(exp(A)) contribution: Σᵢ (Q[0,i])² exp(θᵢ)
        double contribution = 0.0;
        for (uint32_t i = 0; i < m; ++i) {
            double q0i = Q(0, i);
            contribution += q0i * q0i * std::exp(theta(i));
        }

        trace_exp += contribution;
    }

    // Average over probes and scale by n
    trace_exp = (static_cast<double>(n) / num_probes) * trace_exp;

    return std::log(trace_exp / n);
}

}  // namespace gravel
