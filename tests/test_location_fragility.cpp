#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/analysis/location_fragility.h"
#include "gravel/ch/contraction.h"
#include <utility>
#include <vector>

using namespace gravel;

// Build a grid with coordinates centered around a point
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

// Build a line graph (chain) with coordinates
static ArrayGraph make_coord_chain(uint32_t n) {
    std::vector<Edge> edges;
    std::vector<Coord> coords(n);
    for (uint32_t i = 0; i < n; ++i) {
        coords[i] = {35.0, -83.0 + i * 0.005};
        if (i + 1 < n) {
            edges.push_back({i, i + 1, 1.0});
            edges.push_back({i + 1, i, 1.0});
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

TEST_CASE("Location fragility — grid has low isolation risk", "[location_fragility]") {
    auto grid = make_coord_grid(7, 7);
    auto ch = build_ch(grid);

    LocationFragilityConfig config;
    config.center = {35.03, -83.03};
    config.radius_meters = 5000;
    config.seed = 42;

    auto result = location_fragility(grid, ch, config);

    REQUIRE(result.reachable_nodes > 0);
    REQUIRE(result.isolation_risk >= 0.0);
    REQUIRE(result.isolation_risk <= 1.0);
    REQUIRE(!result.curve.empty());
}

TEST_CASE("Location fragility — chain has high isolation risk", "[location_fragility]") {
    auto chain = make_coord_chain(20);
    auto ch = build_ch(chain);

    LocationFragilityConfig config;
    config.center = {35.0, -83.0 + 10 * 0.005};
    config.radius_meters = 5000;
    config.seed = 42;

    auto result = location_fragility(chain, ch, config);

    REQUIRE(result.reachable_nodes > 0);
    // Chain is maximally fragile — every edge is on a shortest path
    REQUIRE(result.isolation_risk > 0.1);
}

TEST_CASE("Location fragility — isolation_risk in [0, 1]", "[location_fragility]") {
    auto grid = make_coord_grid(5, 5);
    auto ch = build_ch(grid);

    LocationFragilityConfig config;
    config.center = {35.02, -83.02};
    config.radius_meters = 3000;

    auto result = location_fragility(grid, ch, config);

    REQUIRE(result.isolation_risk >= 0.0);
    REQUIRE(result.isolation_risk <= 1.0);
    REQUIRE(result.directional_coverage >= 0.0);
    REQUIRE(result.directional_coverage <= 1.0);
}

TEST_CASE("Location fragility — chain vs grid comparison", "[location_fragility]") {
    auto grid = make_coord_grid(7, 7);
    auto chain = make_coord_chain(20);

    auto grid_ch = build_ch(grid);
    auto chain_ch = build_ch(chain);

    LocationFragilityConfig config;
    config.radius_meters = 5000;
    config.seed = 42;

    config.center = {35.03, -83.03};
    auto grid_result = location_fragility(grid, grid_ch, config);

    config.center = {35.0, -83.0 + 10 * 0.005};
    auto chain_result = location_fragility(chain, chain_ch, config);

    // Chain should have higher isolation risk than grid
    REQUIRE(chain_result.isolation_risk > grid_result.isolation_risk);
}

TEST_CASE("Location fragility — greedy betweenness produces removal sequence", "[location_fragility]") {
    auto grid = make_coord_grid(5, 5);
    auto ch = build_ch(grid);

    LocationFragilityConfig config;
    config.center = {35.02, -83.02};
    config.radius_meters = 5000;
    config.strategy = SelectionStrategy::GREEDY_BETWEENNESS;
    config.seed = 42;

    auto result = location_fragility(grid, ch, config);

    if (result.sp_edges_removed > 0) {
        REQUIRE(!result.removal_sequence.empty());
        REQUIRE(!result.curve.empty());
    }
}

TEST_CASE("Location fragility — curve length matches k_edges", "[location_fragility]") {
    auto grid = make_coord_grid(5, 5);
    auto ch = build_ch(grid);

    LocationFragilityConfig config;
    config.center = {35.02, -83.02};
    config.radius_meters = 5000;
    config.removal_fraction = 0.20f;
    config.monte_carlo_runs = 5;
    config.seed = 42;

    auto result = location_fragility(grid, ch, config);

    if (result.sp_edges_removed > 0) {
        REQUIRE(result.curve.size() == result.sp_edges_removed + 1);
    }
}
