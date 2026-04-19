#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/analysis/algebraic_connectivity.h"
#include "gravel/core/constants.h"
#include <cmath>
#include <utility>
#include <vector>

using namespace gravel;

// Build a complete graph K_n
static ArrayGraph make_complete(uint32_t n) {
    std::vector<Edge> edges;
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t j = i + 1; j < n; ++j) {
            edges.push_back({i, j, 1.0});
            edges.push_back({j, i, 1.0});
        }
    return ArrayGraph(n, std::move(edges));
}

// Build a path graph: 0-1-2-...(n-1)
static ArrayGraph make_path(uint32_t n) {
    std::vector<Edge> edges;
    for (uint32_t i = 0; i + 1 < n; ++i) {
        edges.push_back({i, i + 1, 1.0});
        edges.push_back({i + 1, i, 1.0});
    }
    return ArrayGraph(n, std::move(edges));
}

// Build a disconnected graph: two components
static ArrayGraph make_disconnected() {
    // 0-1-2 and 3-4 (no edges between)
    std::vector<Edge> edges = {
        {0, 1, 1.0}, {1, 0, 1.0},
        {1, 2, 1.0}, {2, 1, 1.0},
        {3, 4, 1.0}, {4, 3, 1.0},
    };
    return ArrayGraph(5, std::move(edges));
}

TEST_CASE("Algebraic connectivity of K_n equals n", "[algebraic_connectivity]") {
    // For K_n, all nonzero eigenvalues of L are n. So λ₂ = n.
    auto k5 = make_complete(5);
    double ac = algebraic_connectivity(k5);
    REQUIRE_THAT(ac, Catch::Matchers::WithinRel(5.0, 0.01));

    auto k10 = make_complete(10);
    ac = algebraic_connectivity(k10);
    REQUIRE_THAT(ac, Catch::Matchers::WithinRel(10.0, 0.01));
}

TEST_CASE("Algebraic connectivity of path graph", "[algebraic_connectivity]") {
    // For path of length n: λ₂ = 2(1 - cos(π/n))
    uint32_t n = 10;
    auto path = make_path(n);
    double expected = 2.0 * (1.0 - std::cos(gravel::PI / n));
    double ac = algebraic_connectivity(path);
    REQUIRE_THAT(ac, Catch::Matchers::WithinRel(expected, 0.01));
}

TEST_CASE("Algebraic connectivity of disconnected graph is 0", "[algebraic_connectivity]") {
    auto g = make_disconnected();
    double ac = algebraic_connectivity(g);
    REQUIRE_THAT(ac, Catch::Matchers::WithinAbs(0.0, 1e-6));
}
