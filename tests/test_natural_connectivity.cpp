#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/analysis/natural_connectivity.h"
#include "gravel/analysis/laplacian.h"
#include <Eigen/Dense>
#include <cmath>
#include <utility>
#include <vector>

using namespace gravel;

// Build a small graph for brute-force comparison
static ArrayGraph make_cycle(uint32_t n) {
    std::vector<Edge> edges;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t j = (i + 1) % n;
        edges.push_back({i, j, 1.0});
        edges.push_back({j, i, 1.0});
    }
    return ArrayGraph(n, std::move(edges));
}

static ArrayGraph make_complete(uint32_t n) {
    std::vector<Edge> edges;
    for (uint32_t i = 0; i < n; ++i)
        for (uint32_t j = i + 1; j < n; ++j) {
            edges.push_back({i, j, 1.0});
            edges.push_back({j, i, 1.0});
        }
    return ArrayGraph(n, std::move(edges));
}

// Brute-force: compute eigenvalues of A, then ln((1/n)Σexp(λ))
static double brute_force_nc(const ArrayGraph& graph) {
    auto A = gravel::build_adjacency(graph);
    Eigen::MatrixXd Ad = Eigen::MatrixXd(A);
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(Ad);
    auto eigenvalues = solver.eigenvalues();
    double sum_exp = 0.0;
    for (int i = 0; i < eigenvalues.size(); ++i) {
        sum_exp += std::exp(eigenvalues(i));
    }
    return std::log(sum_exp / graph.node_count());
}

TEST_CASE("Natural connectivity SLQ vs brute-force on cycle", "[natural_connectivity]") {
    auto cycle = make_cycle(8);
    double bf = brute_force_nc(cycle);
    double slq = natural_connectivity(cycle, 30, 50, 42);

    // Should match within 5%
    REQUIRE_THAT(slq, Catch::Matchers::WithinRel(bf, 0.05));
}

TEST_CASE("Natural connectivity SLQ vs brute-force on K_6", "[natural_connectivity]") {
    auto k6 = make_complete(6);
    double bf = brute_force_nc(k6);
    double slq = natural_connectivity(k6, 30, 50, 42);

    REQUIRE_THAT(slq, Catch::Matchers::WithinRel(bf, 0.05));
}

TEST_CASE("Natural connectivity - more connected graph has higher value", "[natural_connectivity]") {
    auto path_graph = [](uint32_t n) {
        std::vector<Edge> edges;
        for (uint32_t i = 0; i + 1 < n; ++i) {
            edges.push_back({i, i + 1, 1.0});
            edges.push_back({i + 1, i, 1.0});
        }
        return ArrayGraph(n, std::move(edges));
    };

    auto path = path_graph(8);
    auto complete = make_complete(8);

    double nc_path = natural_connectivity(path);
    double nc_complete = natural_connectivity(complete);

    REQUIRE(nc_complete > nc_path);
}
