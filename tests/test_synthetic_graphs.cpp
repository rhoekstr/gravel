#include <catch2/catch_test_macros.hpp>
#include "gravel/validation/synthetic_graphs.h"
#include "gravel/core/dijkstra.h"

using namespace gravel;

TEST_CASE("Grid graph structure", "[synthetic]") {
    auto g = make_grid_graph(5, 4);  // 20 nodes

    REQUIRE(g->node_count() == 20);

    // Each interior node has 4 edges (bidirectional grid)
    // Corner nodes have 2, edge nodes have 3
    // Total edges: 2 * (rows*(cols-1) + cols*(rows-1)) = 2 * (5*3 + 4*4) = 2 * 31 = 62
    REQUIRE(g->edge_count() == 62);
}

TEST_CASE("Grid graph shortest paths are manhattan distances", "[synthetic]") {
    auto g = make_grid_graph(10, 10);
    auto result = dijkstra(*g, 0);

    // (0,0) to (9,9) = 18
    REQUIRE(result.distances[99] == 18.0);

    // (0,0) to (0,9) = 9
    REQUIRE(result.distances[9] == 9.0);

    // (0,0) to (9,0) = 9
    REQUIRE(result.distances[90] == 9.0);
}

TEST_CASE("Random graph is connected", "[synthetic]") {
    auto g = make_random_graph(100, 200, 1.0, 10.0, 123);

    REQUIRE(g->node_count() == 100);

    // Verify connectivity: Dijkstra from node 0 should reach all nodes
    auto result = dijkstra(*g, 0);
    for (NodeID i = 0; i < g->node_count(); ++i) {
        REQUIRE(result.distances[i] < INF_WEIGHT);
    }
}

TEST_CASE("Random graph is reproducible with same seed", "[synthetic]") {
    auto g1 = make_random_graph(50, 100, 1.0, 10.0, 42);
    auto g2 = make_random_graph(50, 100, 1.0, 10.0, 42);

    REQUIRE(g1->edge_count() == g2->edge_count());

    // Check first node's edges are identical
    auto t1 = g1->outgoing_targets(0);
    auto t2 = g2->outgoing_targets(0);
    auto w1 = g1->outgoing_weights(0);
    auto w2 = g2->outgoing_weights(0);
    REQUIRE(t1.size() == t2.size());
    for (size_t i = 0; i < t1.size(); ++i) {
        REQUIRE(t1[i] == t2[i]);
        REQUIRE(w1[i] == w2[i]);
    }
}

TEST_CASE("Tree with bridges is connected", "[synthetic]") {
    auto g = make_tree_with_bridges(200, 20, 99);

    REQUIRE(g->node_count() == 200);

    auto result = dijkstra(*g, 0);
    for (NodeID i = 0; i < g->node_count(); ++i) {
        REQUIRE(result.distances[i] < INF_WEIGHT);
    }
}

TEST_CASE("Tree with no extra edges has bridge structure", "[synthetic]") {
    auto g = make_tree_with_bridges(50, 0, 77);

    REQUIRE(g->node_count() == 50);
    // A tree with n nodes has n-1 edges, bidirectional = 2*(n-1)
    REQUIRE(g->edge_count() == 98);

    // Still connected
    auto result = dijkstra(*g, 0);
    for (NodeID i = 0; i < g->node_count(); ++i) {
        REQUIRE(result.distances[i] < INF_WEIGHT);
    }
}
