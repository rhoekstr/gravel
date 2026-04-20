#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/analysis/county_fragility.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/shortcut_index.h"
#include <utility>
#include <vector>

using namespace gravel;

// Build a 5x5 grid with coordinates
static ArrayGraph make_coord_grid(uint32_t rows, uint32_t cols) {
    NodeID n = rows * cols;
    auto node_id = [cols](uint32_t r, uint32_t c) -> NodeID { return r * cols + c; };

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

TEST_CASE("County fragility index produces populated results", "[county_fragility]") {
    auto grid = make_coord_grid(7, 7);

    CHBuildConfig ch_config;
    auto ch = build_ch(grid, ch_config);
    ShortcutIndex idx(ch);

    // Extract inner 5x5 region
    CountyFragilityConfig config;
    config.boundary.vertices = {
        {35.005, -82.995}, {35.005, -82.955},
        {35.055, -82.955}, {35.055, -82.995},
        {35.005, -82.995}
    };
    config.od_sample_count = 10;
    config.seed = 42;

    auto result = county_fragility_index(grid, ch, idx, config);

    // All sub-metrics should be populated
    REQUIRE(result.subgraph_nodes >= 16);  // at least inner nodes captured
    REQUIRE(result.subgraph_nodes <= 25);  // at most 5x5
    REQUIRE(result.subgraph_edges > 0);
    REQUIRE(result.algebraic_connectivity > 0.0);
    REQUIRE(result.kirchhoff_index_value > 0.0);
    REQUIRE(result.entry_point_count > 0);
    REQUIRE(result.composite_index > 0.0);
    REQUIRE(result.composite_index <= 1.0);
}

TEST_CASE("County fragility - grid has no bridges", "[county_fragility]") {
    auto grid = make_coord_grid(5, 5);

    CHBuildConfig ch_config;
    auto ch = build_ch(grid, ch_config);
    ShortcutIndex idx(ch);

    // Capture all 25 nodes
    CountyFragilityConfig config;
    config.boundary.vertices = {
        {34.99, -83.01}, {34.99, -82.95},
        {35.05, -82.95}, {35.05, -83.01},
        {34.99, -83.01}
    };
    config.od_sample_count = 5;

    auto result = county_fragility_index(grid, ch, idx, config);
    REQUIRE(result.bridges.bridges.empty());
}
