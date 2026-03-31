#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/ch/landmarks.h"
#include "gravel/validation/synthetic_graphs.h"
#include "gravel/core/dijkstra.h"

using namespace gravel;

TEST_CASE("Landmark triangle inequality holds", "[landmarks]") {
    auto graph = make_grid_graph(10, 10);
    auto lm = precompute_landmarks(*graph, 4, 42);

    REQUIRE(lm.num_landmarks == 4);
    REQUIRE(lm.dist_from.size() == 4);
    REQUIRE(lm.dist_to.size() == 4);

    // Check triangle inequality: lower_bound(s,t) <= actual_distance(s,t)
    // Use Dijkstra for ground truth
    std::vector<std::pair<NodeID, NodeID>> pairs = {
        {0, 99}, {5, 94}, {10, 89}, {45, 54}, {0, 9}
    };

    for (auto [s, t] : pairs) {
        Weight lb = lm.lower_bound(s, t);
        auto dijk = dijkstra(*graph, s);
        Weight actual = dijk.distances[t];
        REQUIRE(lb <= actual + 1e-6);
        REQUIRE(lb > 0.0);  // should be a positive lower bound
    }
}

TEST_CASE("Landmark lower bound is non-negative", "[landmarks]") {
    auto graph = make_grid_graph(5, 5);
    auto lm = precompute_landmarks(*graph, 3, 42);

    for (NodeID s = 0; s < 25; ++s) {
        for (NodeID t = 0; t < 25; ++t) {
            Weight lb = lm.lower_bound(s, t);
            REQUIRE(lb >= 0.0);
            if (s == t) REQUIRE_THAT(lb, Catch::Matchers::WithinAbs(0.0, 1e-6));
        }
    }
}
