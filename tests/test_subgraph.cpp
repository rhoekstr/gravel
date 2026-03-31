#include <catch2/catch_test_macros.hpp>
#include "gravel/core/subgraph.h"
#include "gravel/core/geo_math.h"

using namespace gravel;

// Build a 5x5 grid with coordinates mapped to a small lat/lon area.
// Node (r,c) at lat = 35.0 + r*0.01, lon = -83.0 + c*0.01
static ArrayGraph make_coord_grid(uint32_t rows, uint32_t cols) {
    NodeID n = rows * cols;
    auto node_id = [cols](uint32_t r, uint32_t c) -> NodeID {
        return r * cols + c;
    };

    std::vector<Edge> edges;
    std::vector<Coord> coords(n);
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            NodeID u = node_id(r, c);
            coords[u] = {35.0 + r * 0.01, -83.0 + c * 0.01};
            if (c + 1 < cols) {
                NodeID v = node_id(r, c + 1);
                edges.push_back({u, v, 1.0});
                edges.push_back({v, u, 1.0});
            }
            if (r + 1 < rows) {
                NodeID v = node_id(r + 1, c);
                edges.push_back({u, v, 1.0});
                edges.push_back({v, u, 1.0});
            }
        }
    }

    // Build via SoA constructor
    std::vector<uint32_t> offsets(n + 1, 0);
    for (auto& e : edges) offsets[e.source + 1]++;
    for (NodeID i = 1; i <= n; ++i) offsets[i] += offsets[i - 1];

    std::vector<NodeID> targets(edges.size());
    std::vector<Weight> weights(edges.size());
    auto pos = offsets;
    for (auto& e : edges) {
        uint32_t idx = pos[e.source]++;
        targets[idx] = e.target;
        weights[idx] = e.weight;
    }

    return ArrayGraph(std::move(offsets), std::move(targets),
                      std::move(weights), std::move(coords));
}

TEST_CASE("Point-in-polygon basic", "[geo][subgraph]") {
    // Square polygon: lat [35.0, 35.04], lon [-83.0, -82.96]
    std::vector<Coord> square = {
        {35.0, -83.0}, {35.0, -82.96}, {35.04, -82.96}, {35.04, -83.0}, {35.0, -83.0}
    };

    REQUIRE(point_in_polygon({35.02, -82.98}, square));   // inside
    REQUIRE_FALSE(point_in_polygon({35.1, -82.98}, square));  // outside (north)
    REQUIRE_FALSE(point_in_polygon({35.02, -82.9}, square));  // outside (east)
}

TEST_CASE("Subgraph extraction from grid", "[subgraph]") {
    auto grid = make_coord_grid(5, 5);
    REQUIRE(grid.node_count() == 25);

    // Rectangle capturing the 3x3 interior: rows 1-3, cols 1-3
    // lat [35.005, 35.035], lon [-82.995, -82.965]
    Polygon rect;
    rect.vertices = {
        {35.005, -82.995}, {35.005, -82.965},
        {35.035, -82.965}, {35.035, -82.995},
        {35.005, -82.995}
    };

    auto result = extract_subgraph(grid, rect);
    REQUIRE(result.graph != nullptr);
    REQUIRE(result.graph->node_count() == 9);  // 3x3 interior
    REQUIRE(result.new_to_original.size() == 9);
    REQUIRE(result.original_to_new.size() == 9);

    // Each interior grid node has 2-4 neighbors within the subgraph
    // Corner of subgraph: 2 edges; edge of subgraph: 3 edges; center: 4 edges
    // 4 corners × 2 + 4 edges × 3 + 1 center × 4 = 8 + 12 + 4 = 24 directed edges
    REQUIRE(result.graph->edge_count() == 24);
}

TEST_CASE("Subgraph preserves coordinates", "[subgraph]") {
    auto grid = make_coord_grid(3, 3);

    // Capture all nodes
    Polygon big;
    big.vertices = {
        {34.99, -83.01}, {34.99, -82.97},
        {35.03, -82.97}, {35.03, -83.01},
        {34.99, -83.01}
    };

    auto result = extract_subgraph(grid, big);
    REQUIRE(result.graph->node_count() == 9);

    // Verify coordinates are preserved
    for (NodeID new_id = 0; new_id < result.graph->node_count(); ++new_id) {
        NodeID orig = result.new_to_original[new_id];
        auto orig_coord = grid.node_coordinate(orig);
        auto new_coord = result.graph->node_coordinate(new_id);
        REQUIRE(orig_coord.has_value());
        REQUIRE(new_coord.has_value());
        REQUIRE(orig_coord->lat == new_coord->lat);
        REQUIRE(orig_coord->lon == new_coord->lon);
    }
}

TEST_CASE("Subgraph empty polygon", "[subgraph]") {
    auto grid = make_coord_grid(3, 3);

    // Polygon far from any nodes
    Polygon empty;
    empty.vertices = {
        {40.0, -70.0}, {40.0, -69.0}, {41.0, -69.0}, {41.0, -70.0}, {40.0, -70.0}
    };

    auto result = extract_subgraph(grid, empty);
    REQUIRE(result.graph->node_count() == 0);
    REQUIRE(result.graph->edge_count() == 0);
}
