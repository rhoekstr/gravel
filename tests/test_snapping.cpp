#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/snap/snapper.h"
#include "gravel/snap/edge_index.h"
#include "gravel/ch/ch_query.h"
#include "gravel/ch/contraction.h"
#include "gravel/core/dijkstra.h"

using namespace gravel;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// Build a simple 4-node grid:
//   0 --- 1
//   |     |
//   2 --- 3
// With coordinates forming a small square in geographic space.
static ArrayGraph make_grid_graph() {
    std::vector<uint32_t> offsets = {0, 2, 4, 6, 8};
    std::vector<NodeID> targets = {1, 2,  0, 3,  0, 3,  1, 2};
    std::vector<Weight> weights = {1.0, 1.0,  1.0, 1.0,  1.0, 1.0,  1.0, 1.0};
    std::vector<Coord> coords = {
        {35.0, -83.0},   // 0: top-left
        {35.0, -82.99},  // 1: top-right
        {34.99, -83.0},  // 2: bottom-left
        {34.99, -82.99}  // 3: bottom-right
    };
    return ArrayGraph(std::move(offsets), std::move(targets), std::move(weights), std::move(coords));
}

TEST_CASE("EdgeIndex builds and queries", "[snap]") {
    auto g = make_grid_graph();
    auto idx = EdgeIndex::build(g.raw_coords(), g.raw_offsets(), g.raw_targets());

    REQUIRE(idx.size() == 8);  // 8 directed edges

    // Query near node 0
    auto cands = idx.query_nearest({35.0, -83.0}, 4);
    REQUIRE(!cands.empty());
    // Nearest edge should involve node 0
    bool found_node0 = false;
    for (const auto& c : cands) {
        if (c.edge.source == 0 || c.edge.target == 0) {
            found_node0 = true;
            break;
        }
    }
    REQUIRE(found_node0);
}

TEST_CASE("Snapper snaps to nearest edge", "[snap]") {
    auto g = make_grid_graph();
    Snapper snapper(g);

    // Point exactly at node 0
    auto r = snapper.snap({35.0, -83.0});
    REQUIRE(r.valid());
    REQUIRE(r.snap_distance_m < 1.0);  // very close
    REQUIRE(r.is_exact_node);

    // Point midway between node 0 and node 1 (along top edge)
    auto r2 = snapper.snap({35.0, -82.995});
    REQUIRE(r2.valid());
    REQUIRE(r2.edge_source == 0);
    REQUIRE(r2.edge_target == 1);
    REQUIRE_THAT(r2.t, WithinAbs(0.5, 0.1));
    REQUIRE(r2.snap_distance_m < 1.0);
}

TEST_CASE("Snapper returns invalid for distant point", "[snap]") {
    auto g = make_grid_graph();
    Snapper snapper(g);

    // Point very far away with tiny radius
    auto r = snapper.snap({0.0, 0.0}, 1.0);
    REQUIRE(!r.valid());
}

TEST_CASE("Snapper batch snap", "[snap]") {
    auto g = make_grid_graph();
    Snapper snapper(g);

    std::vector<Coord> points = {
        {35.0, -83.0},
        {34.99, -82.99},
        {35.0, -82.995}
    };
    auto results = snapper.snap_batch(points);
    REQUIRE(results.size() == 3);
    REQUIRE(results[0].valid());
    REQUIRE(results[1].valid());
    REQUIRE(results[2].valid());
}

TEST_CASE("Snap-aware CH distance", "[snap]") {
    // Build a chain graph: 0 -- 1 -- 2 -- 3 with known coords
    std::vector<Edge> edges;
    for (int i = 0; i < 3; ++i) {
        edges.push_back({(NodeID)i, (NodeID)(i + 1), 1.0});
        edges.push_back({(NodeID)(i + 1), (NodeID)i, 1.0});
    }
    ArrayGraph g(4, std::move(edges));

    // Reconstruct with coords
    auto off = g.raw_offsets();
    auto tgt = std::vector<NodeID>(g.raw_targets().begin(), g.raw_targets().end());
    auto wgt = std::vector<Weight>(g.raw_weights().begin(), g.raw_weights().end());
    std::vector<Coord> coords = {
        {35.0, -83.0}, {35.0, -82.99}, {35.0, -82.98}, {35.0, -82.97}
    };
    ArrayGraph g2(std::move(off), std::move(tgt), std::move(wgt), std::move(coords));

    auto ch = build_ch(g2);
    CHQuery query(ch);
    Snapper snapper(g2);

    // Snap exactly at node 0 and node 3
    auto s0 = snapper.snap({35.0, -83.0});
    auto s3 = snapper.snap({35.0, -82.97});
    REQUIRE(s0.valid());
    REQUIRE(s3.valid());

    // Distance from snapped node 0 to snapped node 3 should be close to 3.0
    Weight snap_dist = query.distance(s0, s3);
    Weight node_dist = query.distance(0, 3);
    // Both should be 3.0 (or very close, accounting for phantom offset)
    REQUIRE(snap_dist < INF_WEIGHT);
    REQUIRE_THAT(snap_dist, WithinAbs(node_dist, 0.5));
}

TEST_CASE("Snapper requires graph with coordinates", "[snap]") {
    std::vector<Edge> edges = {{0, 1, 1.0}};
    ArrayGraph g(2, std::move(edges));
    REQUIRE_THROWS_AS(Snapper(g), std::runtime_error);
}
