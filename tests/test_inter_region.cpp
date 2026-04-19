#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/geo/geography_skeleton.h"
#include "gravel/fragility/inter_region_fragility.h"
#include "gravel/geo/region_assignment.h"
#include "gravel/geo/border_edges.h"
#include "gravel/ch/contraction.h"

using namespace gravel;

// --- Test fixture: 4-node graph with 2 regions ---
//
//   Node 0 ── 10 ── Node 1     Region A
//                │
//               20 (border edge)
//                │
//   Node 2 ── 10 ── Node 3     Region B
//

static std::vector<RegionSpec> two_regions_geo() {
    RegionSpec r1;
    r1.region_id = "A";
    r1.label = "Region A";
    r1.boundary.vertices = {{35.0, -84.0}, {35.0, -83.5}, {35.5, -83.5}, {35.5, -84.0}, {35.0, -84.0}};

    RegionSpec r2;
    r2.region_id = "B";
    r2.label = "Region B";
    r2.boundary.vertices = {{35.0, -83.5}, {35.0, -83.0}, {35.5, -83.0}, {35.5, -83.5}, {35.0, -83.5}};

    return {r1, r2};
}

static ArrayGraph two_region_graph_with_border() {
    std::vector<Coord> coords = {
        {35.25, -83.75},  // 0: in A
        {35.25, -83.60},  // 1: in A (closer to border)
        {35.25, -83.40},  // 2: in B (closer to border)
        {35.25, -83.25},  // 3: in B
    };
    std::vector<Edge> edges = {
        {0, 1, 10}, {1, 0, 10},
        {1, 2, 20}, {2, 1, 20},  // border edges
        {2, 3, 10}, {3, 2, 10},
    };
    ArrayGraph g(4, std::move(edges));
    return ArrayGraph(g.raw_offsets(), g.raw_targets(), g.raw_weights(), std::move(coords));
}

TEST_CASE("Geography skeleton: GEOMETRIC_CENTROID picks closest-to-centroid node", "[skeleton]") {
    auto graph = two_region_graph_with_border();
    auto regions = two_regions_geo();
    auto assignment = assign_nodes_to_regions(graph, regions);
    auto border = summarize_border_edges(graph, assignment);
    auto ch = build_ch(graph);

    ReducedGraphConfig cfg;
    cfg.method = ReducedGraphConfig::Centrality::GEOMETRIC_CENTROID;

    auto reduced = build_reduced_geography_graph(graph, ch, assignment, border, cfg);

    REQUIRE(reduced.valid());
    REQUIRE(reduced.central_of.count("A") == 1);
    REQUIRE(reduced.central_of.count("B") == 1);

    // A's centroid is at lat ~35.25, lon ~-83.75; node 0 is at (35.25, -83.75) → exact match
    NodeID central_a = reduced.central_of["A"];
    REQUIRE(reduced.reduced_to_original[central_a] == 0);
}

TEST_CASE("Geography skeleton: HIGHEST_DEGREE picks node with most edges", "[skeleton]") {
    auto graph = two_region_graph_with_border();
    auto regions = two_regions_geo();
    auto assignment = assign_nodes_to_regions(graph, regions);
    auto border = summarize_border_edges(graph, assignment);
    auto ch = build_ch(graph);

    ReducedGraphConfig cfg;
    cfg.method = ReducedGraphConfig::Centrality::HIGHEST_DEGREE;

    auto reduced = build_reduced_geography_graph(graph, ch, assignment, border, cfg);

    REQUIRE(reduced.valid());
    // Node 1 has degree 2 in region A (one to node 0, one to node 2 across border)
    // Node 0 has degree 1
    NodeID central_a = reduced.central_of["A"];
    REQUIRE(reduced.reduced_to_original[central_a] == 1);
}

TEST_CASE("Geography skeleton: PROVIDED uses caller's central node", "[skeleton]") {
    auto graph = two_region_graph_with_border();
    auto regions = two_regions_geo();
    auto assignment = assign_nodes_to_regions(graph, regions);
    auto border = summarize_border_edges(graph, assignment);
    auto ch = build_ch(graph);

    ReducedGraphConfig cfg;
    cfg.method = ReducedGraphConfig::Centrality::PROVIDED;
    cfg.precomputed_centrals = {0, 3};  // node 0 for A, node 3 for B

    auto reduced = build_reduced_geography_graph(graph, ch, assignment, border, cfg);

    REQUIRE(reduced.valid());
    REQUIRE(reduced.reduced_to_original[reduced.central_of["A"]] == 0);
    REQUIRE(reduced.reduced_to_original[reduced.central_of["B"]] == 3);
}

TEST_CASE("Geography skeleton: PROVIDED rejects wrong-region central", "[skeleton]") {
    auto graph = two_region_graph_with_border();
    auto regions = two_regions_geo();
    auto assignment = assign_nodes_to_regions(graph, regions);
    auto border = summarize_border_edges(graph, assignment);
    auto ch = build_ch(graph);

    ReducedGraphConfig cfg;
    cfg.method = ReducedGraphConfig::Centrality::PROVIDED;
    cfg.precomputed_centrals = {3, 0};  // wrong: node 3 isn't in A

    auto reduced = build_reduced_geography_graph(graph, ch, assignment, border, cfg);

    // Should silently skip invalid centrals (region A has no central → not in central_of)
    REQUIRE(reduced.central_of.count("A") == 0);
}

TEST_CASE("Geography skeleton: tracks inter-region edges by region pair", "[skeleton]") {
    auto graph = two_region_graph_with_border();
    auto regions = two_regions_geo();
    auto assignment = assign_nodes_to_regions(graph, regions);
    auto border = summarize_border_edges(graph, assignment);
    auto ch = build_ch(graph);

    auto reduced = build_reduced_geography_graph(graph, ch, assignment, border);

    // Region A is index 0, B is index 1 in assignment.regions
    RegionPair key{0, 1};
    REQUIRE(reduced.inter_region_edges.count(key) == 1);
    auto& pair_edges = reduced.inter_region_edges.at(key);
    REQUIRE(pair_edges.size() == 2);  // 1->2 and 2->1
}

TEST_CASE("Inter-region fragility: 2-region pair produces curve + AUC", "[inter_region]") {
    auto graph = two_region_graph_with_border();
    auto regions = two_regions_geo();
    auto assignment = assign_nodes_to_regions(graph, regions);
    auto border = summarize_border_edges(graph, assignment);
    auto ch = build_ch(graph);

    auto reduced = build_reduced_geography_graph(graph, ch, assignment, border);

    InterRegionFragilityConfig cfg;
    cfg.k_max = 2;  // we only have 2 inter-region edges
    cfg.monte_carlo_runs = 5;

    auto result = inter_region_fragility(reduced, cfg);

    REQUIRE(result.pairs.size() == 1);
    auto& p = result.pairs[0];

    REQUIRE(p.shared_border_edges == 2);
    REQUIRE(p.k_used == 2);
    REQUIRE(p.curve.size() == 3);  // k=0 (all blocked) through k=2 (fully restored)
    REQUIRE(p.baseline_seconds > 0);

    // At k=k_max (all edges restored), distance should equal baseline
    // At k=0 (all blocked), should be disconnected
    REQUIRE(p.curve.back().mean_seconds > 0);  // restored has finite distance
    REQUIRE(p.curve[0].disconnected_frac == 1.0);  // all blocked → all trials disconnected
    REQUIRE(p.auc_disconnection > 0);  // some level of disconnection
}

TEST_CASE("Inter-region fragility: skips non-adjacent pairs", "[inter_region]") {
    // 3 isolated nodes, each in their own region
    std::vector<Coord> coords = {
        {35.25, -83.75},
        {35.25, -83.40},
        {35.25, -83.10},
    };
    std::vector<Edge> edges = {};  // no edges = no border edges
    ArrayGraph g(3, std::move(edges));
    auto graph = ArrayGraph(g.raw_offsets(), g.raw_targets(), g.raw_weights(), std::move(coords));

    RegionSpec r1, r2, r3;
    r1.region_id = "A"; r1.boundary.vertices = {{35.0, -84.0}, {35.0, -83.6}, {35.5, -83.6}, {35.5, -84.0}, {35.0, -84.0}};
    r2.region_id = "B"; r2.boundary.vertices = {{35.0, -83.6}, {35.0, -83.3}, {35.5, -83.3}, {35.5, -83.6}, {35.0, -83.6}};
    r3.region_id = "C"; r3.boundary.vertices = {{35.0, -83.3}, {35.0, -83.0}, {35.5, -83.0}, {35.5, -83.3}, {35.0, -83.3}};
    std::vector<RegionSpec> regions = {r1, r2, r3};

    auto assignment = assign_nodes_to_regions(graph, regions);
    auto border = summarize_border_edges(graph, assignment);
    auto ch = build_ch(graph);

    auto reduced = build_reduced_geography_graph(graph, ch, assignment, border);

    InterRegionFragilityConfig cfg;
    cfg.k_max = 5;
    cfg.monte_carlo_runs = 3;
    auto result = inter_region_fragility(reduced, cfg);

    REQUIRE(result.pairs.size() == 0);  // no adjacent pairs (no border edges)
}
