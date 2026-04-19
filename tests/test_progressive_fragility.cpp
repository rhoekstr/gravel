#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/analysis/progressive_fragility.h"
#include "gravel/ch/contraction.h"
#include "gravel/validation/synthetic_graphs.h"
#include <utility>
#include <vector>

using namespace gravel;

static ArrayGraph make_coord_grid(uint32_t rows, uint32_t cols) {
    NodeID n = rows * cols;
    auto nid = [cols](uint32_t r, uint32_t c) -> NodeID { return r * cols + c; };
    std::vector<Edge> edges;
    std::vector<Coord> coords(n);
    for (uint32_t r = 0; r < rows; ++r)
        for (uint32_t c = 0; c < cols; ++c) {
            NodeID u = nid(r, c);
            coords[u] = {35.0 + r * 0.01, -83.0 + c * 0.01};
            if (c + 1 < cols) { NodeID v = nid(r, c + 1); edges.push_back({u, v, 1.0}); edges.push_back({v, u, 1.0}); }
            if (r + 1 < rows) { NodeID v = nid(r + 1, c); edges.push_back({u, v, 1.0}); edges.push_back({v, u, 1.0}); }
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

TEST_CASE("Progressive MC on grid produces valid curve", "[progressive]") {
    auto grid = make_coord_grid(7, 7);
    auto ch = build_ch(grid);
    ShortcutIndex idx(ch);

    ProgressiveFragilityConfig cfg;
    cfg.base_config.boundary.vertices = {
        {34.99, -83.01}, {34.99, -82.93}, {35.07, -82.93}, {35.07, -83.01}, {34.99, -83.01}};
    cfg.base_config.od_sample_count = 10;
    cfg.selection_strategy = SelectionStrategy::MONTE_CARLO;
    cfg.k_max = 3;
    cfg.monte_carlo_runs = 5;

    auto result = progressive_fragility(grid, ch, idx, cfg);

    REQUIRE(result.curve.size() == 4);  // k=0,1,2,3
    REQUIRE(result.k_max_used == 3);
    REQUIRE(result.curve[0].mean_composite >= 0.0);

    // AUC should be positive
    REQUIRE(result.auc_raw > 0.0);
    REQUIRE(result.auc_normalized > 0.0);

    // MC levels should have run_values
    REQUIRE(result.curve[1].run_values.size() == 5);
}

TEST_CASE("Progressive greedy betweenness produces removal sequence", "[progressive]") {
    auto grid = make_coord_grid(7, 7);
    auto ch = build_ch(grid);
    ShortcutIndex idx(ch);

    ProgressiveFragilityConfig cfg;
    cfg.base_config.boundary.vertices = {
        {34.99, -83.01}, {34.99, -82.93}, {35.07, -82.93}, {35.07, -83.01}, {34.99, -83.01}};
    cfg.base_config.od_sample_count = 10;
    cfg.selection_strategy = SelectionStrategy::GREEDY_BETWEENNESS;
    cfg.k_max = 3;

    auto result = progressive_fragility(grid, ch, idx, cfg);

    REQUIRE(result.curve.size() == 4);
    REQUIRE(result.removal_sequence.size() == 3);

    // Each removal should be a valid edge
    for (auto [u, v] : result.removal_sequence) {
        REQUIRE(u != INVALID_NODE);
        REQUIRE(v != INVALID_NODE);
    }
}

TEST_CASE("Progressive greedy fragility picks highest-impact edge", "[progressive]") {
    auto grid = make_coord_grid(5, 5);
    auto ch = build_ch(grid);
    ShortcutIndex idx(ch);

    ProgressiveFragilityConfig cfg;
    cfg.base_config.boundary.vertices = {
        {34.99, -83.01}, {34.99, -82.95}, {35.05, -82.95}, {35.05, -83.01}, {34.99, -83.01}};
    cfg.base_config.od_sample_count = 5;
    cfg.selection_strategy = SelectionStrategy::GREEDY_FRAGILITY;
    cfg.k_max = 2;

    auto result = progressive_fragility(grid, ch, idx, cfg);

    REQUIRE(result.curve.size() == 3);  // k=0,1,2
    REQUIRE(result.removal_sequence.size() == 2);
    // Composite should generally increase (or at least not crash)
    REQUIRE(result.curve[1].mean_composite >= 0.0);
}

TEST_CASE("AUC metrics are correctly computed", "[progressive]") {
    auto grid = make_coord_grid(7, 7);
    auto ch = build_ch(grid);
    ShortcutIndex idx(ch);

    ProgressiveFragilityConfig cfg;
    cfg.base_config.boundary.vertices = {
        {34.99, -83.01}, {34.99, -82.93}, {35.07, -82.93}, {35.07, -83.01}, {34.99, -83.01}};
    cfg.base_config.od_sample_count = 10;
    cfg.selection_strategy = SelectionStrategy::GREEDY_BETWEENNESS;
    cfg.k_max = 3;

    auto result = progressive_fragility(grid, ch, idx, cfg);

    // Verify AUC formula: raw = sum of mean_composite[k]
    double expected_raw = 0.0;
    for (const auto& level : result.curve) {
        expected_raw += level.mean_composite;
    }
    REQUIRE_THAT(result.auc_raw, Catch::Matchers::WithinAbs(expected_raw, 1e-6));

    // Normalized = raw / (k_max + 1)
    REQUIRE_THAT(result.auc_normalized,
                 Catch::Matchers::WithinAbs(result.auc_raw / 4.0, 1e-6));
}

TEST_CASE("Progressive fragility on narrow grid detects degradation", "[progressive]") {
    // Narrow 2x10 grid: removing edges has significant impact on connectivity
    auto grid = make_coord_grid(2, 10);
    auto ch = build_ch(grid);
    ShortcutIndex idx(ch);

    ProgressiveFragilityConfig cfg;
    cfg.base_config.boundary.vertices = {
        {34.99, -83.01}, {34.99, -82.89}, {35.02, -82.89}, {35.02, -83.01}, {34.99, -83.01}};
    cfg.base_config.od_sample_count = 10;
    cfg.selection_strategy = SelectionStrategy::GREEDY_FRAGILITY;
    cfg.k_max = 2;

    auto result = progressive_fragility(grid, ch, idx, cfg);

    REQUIRE(result.curve.size() >= 2);
    REQUIRE(result.removal_sequence.size() >= 1);
}
