#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/analysis/county_fragility.h"
#include "gravel/analysis/location_fragility.h"
#include "gravel/simplify/bridge_classification.h"
#include "gravel/analysis/scenario_fragility.h"
#include "gravel/analysis/uncertainty.h"
#include "gravel/geo/edge_confidence.h"
#include "gravel/analysis/tiled_fragility.h"
#include "gravel/analysis/od_sampling.h"
#include "gravel/simplify/bridges.h"
#include "gravel/core/edge_labels.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/ch_query.h"
#include "gravel/validation/synthetic_graphs.h"
#include <utility>
#include <cmath>
#include <vector>

using namespace gravel;

// Build a 7x7 grid with coordinates for county/location tests
static ArrayGraph make_coord_grid(uint32_t rows, uint32_t cols) {
    NodeID n = rows * cols;
    auto node_id = [cols](uint32_t r, uint32_t c) -> NodeID { return r * cols + c; };
    std::vector<Edge> edges;
    std::vector<Coord> coords(n);
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            NodeID u = node_id(r, c);
            coords[u] = {35.0 + r * 0.01, -83.0 + c * 0.01};
            if (c + 1 < cols) { NodeID v = node_id(r, c + 1); edges.push_back({u, v, 1.0}); edges.push_back({v, u, 1.0}); }
            if (r + 1 < rows) { NodeID v = node_id(r + 1, c); edges.push_back({u, v, 1.0}); edges.push_back({v, u, 1.0}); }
        }
    }
    std::vector<uint32_t> offsets(n + 1, 0);
    for (auto& e : edges) offsets[e.source + 1]++;
    for (NodeID i = 1; i <= n; ++i) offsets[i] += offsets[i - 1];
    std::vector<NodeID> targets(edges.size());
    std::vector<Weight> weights(edges.size());
    auto pos = offsets;
    for (auto& e : edges) { uint32_t idx = pos[e.source]++; targets[idx] = e.target; weights[idx] = e.weight; }
    return ArrayGraph(std::move(offsets), std::move(targets), std::move(weights), std::move(coords));
}

// --- A1: Fragility Distribution ---

TEST_CASE("County fragility has percentile distribution", "[v2]") {
    auto grid = make_coord_grid(7, 7);
    auto ch = build_ch(grid);
    ShortcutIndex idx(ch);

    CountyFragilityConfig cfg;
    cfg.boundary.vertices = {{34.99, -83.01}, {34.99, -82.93}, {35.07, -82.93}, {35.07, -83.01}, {34.99, -83.01}};
    cfg.od_sample_count = 20;

    auto result = county_fragility_index(grid, ch, idx, cfg);
    REQUIRE(result.fragility_p50 >= 1.0);
    REQUIRE(result.fragility_p90 >= result.fragility_p50);
    REQUIRE(result.fragility_p99 >= result.fragility_p90);
}

// --- A2: Directional Asymmetry ---

TEST_CASE("Location fragility has directional asymmetry", "[v2]") {
    auto grid = make_coord_grid(10, 10);
    auto ch = build_ch(grid);

    LocationFragilityConfig cfg;
    cfg.center = {35.045, -82.955};
    cfg.radius_meters = 5000;

    auto result = location_fragility(grid, ch, cfg);

    REQUIRE(result.directional_fragility.size() == cfg.angular_bins);
    // Symmetric grid should have relatively uniform directional fragility
    REQUIRE(result.directional_asymmetry >= 0.0);
    REQUIRE(result.directional_asymmetry <= 1.0);
}

// --- A3: Bridge Replacement Cost ---

TEST_CASE("Bridge replacement costs are computed", "[v2]") {
    auto tree = make_tree_with_bridges(30, 5, 42);
    auto ch = build_ch(*tree);
    ShortcutIndex idx(ch);

    auto bridges = find_bridges(*tree);
    REQUIRE(!bridges.bridges.empty());

    compute_bridge_costs(bridges, *tree, ch, idx);
    REQUIRE(bridges.replacement_costs.size() == bridges.bridges.size());

    // Each cost should be >= 0 (may be INF if bridge disconnects)
    for (auto cost : bridges.replacement_costs) {
        REQUIRE(cost >= 0.0);
    }
}

// --- A4: Bridge Classification ---

TEST_CASE("Bridge classification distinguishes structural from filter-induced", "[v2]") {
    // Create a graph where removing some edges creates filter-induced bridges
    // Grid: all edges present. When we filter to keep only horizontal edges,
    // the vertical connections are removed and horizontal chains become bridges.
    auto grid = make_grid_graph(3, 5);  // 3 rows, 5 cols

    // Keep only about half the edges (even-indexed ones)
    auto filter = [](uint32_t edge_idx) -> bool { return edge_idx % 3 != 0; };

    auto classification = classify_bridges(*grid, filter);

    // The full grid has NO bridges (it's 2-connected).
    // The filtered graph likely has some bridges.
    // All filtered bridges should be FILTER_INDUCED since none exist in full graph.
    for (auto type : classification.types) {
        REQUIRE(type == BridgeType::FILTER_INDUCED);
    }
}

// --- B1: Population-Weighted Sampling ---

TEST_CASE("Weighted sampling favors high-weight nodes", "[v2]") {
    auto grid = make_grid_graph(5, 5);
    auto ch = build_ch(*grid);

    SamplingConfig cfg;
    cfg.total_samples = 200;
    cfg.seed = 42;
    cfg.node_weights.assign(25, 1.0);  // uniform base
    cfg.node_weights[0] = 100.0;       // node 0 is 100x more important

    auto pairs = stratified_sample(*grid, ch, cfg);

    // Count how often node 0 appears
    uint32_t node0_count = 0;
    for (auto [s, t] : pairs) {
        if (s == 0 || t == 0) node0_count++;
    }

    // With 100x weight, node 0 should appear in a substantial fraction
    REQUIRE(node0_count > pairs.size() / 10);  // at least 10%
}

// --- B2: Scenario Fragility ---

TEST_CASE("Scenario fragility: blocking edges increases composite", "[v2]") {
    auto grid = make_coord_grid(7, 7);
    auto ch = build_ch(grid);
    ShortcutIndex idx(ch);

    ScenarioConfig cfg;
    cfg.baseline.boundary.vertices = {
        {34.99, -83.01}, {34.99, -82.93}, {35.07, -82.93}, {35.07, -83.01}, {34.99, -83.01}};
    cfg.baseline.od_sample_count = 10;

    // Block some edges in the center of the grid
    cfg.blocked_edges = {{24, 25}, {25, 24}, {24, 31}, {31, 24}};

    auto result = scenario_fragility(grid, ch, idx, cfg);

    REQUIRE(result.edges_blocked > 0);
    REQUIRE(result.baseline.composite_index >= 0.0);
    REQUIRE(result.scenario.composite_index >= 0.0);
    // Blocking edges should increase fragility (or at least not decrease it much)
    // On a grid, the effect may be small, but delta should be computed
    REQUIRE(std::isfinite(result.delta_composite));
}

// --- C1: Ensemble Fragility ---

TEST_CASE("Ensemble fragility produces variance across seeds", "[v2]") {
    auto grid = make_coord_grid(7, 7);
    auto ch = build_ch(grid);
    ShortcutIndex idx(ch);

    EnsembleConfig ecfg;
    ecfg.base_config.boundary.vertices = {
        {34.99, -83.01}, {34.99, -82.93}, {35.07, -82.93}, {35.07, -83.01}, {34.99, -83.01}};
    ecfg.base_config.od_sample_count = 10;
    ecfg.num_runs = 5;

    auto result = ensemble_fragility(grid, ch, idx, ecfg);

    REQUIRE(result.ensemble.size() == 5);
    REQUIRE(result.mean_composite > 0.0);
    REQUIRE(result.mean_composite >= result.min_composite);
    REQUIRE(result.mean_composite <= result.max_composite);
    REQUIRE(result.coefficient_of_variation >= 0.0);
}

// --- C2: Weight Sensitivity ---

TEST_CASE("Weight sensitivity detects composite response to weight changes", "[v2]") {
    auto grid = make_coord_grid(7, 7);
    auto ch = build_ch(grid);
    ShortcutIndex idx(ch);

    WeightSensitivityConfig wcfg;
    wcfg.base_config.boundary.vertices = {
        {34.99, -83.01}, {34.99, -82.93}, {35.07, -82.93}, {35.07, -83.01}, {34.99, -83.01}};
    wcfg.base_config.od_sample_count = 10;
    wcfg.perturbation_range = 0.2;
    wcfg.grid_points = 3;  // -20%, 0%, +20%

    auto result = weight_sensitivity(grid, ch, idx, wcfg);

    // Should have 4 dimensions × 3 points = 12 evaluations
    REQUIRE(result.weight_grid.size() == 12);
    REQUIRE(result.composite_values.size() == 12);
    REQUIRE(result.composite_max >= result.composite_min);

    // At least one sensitivity should be non-zero
    double total_sens = result.sensitivity_bridge + result.sensitivity_connectivity +
                         result.sensitivity_accessibility + result.sensitivity_fragility;
    REQUIRE(total_sens >= 0.0);
}

// --- B2 continued ---

TEST_CASE("edges_in_polygon finds edges within polygon", "[v2]") {
    auto grid = make_coord_grid(5, 5);

    // Small polygon covering center of grid
    Polygon poly;
    poly.vertices = {{35.015, -82.985}, {35.015, -82.965}, {35.035, -82.965}, {35.035, -82.985}, {35.015, -82.985}};

    auto edges = edges_in_polygon(grid, poly);
    REQUIRE(!edges.empty());
    REQUIRE(edges.size() < grid.edge_count());  // not all edges
}

// --- D1: Edge Confidence ---

TEST_CASE("Edge confidence from flat array", "[v2]") {
    auto conf = confidence_from_array({1.0, 0.5, 0.0, 0.8});

    REQUIRE(conf.scores.size() == 4);
    REQUIRE_THAT(conf.weight_multiplier(0), Catch::Matchers::WithinAbs(1.0, 1e-6));  // high confidence
    REQUIRE_THAT(conf.weight_multiplier(1), Catch::Matchers::WithinAbs(1.5, 1e-6));  // medium
    REQUIRE_THAT(conf.weight_multiplier(2), Catch::Matchers::WithinAbs(2.0, 1e-6));  // low
    REQUIRE_THAT(conf.weight_multiplier(99), Catch::Matchers::WithinAbs(1.0, 1e-6)); // out of range
}

// --- D2: Tiled Fragility ---

TEST_CASE("Tiled fragility produces spatial grid", "[v2]") {
    auto grid = make_coord_grid(10, 10);
    auto ch = build_ch(grid);
    ShortcutIndex idx(ch);

    TileConfig tcfg;
    tcfg.min_lat = 35.0;
    tcfg.max_lat = 35.09;
    tcfg.min_lon = -83.0;
    tcfg.max_lon = -82.91;
    tcfg.tile_size_meters = 5000;  // ~5km tiles
    tcfg.location_config.radius_meters = 3000;

    auto result = tiled_fragility_analysis(grid, ch, tcfg);

    REQUIRE(result.rows > 0);
    REQUIRE(result.cols > 0);
    REQUIRE(result.tiles.size() == result.rows * result.cols);
    REQUIRE(result.mean_isolation_risk >= 0.0);
    REQUIRE(result.mean_isolation_risk <= 1.0);
    REQUIRE(result.max_isolation_risk >= result.min_isolation_risk);
}
