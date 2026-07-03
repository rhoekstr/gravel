#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gravel/analysis/scenario_fragility.h"
#include "gravel/core/graph_build.h"

#include <algorithm>
#include <utility>
#include <vector>

using namespace gravel;

TEST_CASE("hazard_edge_probabilities: both endpoints inside, max wins, baseline", "[hazard]") {
    // 4 nodes in a unit square: 0=(0,0) 1=(0,1) 2=(1,0) 3=(1,1). Edges 0-1, 1-3 (bidirectional).
    std::vector<uint32_t> offsets = {0, 1, 3, 3, 4};
    std::vector<NodeID> targets = {1, 0, 3, 1};  // 0->1 ; 1->0,1->3 ; (2 none) ; 3->1
    std::vector<Weight> weights(targets.size(), 1.0);
    std::vector<Coord> coords = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};
    ArrayGraph g(std::move(offsets), std::move(targets), std::move(weights), std::move(coords));

    // Polygon covering the lat=0 side (nodes 0 and 1 only).
    Polygon zone;
    zone.vertices = {{-0.1, -0.1}, {-0.1, 1.1}, {0.1, 1.1}, {0.1, -0.1}};
    auto probs = hazard_edge_probabilities(g, {{zone, 0.8}}, 0.05);

    REQUIRE(probs.size() == 4);
    auto tg = g.raw_targets();
    auto off = g.raw_offsets();
    for (uint32_t u = 0; u < g.node_count(); ++u) {
        for (uint32_t e = off[u]; e < off[u + 1]; ++e) {
            NodeID v = tg[e];
            bool both_in = (u <= 1) && (v <= 1);          // only the 0<->1 edges
            REQUIRE(probs[e] == Catch::Approx(both_in ? 0.8 : 0.05));
        }
    }
}

TEST_CASE("graph_from_endpoints snaps shared nodes", "[graph_build]") {
    // Two chained segments (0,0)->(0,1) and (0,1)->(0,2); the shared (0,1) endpoint snaps.
    std::vector<Coord> src = {{0.0, 0.0}, {0.0, 1.0}};
    std::vector<Coord> tgt = {{0.0, 1.0}, {0.0, 2.0}};
    std::vector<double> w = {1.0, 1.0};
    auto g = graph_from_endpoints(src, tgt, w, 7, /*directed=*/false);

    REQUIRE(g->node_count() == 3);          // (0,0), (0,1), (0,2) — shared node merged
    REQUIRE(g->edge_count() == 4);          // 2 edges x 2 directions

    // Resolve node IDs by coordinate and assert the edges connect the correct snapped nodes.
    auto find_node = [&](double lat, double lon) -> NodeID {
        for (NodeID n = 0; n < g->node_count(); ++n) {
            auto c = g->node_coordinate(n);
            if (c && c->lat == Catch::Approx(lat) && c->lon == Catch::Approx(lon)) return n;
        }
        FAIL("node not found for coordinate");
        return 0;
    };
    NodeID a = find_node(0.0, 0.0), b = find_node(0.0, 1.0), c = find_node(0.0, 2.0);
    REQUIRE(a != b);
    REQUIRE(b != c);

    auto off = g->raw_offsets();
    auto tg = g->raw_targets();
    auto neighbors = [&](NodeID u) {
        std::vector<NodeID> out;
        for (uint32_t e = off[u]; e < off[u + 1]; ++e) out.push_back(tg[e]);
        return out;
    };
    // b is the shared midpoint: undirected, it links to both a and c.
    auto nb = neighbors(b);
    REQUIRE(std::find(nb.begin(), nb.end(), a) != nb.end());
    REQUIRE(std::find(nb.begin(), nb.end(), c) != nb.end());
    // a links only to b; c links only to b.
    REQUIRE(neighbors(a) == std::vector<NodeID>{b});
    REQUIRE(neighbors(c) == std::vector<NodeID>{b});
    // Shared node carries the (0,1) coordinate.
    REQUIRE(g->node_coordinate(b)->lon == Catch::Approx(1.0));

    auto directed = graph_from_endpoints(src, tgt, w, 7, /*directed=*/true);
    REQUIRE(directed->node_count() == 3);
    REQUIRE(directed->edge_count() == 2);   // no reverse edges
}
