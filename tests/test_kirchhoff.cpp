#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/analysis/kirchhoff.h"

using namespace gravel;

// K_n: Kirchhoff index = n-1 (each nonzero eigenvalue of L is n, so trace(L⁺) = (n-1)/n)
// R_G = n * (n-1)/n = n-1
static ArrayGraph make_complete(uint32_t n) {
    std::vector<Edge> edges;
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t j = i + 1; j < n; ++j) {
            edges.push_back({i, j, 1.0});
            edges.push_back({j, i, 1.0});
        }
    return ArrayGraph(n, std::move(edges));
}

// Cycle: eigenvalues λ_k = 2(1-cos(2πk/n)), k=0..n-1
// trace(L⁺) = Σ_{k=1}^{n-1} 1/λ_k
static ArrayGraph make_cycle(uint32_t n) {
    std::vector<Edge> edges;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t j = (i + 1) % n;
        edges.push_back({i, j, 1.0});
        edges.push_back({j, i, 1.0});
    }
    return ArrayGraph(n, std::move(edges));
}

TEST_CASE("Kirchhoff index of K_n equals n-1", "[kirchhoff]") {
    auto k5 = make_complete(5);
    double ki = kirchhoff_index(k5);
    // Should be close to 4.0
    REQUIRE_THAT(ki, Catch::Matchers::WithinRel(4.0, 0.10));  // within 10%
}

TEST_CASE("Kirchhoff index of K_10", "[kirchhoff]") {
    auto k10 = make_complete(10);
    double ki = kirchhoff_index(k10);
    REQUIRE_THAT(ki, Catch::Matchers::WithinRel(9.0, 0.10));
}

TEST_CASE("Kirchhoff stochastic within 10% of exact on cycle", "[kirchhoff]") {
    uint32_t n = 8;
    auto cycle = make_cycle(n);

    // Exact: R_G = n * Σ_{k=1}^{n-1} 1/(2(1-cos(2πk/n)))
    double exact_trace = 0.0;
    for (uint32_t k = 1; k < n; ++k) {
        double lambda = 2.0 * (1.0 - std::cos(2.0 * M_PI * k / n));
        exact_trace += 1.0 / lambda;
    }
    double exact_ki = n * exact_trace;

    KirchhoffConfig config;
    config.num_probes = 50;
    config.seed = 42;
    double stochastic_ki = kirchhoff_index(cycle, config);

    // Within 10% of exact
    REQUIRE(stochastic_ki > exact_ki * 0.90);
    REQUIRE(stochastic_ki < exact_ki * 1.10);
}
