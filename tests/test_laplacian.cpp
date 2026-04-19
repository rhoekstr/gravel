#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/analysis/laplacian.h"
#include <cmath>
#include <utility>
#include <vector>

using namespace gravel;

// Build a cycle graph: 0-1-2-...(n-1)-0
static ArrayGraph make_cycle(uint32_t n) {
    std::vector<Edge> edges;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t j = (i + 1) % n;
        edges.push_back({i, j, 1.0});
        edges.push_back({j, i, 1.0});
    }
    return ArrayGraph(n, std::move(edges));
}

// Build a complete graph K_n
static ArrayGraph make_complete(uint32_t n) {
    std::vector<Edge> edges;
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = i + 1; j < n; ++j) {
            edges.push_back({i, j, 1.0});
            edges.push_back({j, i, 1.0});
        }
    }
    return ArrayGraph(n, std::move(edges));
}

TEST_CASE("Laplacian of cycle graph — row sums zero", "[laplacian]") {
    auto cycle = make_cycle(6);
    auto L = build_laplacian(cycle);

    REQUIRE(L.rows() == 6);
    REQUIRE(L.cols() == 6);

    for (int i = 0; i < L.rows(); ++i) {
        double row_sum = 0.0;
        for (Eigen::SparseMatrix<double>::InnerIterator it(L, i); it; ++it) {
            // Note: Eigen stores column-major by default, but InnerIterator
            // over column k gives all rows in that column.
            // For row sum, iterate differently.
        }
        // Sum row i by iterating all columns
        double sum = 0.0;
        for (int j = 0; j < L.cols(); ++j) {
            sum += L.coeff(i, j);
        }
        REQUIRE_THAT(sum, Catch::Matchers::WithinAbs(0.0, 1e-10));
    }
}

TEST_CASE("Laplacian of cycle — diagonal equals degree", "[laplacian]") {
    auto cycle = make_cycle(6);
    auto L = build_laplacian(cycle);

    // Each node in a cycle has degree 2
    for (int i = 0; i < 6; ++i) {
        REQUIRE_THAT(L.coeff(i, i), Catch::Matchers::WithinAbs(2.0, 1e-10));
    }
}

TEST_CASE("Laplacian of cycle — off-diagonal is -1 for neighbors", "[laplacian]") {
    auto cycle = make_cycle(6);
    auto L = build_laplacian(cycle);

    // Node 0 is connected to 1 and 5
    REQUIRE_THAT(L.coeff(0, 1), Catch::Matchers::WithinAbs(-1.0, 1e-10));
    REQUIRE_THAT(L.coeff(0, 5), Catch::Matchers::WithinAbs(-1.0, 1e-10));
    // Node 0 not connected to 3
    REQUIRE_THAT(L.coeff(0, 3), Catch::Matchers::WithinAbs(0.0, 1e-10));
}

TEST_CASE("Adjacency of complete graph K_5", "[laplacian]") {
    auto k5 = make_complete(5);
    auto A = build_adjacency(k5);

    REQUIRE(A.rows() == 5);
    // Diagonal should be 0
    for (int i = 0; i < 5; ++i) {
        REQUIRE_THAT(A.coeff(i, i), Catch::Matchers::WithinAbs(0.0, 1e-10));
    }
    // Off-diagonal should be 1
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j) {
            if (i != j) {
                REQUIRE_THAT(A.coeff(i, j), Catch::Matchers::WithinAbs(1.0, 1e-10));
            }
        }
    }
}

TEST_CASE("Laplacian of K_n — row sums zero, diagonal = n-1", "[laplacian]") {
    auto k4 = make_complete(4);
    auto L = build_laplacian(k4);

    for (int i = 0; i < 4; ++i) {
        REQUIRE_THAT(L.coeff(i, i), Catch::Matchers::WithinAbs(3.0, 1e-10));
        double sum = 0.0;
        for (int j = 0; j < 4; ++j) sum += L.coeff(i, j);
        REQUIRE_THAT(sum, Catch::Matchers::WithinAbs(0.0, 1e-10));
    }
}
