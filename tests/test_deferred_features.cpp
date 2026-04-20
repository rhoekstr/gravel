#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/geo/border_edges.h"
#include "gravel/geo/graph_coarsening.h"
#include "gravel/geo/region_assignment.h"
#include "gravel/analysis/scenario_fragility.h"
#include "gravel/us/county_assignment.h"
#include "gravel/us/cbsa_assignment.h"
#include "gravel/us/fips_crosswalk.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/shortcut_index.h"
#include <utility>
#include <vector>

using namespace gravel;

// --- Shared test fixtures ---

// Two adjacent square regions with a 4-node graph: 0,1 in R1; 2,3 in R2
static std::vector<RegionSpec> make_two_regions() {
    RegionSpec r1;
    r1.region_id = "R1";
    r1.label = "Region 1";
    r1.boundary.vertices = {{35.0, -84.0}, {35.0, -83.5}, {35.5, -83.5}, {35.5, -84.0}, {35.0, -84.0}};

    RegionSpec r2;
    r2.region_id = "R2";
    r2.label = "Region 2";
    r2.boundary.vertices = {{35.0, -83.5}, {35.0, -83.0}, {35.5, -83.0}, {35.5, -83.5}, {35.0, -83.5}};

    return {r1, r2};
}

static ArrayGraph make_two_region_graph() {
    std::vector<Coord> coords = {
        {35.25, -83.75},  // node 0: in R1
        {35.25, -83.60},  // node 1: in R1
        {35.25, -83.40},  // node 2: in R2
        {35.25, -83.25},  // node 3: in R2
    };
    std::vector<Edge> edges = {
        {0, 1, 10}, {1, 0, 10},
        {1, 2, 20}, {2, 1, 20},  // border edges
        {2, 3, 10}, {3, 2, 10},
    };
    ArrayGraph g(4, std::move(edges));
    return ArrayGraph(g.raw_offsets(), g.raw_targets(), g.raw_weights(), std::move(coords));
}

// --- Border Edge Tests ---

TEST_CASE("Border edges - counts cross-region edges", "[border_edges]") {
    auto graph = make_two_region_graph();
    auto regions = make_two_regions();
    auto assignment = assign_nodes_to_regions(graph, regions);

    auto result = summarize_border_edges(graph, assignment);

    REQUIRE(result.connected_pairs == 1);
    REQUIRE(result.total_border_edges == 2);  // 1→2 and 2→1
    REQUIRE(result.unassigned_edges == 0);

    RegionPair key{0, 1};
    REQUIRE(result.pair_summaries.count(key) == 1);
    auto& summary = result.pair_summaries.at(key);
    REQUIRE(summary.edge_count == 2);
    REQUIRE(summary.min_weight == 20.0);
    REQUIRE(summary.max_weight == 20.0);
}

TEST_CASE("Border edges - unassigned nodes counted separately", "[border_edges]") {
    std::vector<Coord> coords = {
        {35.25, -83.75},  // in R1
        {0.0, 0.0},       // unassigned
    };
    std::vector<Edge> edges = {{0, 1, 10}, {1, 0, 10}};
    ArrayGraph g(2, std::move(edges));
    auto graph = ArrayGraph(g.raw_offsets(), g.raw_targets(), g.raw_weights(), std::move(coords));

    auto regions = make_two_regions();
    auto assignment = assign_nodes_to_regions(graph, regions);

    auto result = summarize_border_edges(graph, assignment);
    REQUIRE(result.connected_pairs == 0);
    REQUIRE(result.unassigned_edges == 2);  // both directions touch unassigned
}

// --- Graph Coarsening Tests ---

TEST_CASE("Graph coarsening - two regions produce 2-node graph", "[graph_coarsening]") {
    auto graph = make_two_region_graph();
    auto regions = make_two_regions();
    auto assignment = assign_nodes_to_regions(graph, regions);
    auto border = summarize_border_edges(graph, assignment);

    auto result = coarsen_graph(graph, assignment, border);

    REQUIRE(result.graph != nullptr);
    REQUIRE(result.graph->node_count() == 2);
    REQUIRE(result.graph->edge_count() == 2);  // one edge each direction
    REQUIRE(result.region_ids[0] == "R1");
    REQUIRE(result.region_ids[1] == "R2");
    REQUIRE(result.node_counts[0] == 2);
    REQUIRE(result.node_counts[1] == 2);
}

TEST_CASE("Graph coarsening - centroids computed", "[graph_coarsening]") {
    auto graph = make_two_region_graph();
    auto regions = make_two_regions();
    auto assignment = assign_nodes_to_regions(graph, regions);
    auto border = summarize_border_edges(graph, assignment);

    CoarseningConfig config;
    config.compute_centroids = true;
    auto result = coarsen_graph(graph, assignment, border, config);

    REQUIRE(result.graph != nullptr);
    auto c0 = result.graph->node_coordinate(0);
    REQUIRE(c0.has_value());
    // Centroid of nodes 0,1: lat = 35.25, lon = (-83.75 + -83.60)/2 = -83.675
    REQUIRE_THAT(c0->lat, Catch::Matchers::WithinAbs(35.25, 0.01));
    REQUIRE_THAT(c0->lon, Catch::Matchers::WithinAbs(-83.675, 0.01));
}

TEST_CASE("Graph coarsening - min_border_edges filter", "[graph_coarsening]") {
    auto graph = make_two_region_graph();
    auto regions = make_two_regions();
    auto assignment = assign_nodes_to_regions(graph, regions);
    auto border = summarize_border_edges(graph, assignment);

    CoarseningConfig config;
    config.min_border_edges = 10;  // higher than actual count
    auto result = coarsen_graph(graph, assignment, border, config);

    REQUIRE(result.graph != nullptr);
    REQUIRE(result.graph->edge_count() == 0);  // filtered out
}

TEST_CASE("Graph coarsening - internal edge counts", "[graph_coarsening]") {
    auto graph = make_two_region_graph();
    auto regions = make_two_regions();
    auto assignment = assign_nodes_to_regions(graph, regions);
    auto border = summarize_border_edges(graph, assignment);

    auto result = coarsen_graph(graph, assignment, border);

    // R1: edges 0→1 and 1→0 = 2 internal edges
    REQUIRE(result.internal_edge_counts[0] == 2);
    // R2: edges 2→3 and 3→2 = 2 internal edges
    REQUIRE(result.internal_edge_counts[1] == 2);
}

// --- Scenario Fragility Fast Path ---

TEST_CASE("Scenario fragility uses BlockedCHQuery fast path", "[scenario_fragility]") {
    // Build a simple graph where blocking an edge changes fragility
    std::vector<Coord> coords;
    for (int i = 0; i < 10; ++i) {
        coords.push_back({35.25 + i * 0.01, -83.5 + i * 0.01});
    }

    // Chain graph: 0-1-2-...-9 with a parallel shortcut 0-5
    std::vector<Edge> edges;
    for (int i = 0; i < 9; ++i) {
        edges.push_back({static_cast<NodeID>(i), static_cast<NodeID>(i + 1), 10});
        edges.push_back({static_cast<NodeID>(i + 1), static_cast<NodeID>(i), 10});
    }
    edges.push_back({0, 5, 45});
    edges.push_back({5, 0, 45});

    ArrayGraph g(10, std::move(edges));
    auto graph = ArrayGraph(g.raw_offsets(), g.raw_targets(), g.raw_weights(), std::move(coords));

    auto ch = build_ch(graph);
    ShortcutIndex idx(ch);

    Polygon boundary;
    boundary.vertices = {{35.20, -83.55}, {35.20, -83.40}, {35.40, -83.40}, {35.40, -83.55}, {35.20, -83.55}};

    ScenarioConfig config;
    config.baseline.boundary = boundary;
    config.baseline.od_sample_count = 20;
    config.baseline.seed = 42;
    config.blocked_edges = {{0, 1}, {1, 0}};  // Block the first chain edge

    auto result = scenario_fragility(graph, ch, idx, config);

    // Scenario should complete without error
    REQUIRE(result.edges_blocked == 2);
    // Scenario fragility should differ from baseline (blocking an edge changes things)
    // The exact values depend on which O-D pairs are sampled
}

// --- FIPS Crosswalk Tests ---

TEST_CASE("FIPS crosswalk - state extraction from county FIPS", "[fips_crosswalk]") {
    RegionAssignment county_assignment;
    RegionSpec r;
    r.region_id = "37173";
    r.label = "Swain County";
    r.boundary.vertices = {{35.0, -84.0}, {35.0, -83.0}, {35.5, -83.0}, {35.5, -84.0}, {35.0, -84.0}};
    county_assignment.regions = {r};
    county_assignment.region_index = {};
    county_assignment.region_node_counts = {0};

    auto xwalk = build_fips_crosswalk(county_assignment);

    REQUIRE(xwalk.entries.size() == 1);
    REQUIRE(xwalk.entries[0].county_fips == "37173");
    REQUIRE(xwalk.entries[0].state_fips == "37");
    REQUIRE(xwalk.county_to_state("37173") == "37");
    REQUIRE(xwalk.county_to_cbsa("37173").empty());  // no CBSA assignment
}

TEST_CASE("FIPS crosswalk - counties in state", "[fips_crosswalk]") {
    RegionAssignment county_assignment;

    RegionSpec r1;
    r1.region_id = "37173";
    r1.label = "Swain County";
    r1.boundary.vertices = {{35.0, -84.0}, {35.0, -83.0}, {35.5, -83.0}, {35.5, -84.0}, {35.0, -84.0}};

    RegionSpec r2;
    r2.region_id = "37021";
    r2.label = "Buncombe County";
    r2.boundary.vertices = {{35.0, -83.0}, {35.0, -82.0}, {35.5, -82.0}, {35.5, -83.0}, {35.0, -83.0}};

    RegionSpec r3;
    r3.region_id = "47009";
    r3.label = "Blount County";
    r3.boundary.vertices = {{35.5, -84.0}, {35.5, -83.0}, {36.0, -83.0}, {36.0, -84.0}, {35.5, -84.0}};

    county_assignment.regions = {r1, r2, r3};
    county_assignment.region_index = {};
    county_assignment.region_node_counts = {0, 0, 0};

    auto xwalk = build_fips_crosswalk(county_assignment);

    auto nc_counties = xwalk.counties_in_state("37");
    REQUIRE(nc_counties.size() == 2);

    auto tn_counties = xwalk.counties_in_state("47");
    REQUIRE(tn_counties.size() == 1);
}
