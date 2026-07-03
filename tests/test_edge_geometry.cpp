#include <catch2/catch_test_macros.hpp>
#include "gravel/core/edge_geometry.h"
#include "gravel/simplify/simplify.h"

#include <vector>

using namespace gravel;

// Bidirectional path 0-1-2-3-4 with distinct coordinates. Nodes 1,2,3 are
// degree-2 (contractible); 0 and 4 are the surviving junctions.
static ArrayGraph make_path5_with_coords() {
    std::vector<uint32_t> offsets = {0, 1, 3, 5, 7, 8};
    std::vector<NodeID> targets = {1, 0, 2, 1, 3, 2, 4, 3};
    std::vector<Weight> weights(targets.size(), 1.0);
    std::vector<Coord> coords = {
        {0.0, 0.0}, {0.0, 1.0}, {0.0, 2.0}, {0.0, 3.0}, {0.0, 4.0}};
    return ArrayGraph(std::move(offsets), std::move(targets),
                      std::move(weights), std::move(coords));
}

// Star: center 0 (degree 3) with leaves 1,2,3 (degree 1). Nothing contracts,
// so every edge is a direct junction-to-junction segment.
static ArrayGraph make_star_with_coords() {
    std::vector<uint32_t> offsets = {0, 3, 4, 5, 6};
    std::vector<NodeID> targets = {1, 2, 3, 0, 0, 0};
    std::vector<Weight> weights(targets.size(), 1.0);
    std::vector<Coord> coords = {
        {0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}, {-1.0, 0.0}};
    return ArrayGraph(std::move(offsets), std::move(targets),
                      std::move(weights), std::move(coords));
}

TEST_CASE("emit_geometry off leaves geometry empty", "[geometry]") {
    auto path = make_path5_with_coords();
    auto result = contract_degree2(path, {}, {}, /*emit_geometry=*/false);
    REQUIRE(result.edge_geometry.empty());
    REQUIRE(result.edge_geometry.edge_count() == 0);
}

TEST_CASE("degree-2 chain collapses to a real polyline", "[geometry]") {
    auto path = make_path5_with_coords();
    auto result = contract_degree2(path, {}, {}, /*emit_geometry=*/true);

    const auto& g = *result.graph;
    const auto& geom = result.edge_geometry;

    // Two surviving junctions (0, 4), two directed merged edges between them.
    REQUIRE(g.node_count() == 2);
    REQUIRE(g.edge_count() == 2);

    // Geometry is CSR-aligned to the edge set.
    REQUIRE(geom.edge_count() == g.edge_count());
    REQUIRE(geom.offsets.size() == g.edge_count() + 1);
    REQUIRE(geom.offsets.front() == 0);
    REQUIRE(geom.offsets.back() == geom.points.size());

    // The whole chain is preserved: 5 points per direction, 10 total.
    REQUIRE(geom.points.size() == 10);

    // Each edge's polyline starts at its source coord and ends at its target
    // coord, and both edges carry the full 5-point chain.
    auto off = g.raw_offsets();
    auto tg = g.raw_targets();
    for (NodeID u = 0; u < g.node_count(); ++u) {
        for (uint32_t e = off[u]; e < off[u + 1]; ++e) {
            NodeID v = tg[e];
            auto pts = geom.points_for(e);
            REQUIRE(pts.size() == 5);
            Coord src = g.node_coordinate(u).value();
            Coord dst = g.node_coordinate(v).value();
            REQUIRE(pts.front().lat == src.lat);
            REQUIRE(pts.front().lon == src.lon);
            REQUIRE(pts.back().lat == dst.lat);
            REQUIRE(pts.back().lon == dst.lon);
        }
    }

    // The two directed edges are reverses of each other.
    auto a = geom.points_for(0);
    auto b = geom.points_for(1);
    REQUIRE(a.size() == b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].lon == b[b.size() - 1 - i].lon);
    }
}

TEST_CASE("non-chain edges get 2-point geometry", "[geometry]") {
    auto star = make_star_with_coords();
    auto result = contract_degree2(star, {}, {}, /*emit_geometry=*/true);

    const auto& g = *result.graph;
    const auto& geom = result.edge_geometry;

    REQUIRE(g.node_count() == 4);          // nothing contracts
    REQUIRE(geom.edge_count() == g.edge_count());

    auto off = g.raw_offsets();
    auto tg = g.raw_targets();
    for (NodeID u = 0; u < g.node_count(); ++u) {
        for (uint32_t e = off[u]; e < off[u + 1]; ++e) {
            NodeID v = tg[e];
            auto pts = geom.points_for(e);
            REQUIRE(pts.size() == 2);       // straight segment
            REQUIRE(pts.front().lat == g.node_coordinate(u).value().lat);
            REQUIRE(pts.back().lon == g.node_coordinate(v).value().lon);
        }
    }
}

TEST_CASE("one-way degree-2 chain emits no phantom reverse edge", "[geometry][simplify]") {
    // Theta graph, junctions 0 & 1. The chain through mid 2 is ONE-WAY (1->2->0); the chains
    // through mids 3 and 4 are bidirectional. Weight is used as an existence proxy nowhere:
    // the one-way chain must contribute only its real direction, with no zero-weight phantom.
    std::vector<uint32_t> offsets = {0, 2, 5, 6, 8, 10};
    std::vector<NodeID> targets = {3, 4, /*1*/ 2, 3, 4, /*2*/ 0, /*3*/ 0, 1, /*4*/ 0, 1};
    std::vector<Weight> weights(targets.size(), 1.0);
    std::vector<Coord> coords = {{0, 0}, {2, 0}, {1, 0}, {1, 1}, {1, -1}};
    ArrayGraph g(std::move(offsets), std::move(targets), std::move(weights), std::move(coords));

    auto res = contract_degree2(g);  // emit_geometry defaults on
    const auto& s = *res.graph;

    REQUIRE(s.node_count() == 2);
    // 5 real directed edges: 0->1 (chains 3,4) and 1->0 (chains 2,3,4). The old
    // weight-inferred logic would add a 6th, a zero-weight phantom 0->1 for the one-way chain.
    REQUIRE(s.edge_count() == 5);
    for (Weight w : s.raw_weights()) REQUIRE(w > 0.0);  // no zero-weight phantom
    REQUIRE(res.edge_geometry.edge_count() == s.edge_count());  // geometry stays aligned
}

TEST_CASE("simplify_edge_geometry keeps off-chord points and drops near-chord ones",
          "[geometry][simplify]") {
    // Edge 0: bent 3-point polyline — midpoint (1,1) sits perpendicular distance 1.0 off the
    // chord (0,0)->(0,2). Edge 1: an already-straight 2-point segment (must be untouched).
    EdgeGeometry geom;
    geom.offsets = {0, 3, 5};
    geom.points = {{0.0, 0.0}, {1.0, 1.0}, {0.0, 2.0}, {5.0, 5.0}, {5.0, 7.0}};

    SECTION("tolerance below the off-chord distance retains the bend") {
        auto out = simplify_edge_geometry(geom, 0.5);  // 0.5 < 1.0 -> keep midpoint
        REQUIRE(out.edge_count() == 2);
        REQUIRE(out.points_for(0).size() == 3);  // bend retained
        REQUIRE(out.points_for(0)[1].lat == 1.0);
        REQUIRE(out.points_for(1).size() == 2);  // straight edge unchanged
    }

    SECTION("tolerance above the off-chord distance collapses to the endpoints") {
        auto out = simplify_edge_geometry(geom, 2.0);  // 2.0 > 1.0 -> drop midpoint
        REQUIRE(out.edge_count() == 2);
        REQUIRE(out.points_for(0).size() == 2);
        REQUIRE(out.points_for(0).front().lat == 0.0);
        REQUIRE(out.points_for(0).back().lon == 2.0);
        REQUIRE(out.points_for(1).size() == 2);
    }

    SECTION("tolerance <= 0 returns an unchanged copy") {
        auto out = simplify_edge_geometry(geom, 0.0);
        REQUIRE(out.points.size() == geom.points.size());
        REQUIRE(out.points_for(0).size() == 3);
    }
}
