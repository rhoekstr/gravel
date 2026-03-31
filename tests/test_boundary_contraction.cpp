#include <catch2/catch_test_macros.hpp>
#include "gravel/simplify/simplify.h"
#include "gravel/geo/boundary_nodes.h"
#include "gravel/geo/region_assignment.h"

using namespace gravel;

// Linear chain: 0 -- 1 -- 2 -- 3 -- 4 (all degree 2 except endpoints)
// Nodes 0,1 in Region A; nodes 2,3,4 in Region B
// Node 2 is a boundary node (neighbor 1 is in Region A)
static ArrayGraph make_chain_graph() {
    std::vector<Coord> coords = {
        {35.0, -84.0},   // 0: Region A
        {35.0, -83.75},  // 1: Region A
        {35.0, -83.50},  // 2: Region B (boundary)
        {35.0, -83.25},  // 3: Region B
        {35.0, -83.00},  // 4: Region B
    };
    std::vector<Edge> edges;
    for (int i = 0; i < 4; ++i) {
        edges.push_back({static_cast<NodeID>(i), static_cast<NodeID>(i+1), 10});
        edges.push_back({static_cast<NodeID>(i+1), static_cast<NodeID>(i), 10});
    }
    ArrayGraph g(5, std::move(edges));
    return ArrayGraph(g.raw_offsets(), g.raw_targets(), g.raw_weights(), std::move(coords));
}

static std::vector<RegionSpec> make_ab_regions() {
    RegionSpec a;
    a.region_id = "A"; a.label = "Region A";
    a.boundary.vertices = {{34.5, -84.5}, {34.5, -83.6}, {35.5, -83.6}, {35.5, -84.5}, {34.5, -84.5}};

    RegionSpec b;
    b.region_id = "B"; b.label = "Region B";
    b.boundary.vertices = {{34.5, -83.6}, {34.5, -82.5}, {35.5, -82.5}, {35.5, -83.6}, {34.5, -83.6}};

    return {a, b};
}

TEST_CASE("boundary_nodes identifies cross-region nodes", "[boundary_contraction]") {
    auto graph = make_chain_graph();
    auto regions = make_ab_regions();
    auto assignment = assign_nodes_to_regions(graph, regions);

    auto bnodes = boundary_nodes(graph, assignment);

    // Nodes 1 and 2 are boundary nodes (edge 1-2 crosses regions)
    REQUIRE(bnodes.count(1));
    REQUIRE(bnodes.count(2));

    // Nodes 0, 3, 4 are interior nodes
    REQUIRE(!bnodes.count(0));
    REQUIRE(!bnodes.count(3));
    REQUIRE(!bnodes.count(4));
}

TEST_CASE("contract_degree2 without boundary protection removes chain nodes", "[boundary_contraction]") {
    auto graph = make_chain_graph();

    auto result = contract_degree2(graph);

    // Without protection, nodes 1,2,3 are all degree-2 and contractible
    // Result should have just endpoints 0 and 4
    REQUIRE(result.simplified_nodes == 2);
}

TEST_CASE("contract_degree2 with boundary protection preserves border nodes", "[boundary_contraction]") {
    auto graph = make_chain_graph();
    auto regions = make_ab_regions();
    auto assignment = assign_nodes_to_regions(graph, regions);

    auto protection = boundary_nodes(graph, assignment);

    // Protect boundary nodes (1 and 2)
    auto result = contract_degree2(graph, {}, protection);

    // Nodes 1 and 2 are protected. Node 3 is contractible (degree-2, interior to B).
    // Result should have: 0, 1, 2, 4 (node 3 contracted into edge 2→4)
    REQUIRE(result.simplified_nodes == 4);

    // Verify boundary nodes are preserved
    bool has_node_1 = false, has_node_2 = false;
    for (NodeID orig : result.new_to_original) {
        if (orig == 1) has_node_1 = true;
        if (orig == 2) has_node_2 = true;
    }
    REQUIRE(has_node_1);
    REQUIRE(has_node_2);
}
