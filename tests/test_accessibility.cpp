#include <catch2/catch_test_macros.hpp>
#include "gravel/analysis/accessibility.h"
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

TEST_CASE("Entry point identification on subgraph", "[accessibility]") {
    auto grid = make_coord_grid(5, 5);

    // Extract 3x3 interior
    Polygon rect;
    rect.vertices = {
        {35.005, -82.995}, {35.005, -82.965},
        {35.035, -82.965}, {35.035, -82.995},
        {35.005, -82.995}
    };
    auto sub = extract_subgraph(grid, rect);
    REQUIRE(sub.graph->node_count() == 9);

    // Build CH on full graph
    CHBuildConfig ch_config;
    auto ch = build_ch(grid, ch_config);
    ShortcutIndex idx(ch);

    auto result = analyze_accessibility(grid, ch, idx, sub);

    // All 9 interior nodes border external nodes (they're all on the
    // boundary of the 3x3 subgraph within the 5x5 grid)
    // Actually: the 3x3 interior has 8 boundary nodes and 1 center node.
    // The center node (2,2) only connects to interior nodes - no entry point.
    // The 8 boundary nodes of the 3x3 each have at least one external neighbor.
    REQUIRE(result.entry_points.size() == 8);

    // Each entry point should have external neighbors
    for (const auto& ep : result.entry_points) {
        REQUIRE(!ep.external_neighbors.empty());
        REQUIRE(ep.original_id != INVALID_NODE);
        REQUIRE(ep.subgraph_id != INVALID_NODE);
    }
}
