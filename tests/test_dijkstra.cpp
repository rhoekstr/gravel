#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/core/dijkstra.h"
#include "gravel/core/array_graph.h"
#include <memory>
#include <utility>
#include <vector>

using namespace gravel;
using Catch::Matchers::WithinAbs;

static std::unique_ptr<ArrayGraph> make_5node_graph() {
    //     1
    //  0 ---> 1
    //  |      |
    // 4|      |2
    //  v      v
    //  3 ---> 2
    //     1
    //  0 ---> 4 (weight 10)
    std::vector<Edge> edges = {
        {0, 1, 1.0},
        {0, 3, 4.0},
        {0, 4, 10.0},
        {1, 2, 2.0},
        {3, 2, 1.0},
        {2, 4, 3.0},
    };
    return std::make_unique<ArrayGraph>(5, std::move(edges));
}

TEST_CASE("Dijkstra single-source on 5-node graph", "[dijkstra]") {
    auto g = make_5node_graph();
    auto result = dijkstra(*g, 0);

    REQUIRE_THAT(result.distances[0], WithinAbs(0.0, 1e-9));
    REQUIRE_THAT(result.distances[1], WithinAbs(1.0, 1e-9));
    REQUIRE_THAT(result.distances[2], WithinAbs(3.0, 1e-9));  // 0->1->2
    REQUIRE_THAT(result.distances[3], WithinAbs(4.0, 1e-9));
    REQUIRE_THAT(result.distances[4], WithinAbs(6.0, 1e-9));  // 0->1->2->4
}

TEST_CASE("Dijkstra pair stops early", "[dijkstra]") {
    auto g = make_5node_graph();

    REQUIRE_THAT(dijkstra_pair(*g, 0, 2), WithinAbs(3.0, 1e-9));
    REQUIRE_THAT(dijkstra_pair(*g, 0, 4), WithinAbs(6.0, 1e-9));
    REQUIRE(dijkstra_pair(*g, 4, 0) == INF_WEIGHT);  // no path backward
}

TEST_CASE("Dijkstra path reconstruction", "[dijkstra]") {
    auto g = make_5node_graph();
    auto result = dijkstra(*g, 0);

    auto path = reconstruct_path(result, 0, 4);
    REQUIRE(path == std::vector<NodeID>{0, 1, 2, 4});

    auto no_path = reconstruct_path(result, 0, 0);
    REQUIRE(no_path == std::vector<NodeID>{0});
}

TEST_CASE("Dijkstra unreachable node", "[dijkstra]") {
    auto g = make_5node_graph();
    auto result = dijkstra(*g, 4);  // node 4 has no outgoing edges

    REQUIRE_THAT(result.distances[4], WithinAbs(0.0, 1e-9));
    REQUIRE(result.distances[0] == INF_WEIGHT);

    auto path = reconstruct_path(result, 4, 0);
    REQUIRE(path.empty());
}

TEST_CASE("Dijkstra on 10x10 grid", "[dijkstra]") {
    // Grid: 10 rows x 10 cols, all weights = 1.0
    // Distance from (0,0) to (9,9) = 18 (manhattan distance)
    uint32_t rows = 10, cols = 10;
    NodeID n = rows * cols;
    std::vector<Edge> edges;
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            NodeID u = r * cols + c;
            if (c + 1 < cols) {
                NodeID v = r * cols + (c + 1);
                edges.push_back({u, v, 1.0});
                edges.push_back({v, u, 1.0});
            }
            if (r + 1 < rows) {
                NodeID v = (r + 1) * cols + c;
                edges.push_back({u, v, 1.0});
                edges.push_back({v, u, 1.0});
            }
        }
    }
    ArrayGraph g(n, std::move(edges));

    auto result = dijkstra(g, 0);
    REQUIRE_THAT(result.distances[99], WithinAbs(18.0, 1e-9));

    // Also test pair variant
    REQUIRE_THAT(dijkstra_pair(g, 0, 99), WithinAbs(18.0, 1e-9));
}
