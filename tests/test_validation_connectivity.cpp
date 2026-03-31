#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/analysis/algebraic_connectivity.h"
#include "gravel/analysis/laplacian.h"
#include "gravel/validation/synthetic_graphs.h"
#include <Eigen/Dense>
#include <random>
#include <cmath>

using namespace gravel;

// Brute-force algebraic connectivity via dense eigendecomposition
static double brute_force_ac(const ArrayGraph& graph) {
    auto L = build_laplacian(graph);
    Eigen::MatrixXd Ld = Eigen::MatrixXd(L);
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(Ld);
    auto eigenvalues = solver.eigenvalues();
    return std::max(0.0, eigenvalues(1));
}

TEST_CASE("Algebraic connectivity within 5% of brute-force on 10 random graphs", "[validation]") {
    std::mt19937_64 rng(42);
    uint32_t passed = 0;

    for (int trial = 0; trial < 10; ++trial) {
        // Random connected graph: 20-40 nodes, ~3x edges
        uint32_t n = 20 + (rng() % 21);
        uint32_t m = n * 3;
        uint64_t seed = rng();

        auto graph = make_random_graph(n, m, 1.0, 10.0, seed);

        double bf = brute_force_ac(*graph);
        double computed = algebraic_connectivity(*graph);

        // Within 5% or absolute 0.01 (for very small values)
        bool ok = (std::abs(computed - bf) < 0.05 * bf + 0.01);
        if (ok) passed++;
    }

    REQUIRE(passed >= 9);  // at least 9 out of 10 must pass
}
