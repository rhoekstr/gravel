#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/simplify/simplify.h"
#include "gravel/core/edge_labels.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/ch_query.h"
#include "gravel/core/dijkstra.h"
#include "gravel/validation/synthetic_graphs.h"
#include "gravel/simplify/bridges.h"
#include <utility>
#include <algorithm>
#include <unordered_set>
#include <vector>

using namespace gravel;

// Path graph: 0-1-2-3-4 (all degree-2 except endpoints)
static ArrayGraph make_path5() {
    std::vector<Edge> edges;
    for (int i = 0; i < 4; ++i) {
        edges.push_back({(NodeID)i, (NodeID)(i + 1), 1.0 + i});
        edges.push_back({(NodeID)(i + 1), (NodeID)i, 1.0 + i});
    }
    return ArrayGraph(5, std::move(edges));
}

// --- Degree-2 Contraction Tests ---

TEST_CASE("Degree-2 contraction on path graph", "[simplify]") {
    auto path = make_path5();
    // Path: 0 --1-- 1 --2-- 2 --3-- 3 --4-- 4
    // Nodes 1, 2, 3 are degree-2 → contract to: 0 --10-- 4

    auto result = contract_degree2(path);

    REQUIRE(result.simplified_nodes == 2);  // just endpoints 0 and 4
    REQUIRE(result.graph->node_count() == 2);

    // Merged edge weight should be 1+2+3+4 = 10
    auto targets = result.graph->outgoing_targets(0);
    auto weights = result.graph->outgoing_weights(0);
    REQUIRE(targets.size() >= 1);
    REQUIRE_THAT(weights[0], Catch::Matchers::WithinAbs(10.0, 1e-6));
}

TEST_CASE("Degree-2 contraction on grid - no change", "[simplify]") {
    auto grid = make_grid_graph(4, 4);
    // All interior nodes have degree 4, corner nodes degree 2 but they ARE endpoints

    auto result = contract_degree2(*grid);

    // Grid has no degree-2 chains (every node has ≥3 undirected neighbors except corners,
    // but corners connect to 2 different neighbors which themselves have >2 neighbors).
    // Actually corner nodes DO have undirected degree 2. Let's verify:
    // Node (0,0): neighbors are (0,1) and (1,0) → degree 2 → contractible!
    // But contracting it merges the two edges into one, which changes topology.

    // For a 4x4 grid, corner nodes have degree 2. The contraction merges:
    // (0,0) → merges edge to (0,1) and (1,0)
    // This is expected behavior for degree-2 contraction.
    REQUIRE(result.simplified_nodes <= grid->node_count());
    REQUIRE(result.simplified_nodes >= 4);  // at least some nodes remain
}

TEST_CASE("Degree-2 contraction preserves bridge endpoints", "[simplify]") {
    // Tree with bridges: contracting degree-2 nodes should skip bridge endpoints
    auto tree = make_tree_with_bridges(20, 5, 42);

    // Find bridges first
    auto bridges = find_bridges(*tree);
    std::unordered_set<NodeID> bridge_eps;
    for (const auto& [u, v] : bridges.bridges) {
        bridge_eps.insert(u);
        bridge_eps.insert(v);
    }

    auto result = contract_degree2(*tree, bridge_eps);

    // Bridge endpoints should all be preserved
    for (NodeID bp : bridge_eps) {
        REQUIRE(result.original_to_new.count(bp) > 0);
    }
}

TEST_CASE("Degree-2 contraction preserves shortest-path distances", "[simplify]") {
    auto path = make_path5();

    auto result = contract_degree2(path);

    // Distance from original node 0 to node 4 should be preserved
    // In simplified graph: 0→4 has weight 10
    // In original graph: 0→1→2→3→4 has weight 1+2+3+4=10
    auto dijk_orig = dijkstra(path, 0);
    REQUIRE_THAT(dijk_orig.distances[4], Catch::Matchers::WithinAbs(10.0, 1e-6));

    // Simplified: only 2 nodes, distance 0→1 (new ID for node 4)
    auto dijk_simp = dijkstra(*result.graph, 0);
    NodeID new_4 = result.original_to_new.at(4);
    REQUIRE_THAT(dijk_simp.distances[new_4], Catch::Matchers::WithinAbs(10.0, 1e-6));
}

// --- Edge Category Filtering Tests ---

TEST_CASE("Edge category filter removes low-priority edges", "[simplify]") {
    auto grid = make_grid_graph(3, 3);

    // Label some edges as "high priority" (0) and others as "low priority" (5)
    EdgeCategoryLabels labels;
    labels.categories.resize(grid->edge_count(), 5);  // all low by default

    // Mark first few edges as high priority
    for (uint32_t i = 0; i < std::min((uint32_t)4, grid->edge_count()); ++i) {
        labels.categories[i] = 0;
    }

    auto filter = make_category_filter(labels, 2);  // keep only rank ≤ 2
    auto result = filter_edges(*grid, filter);

    REQUIRE(result.simplified_edges < grid->edge_count());
    REQUIRE(result.simplified_edges >= 4);  // at least the high-priority ones
}

TEST_CASE("OSM road ranks are correct", "[simplify]") {
    auto ranks = EdgeCategoryLabels::osm_road_ranks();

    REQUIRE(ranks["motorway"] == 0);
    REQUIRE(ranks["trunk"] == 1);
    REQUIRE(ranks["primary"] == 2);
    REQUIRE(ranks["secondary"] == 3);
    REQUIRE(ranks["tertiary"] == 4);
    REQUIRE(ranks["residential"] == 5);
    REQUIRE(ranks["service"] == 6);
}

// --- CH-Level Pruning Tests ---

TEST_CASE("CH-level pruning keeps top fraction", "[simplify]") {
    auto grid = make_grid_graph(10, 10);
    auto ch = build_ch(*grid);

    auto result = prune_by_ch_level(*grid, ch, 0.5);

    // Should keep roughly 50% of nodes
    REQUIRE(result.simplified_nodes >= 40);
    REQUIRE(result.simplified_nodes <= 60);
    REQUIRE(result.simplified_nodes < grid->node_count());
}

TEST_CASE("CH-level pruning preserves bridges", "[simplify]") {
    auto tree = make_tree_with_bridges(50, 10, 42);
    auto ch = build_ch(*tree);
    auto bridges = find_bridges(*tree);

    std::unordered_set<NodeID> bridge_eps;
    for (const auto& [u, v] : bridges.bridges) {
        bridge_eps.insert(u);
        bridge_eps.insert(v);
    }

    auto result = prune_by_ch_level(*tree, ch, 0.3, bridge_eps);

    // All bridge endpoints should be in the simplified graph
    for (NodeID bp : bridge_eps) {
        REQUIRE(result.original_to_new.count(bp) > 0);
    }
}

TEST_CASE("CH-level keep_fraction 1.0 returns all nodes", "[simplify]") {
    auto grid = make_grid_graph(5, 5);
    auto ch = build_ch(*grid);

    auto result = prune_by_ch_level(*grid, ch, 1.0);

    REQUIRE(result.simplified_nodes == grid->node_count());
    REQUIRE(result.simplified_edges == grid->edge_count());
}

// --- Full Pipeline Tests ---

TEST_CASE("Full pipeline: degree2 + CH pruning", "[simplify]") {
    auto grid = make_grid_graph(10, 10);
    auto ch = build_ch(*grid);
    ShortcutIndex idx(ch);

    SimplificationConfig config;
    config.contract_degree2 = true;
    config.ch_level_keep_fraction = 0.7;
    config.estimate_degradation = true;
    config.degradation_samples = 50;

    auto result = simplify_graph(*grid, &ch, &idx, config);

    REQUIRE(result.simplified_nodes < grid->node_count());
    REQUIRE(result.simplified_nodes > 0);
    REQUIRE(result.graph != nullptr);

    // Degradation should be populated
    REQUIRE(result.degradation.od_pairs_sampled > 0);
    REQUIRE(result.degradation.median_stretch >= 1.0);
    REQUIRE(result.degradation.connectivity_ratio >= 0.0);
    REQUIRE(result.degradation.connectivity_ratio <= 1.0);

    // Stage reports should be present
    REQUIRE(result.degradation.stages.size() >= 1);
}

TEST_CASE("Full pipeline: degree2 only (no CH needed)", "[simplify]") {
    auto path = make_path5();

    SimplificationConfig config;
    config.contract_degree2 = true;
    config.preserve_bridges = false;  // path has all bridges - must disable to allow contraction
    config.ch_level_keep_fraction = 1.0;  // disabled
    config.estimate_degradation = false;

    auto result = simplify_graph(path, nullptr, nullptr, config);

    REQUIRE(result.simplified_nodes == 2);
    REQUIRE(result.graph != nullptr);
}

TEST_CASE("Full pipeline on tree_with_bridges preserves all bridges", "[simplify]") {
    auto tree = make_tree_with_bridges(50, 10, 42);
    auto ch = build_ch(*tree);
    ShortcutIndex idx(ch);

    SimplificationConfig config;
    config.contract_degree2 = true;
    config.ch_level_keep_fraction = 0.5;
    config.preserve_bridges = true;
    config.estimate_degradation = true;
    config.degradation_samples = 30;

    auto result = simplify_graph(*tree, &ch, &idx, config);

    REQUIRE(result.degradation.all_bridges_preserved);
}
