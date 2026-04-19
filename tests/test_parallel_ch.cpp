#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/ch/contraction.h"
#include "gravel/ch/ch_query.h"
#include "gravel/validation/synthetic_graphs.h"
#include <utility>
#include <vector>

using namespace gravel;

TEST_CASE("Parallel CH produces query-equivalent distances on grid", "[parallel_ch]") {
    auto graph = make_grid_graph(10, 10);

    CHBuildConfig seq_config;
    auto seq_ch = build_ch(*graph, seq_config);
    CHQuery seq_q(seq_ch);

    CHBuildConfig par_config;
    par_config.parallel = true;
    auto par_ch = build_ch(*graph, par_config);
    CHQuery par_q(par_ch);

    std::vector<std::pair<NodeID, NodeID>> pairs = {
        {0, 99}, {5, 94}, {10, 89}, {45, 54}, {0, 9}, {90, 99}
    };

    for (auto [s, t] : pairs) {
        Weight seq_d = seq_q.distance(s, t);
        Weight par_d = par_q.distance(s, t);
        REQUIRE_THAT(seq_d, Catch::Matchers::WithinAbs(par_d, 1e-6));
    }
}

TEST_CASE("Parallel CH produces query-equivalent distances on tree_with_bridges", "[parallel_ch]") {
    auto graph = make_tree_with_bridges(100, 30, 42);

    CHBuildConfig seq_config;
    auto seq_ch = build_ch(*graph, seq_config);
    CHQuery seq_q(seq_ch);

    CHBuildConfig par_config;
    par_config.parallel = true;
    auto par_ch = build_ch(*graph, par_config);
    CHQuery par_q(par_ch);

    std::vector<std::pair<NodeID, NodeID>> pairs = {
        {0, 50}, {10, 90}, {25, 75}, {1, 99}, {30, 60}
    };

    for (auto [s, t] : pairs) {
        Weight seq_d = seq_q.distance(s, t);
        Weight par_d = par_q.distance(s, t);
        REQUIRE_THAT(seq_d, Catch::Matchers::WithinAbs(par_d, 1e-6));
    }
}

TEST_CASE("Parallel CH known distances on small grid", "[parallel_ch]") {
    auto graph = make_grid_graph(5, 5);

    CHBuildConfig par_config;
    par_config.parallel = true;
    auto par_ch = build_ch(*graph, par_config);
    CHQuery q(par_ch);

    // 5x5 grid with unit weights: d(0, 24) = 8 (Manhattan distance)
    REQUIRE_THAT(q.distance(0, 24), Catch::Matchers::WithinAbs(8.0, 1e-6));
    // d(0, 4) = 4
    REQUIRE_THAT(q.distance(0, 4), Catch::Matchers::WithinAbs(4.0, 1e-6));
    // d(12, 12) = 0
    REQUIRE_THAT(q.distance(12, 12), Catch::Matchers::WithinAbs(0.0, 1e-6));
}
