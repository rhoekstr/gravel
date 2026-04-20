#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/core/array_graph.h"
#include <utility>
#include <vector>

using namespace gravel;

TEST_CASE("ArrayGraph from edge list - triangle", "[array_graph]") {
    // Triangle: 0->1 (w=1), 0->2 (w=4), 1->2 (w=2)
    std::vector<Edge> edges = {
        {0, 1, 1.0},
        {0, 2, 4.0},
        {1, 2, 2.0},
    };
    ArrayGraph g(3, std::move(edges));

    REQUIRE(g.node_count() == 3);
    REQUIRE(g.edge_count() == 3);

    SECTION("node 0 has two outgoing edges") {
        auto targets = g.outgoing_targets(0);
        auto weights = g.outgoing_weights(0);
        REQUIRE(targets.size() == 2);
        REQUIRE(targets[0] == 1);
        REQUIRE(weights[0] == 1.0);
        REQUIRE(targets[1] == 2);
        REQUIRE(weights[1] == 4.0);
    }

    SECTION("node 1 has one outgoing edge") {
        auto targets = g.outgoing_targets(1);
        auto weights = g.outgoing_weights(1);
        REQUIRE(targets.size() == 1);
        REQUIRE(targets[0] == 2);
        REQUIRE(weights[0] == 2.0);
    }

    SECTION("node 2 has no outgoing edges") {
        REQUIRE(g.degree(2) == 0);
        REQUIRE(g.outgoing_targets(2).empty());
    }

    SECTION("no coordinates by default") {
        REQUIRE(!g.node_coordinate(0).has_value());
    }
}

TEST_CASE("ArrayGraph from SoA CSR arrays", "[array_graph]") {
    // Same triangle as above, but pre-built SoA CSR
    std::vector<uint32_t> offsets = {0, 2, 3, 3};
    std::vector<NodeID> targets = {1, 2, 2};
    std::vector<Weight> weights = {1.0, 4.0, 2.0};
    std::vector<Coord> coords = {{35.0, -83.0}, {35.1, -83.1}, {35.2, -83.2}};

    ArrayGraph g(std::move(offsets), std::move(targets), std::move(weights), std::move(coords));

    REQUIRE(g.node_count() == 3);
    REQUIRE(g.edge_count() == 3);

    SECTION("coordinates are accessible") {
        auto c = g.node_coordinate(0);
        REQUIRE(c.has_value());
        REQUIRE_THAT(c->lat, Catch::Matchers::WithinAbs(35.0, 1e-9));
    }
}

TEST_CASE("ArrayGraph with isolated nodes", "[array_graph]") {
    // 5 nodes but only 1 edge: 0->1
    std::vector<Edge> edges = {{0, 1, 3.0}};
    ArrayGraph g(5, std::move(edges));

    REQUIRE(g.node_count() == 5);
    REQUIRE(g.edge_count() == 1);
    REQUIRE(g.degree(0) == 1);
    REQUIRE(g.degree(1) == 0);
    REQUIRE(g.degree(4) == 0);
}

TEST_CASE("ArrayGraph raw accessors", "[array_graph]") {
    std::vector<Edge> edges = {{0, 1, 2.5}, {1, 0, 3.5}};
    ArrayGraph g(2, std::move(edges));

    REQUIRE(g.raw_offsets().size() == 3);
    REQUIRE(g.raw_targets().size() == 2);
    REQUIRE(g.raw_weights().size() == 2);
    REQUIRE(g.raw_coords().empty());
}
