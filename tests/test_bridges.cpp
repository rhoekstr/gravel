#include <catch2/catch_test_macros.hpp>
#include "gravel/simplify/bridges.h"
#include "gravel/validation/synthetic_graphs.h"
#include "gravel/core/dijkstra.h"
#include <utility>
#include <vector>

using namespace gravel;

TEST_CASE("Tree graph - all edges are bridges", "[bridges]") {
    // Chain: 0-1-2-3 (bidirectional)
    std::vector<Edge> edges;
    for (int i = 0; i < 3; ++i) {
        edges.push_back({(NodeID)i, (NodeID)(i + 1), 1.0});
        edges.push_back({(NodeID)(i + 1), (NodeID)i, 1.0});
    }
    ArrayGraph g(4, std::move(edges));

    auto result = find_bridges(g);
    REQUIRE(result.bridges.size() == 3);
}

TEST_CASE("Cycle graph - no bridges", "[bridges]") {
    // Triangle: 0-1, 1-2, 2-0 (bidirectional)
    std::vector<Edge> edges = {
        {0, 1, 1.0}, {1, 0, 1.0},
        {1, 2, 1.0}, {2, 1, 1.0},
        {2, 0, 1.0}, {0, 2, 1.0}
    };
    ArrayGraph g(3, std::move(edges));

    auto result = find_bridges(g);
    REQUIRE(result.bridges.empty());
}

TEST_CASE("Parallel edges are not bridges", "[bridges]") {
    // Two edges between 0 and 1 (bidirectional each)
    std::vector<Edge> edges = {
        {0, 1, 1.0}, {1, 0, 1.0},
        {0, 1, 2.0}, {1, 0, 2.0}
    };
    ArrayGraph g(2, std::move(edges));

    auto result = find_bridges(g);
    REQUIRE(result.bridges.empty());
}

TEST_CASE("Bridge connects two components", "[bridges]") {
    // Two triangles connected by a single bridge edge:
    // Triangle A: 0-1-2-0
    // Triangle B: 3-4-5-3
    // Bridge: 2-3
    std::vector<Edge> edges = {
        {0, 1, 1.0}, {1, 0, 1.0},
        {1, 2, 1.0}, {2, 1, 1.0},
        {2, 0, 1.0}, {0, 2, 1.0},
        {2, 3, 1.0}, {3, 2, 1.0},  // bridge
        {3, 4, 1.0}, {4, 3, 1.0},
        {4, 5, 1.0}, {5, 4, 1.0},
        {5, 3, 1.0}, {3, 5, 1.0}
    };
    ArrayGraph g(6, std::move(edges));

    auto result = find_bridges(g);
    REQUIRE(result.bridges.size() == 1);
    REQUIRE(result.bridges[0] == std::make_pair((NodeID)2, (NodeID)3));
}

TEST_CASE("Synthetic tree_with_bridges has known bridge count", "[bridges]") {
    // tree_with_bridges(n, extra_edges_per_node=0) is a pure tree → n-1 bridges
    auto g = make_tree_with_bridges(20, 0);
    auto result = find_bridges(*g);
    REQUIRE(result.bridges.size() == 19);
}

TEST_CASE("Removing a detected bridge disconnects the graph", "[bridges]") {
    // Chain 0-1-2-3, all bridges. Remove bridge (1,2) and verify disconnection.
    std::vector<Edge> edges;
    for (int i = 0; i < 3; ++i) {
        edges.push_back({(NodeID)i, (NodeID)(i + 1), 1.0});
        edges.push_back({(NodeID)(i + 1), (NodeID)i, 1.0});
    }
    ArrayGraph g(4, std::move(edges));

    auto result = find_bridges(g);
    REQUIRE(!result.bridges.empty());

    // Pick bridge (1,2) and build graph without it
    auto [bu, bv] = result.bridges[1];  // (1,2)
    std::vector<Edge> remaining;
    for (int i = 0; i < 3; ++i) {
        NodeID u = i, v = i + 1;
        if ((u == bu && v == bv) || (u == bv && v == bu)) continue;
        remaining.push_back({u, v, 1.0});
        remaining.push_back({v, u, 1.0});
    }
    ArrayGraph g2(4, std::move(remaining));

    // Node 0 should not be able to reach node 3
    Weight dist = dijkstra_pair(g2, 0, 3);
    REQUIRE(dist == INF_WEIGHT);
}

TEST_CASE("Empty graph has no bridges", "[bridges]") {
    std::vector<Edge> edges;
    ArrayGraph g(0, std::move(edges));
    auto result = find_bridges(g);
    REQUIRE(result.bridges.empty());
}

TEST_CASE("Single node has no bridges", "[bridges]") {
    std::vector<Edge> edges;
    ArrayGraph g(1, std::move(edges));
    auto result = find_bridges(g);
    REQUIRE(result.bridges.empty());
}
