#include <catch2/catch_test_macros.hpp>
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
