#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/snap/snapper.h"
#include "gravel/snap/edge_index.h"
#include <random>
#include <cmath>

using namespace gravel;
using Catch::Matchers::WithinAbs;

// Build a 10x10 grid with coordinates
static ArrayGraph make_coord_grid_10() {
    uint32_t rows = 10, cols = 10;
    NodeID n = rows * cols;
    auto node_id = [cols](uint32_t r, uint32_t c) -> NodeID { return r * cols + c; };

    std::vector<Edge> edges;
    std::vector<Coord> coords(n);
    for (uint32_t r = 0; r < rows; ++r) {
        for (uint32_t c = 0; c < cols; ++c) {
            NodeID u = node_id(r, c);
            coords[u] = {35.0 + r * 0.005, -83.0 + c * 0.005};
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

TEST_CASE("R-tree serialization round-trip", "[snap]") {
    auto grid = make_coord_grid_10();
    auto idx = EdgeIndex::build(grid.raw_coords(), grid.raw_offsets(), grid.raw_targets());

    std::string path = "/tmp/test_edge_index.gravel.rtree";
    idx.save(path);

    auto loaded = EdgeIndex::load(path);
    REQUIRE(loaded.size() == idx.size());

    // Query should produce same results
    Coord query = {35.012, -82.988};
    auto orig_results = idx.query_nearest(query, 5);
    auto load_results = loaded.query_nearest(query, 5);

    REQUIRE(orig_results.size() == load_results.size());
    for (size_t i = 0; i < orig_results.size(); ++i) {
        REQUIRE_THAT(orig_results[i].min_dist_m,
                     WithinAbs(load_results[i].min_dist_m, 1e-3));
    }
}

TEST_CASE("Grid graph snap — midpoint of cell", "[snap][validation]") {
    auto grid = make_coord_grid_10();
    Snapper snapper(grid);

    // Center of cell between nodes (0,0), (0,1), (1,0), (1,1)
    // Should snap to one of the 4 surrounding edges
    Coord center = {35.0025, -82.9975};
    auto r = snapper.snap(center, 1000.0);
    REQUIRE(r.valid());
    REQUIRE_THAT(r.t, WithinAbs(0.5, 0.15));  // should be near midpoint of edge
    REQUIRE(r.snap_distance_m < 500.0);
}

TEST_CASE("Diagonal edge snap — perpendicular projection", "[snap][validation]") {
    // Single diagonal edge from (35.0, -83.0) to (35.01, -82.99)
    std::vector<uint32_t> offsets = {0, 1, 2};
    std::vector<NodeID> targets = {1, 0};
    std::vector<Weight> weights = {1.0, 1.0};
    std::vector<Coord> coords = {{35.0, -83.0}, {35.01, -82.99}};
    ArrayGraph g(std::move(offsets), std::move(targets), std::move(weights), std::move(coords));

    Snapper snapper(g);

    // Point above the midpoint of the diagonal
    Coord point = {35.008, -82.998};
    auto r = snapper.snap(point, 2000.0);
    REQUIRE(r.valid());
    // t should be near 0.5 (midpoint of diagonal)
    REQUIRE(r.t > 0.2);
    REQUIRE(r.t < 0.8);
    // Should not snap to an endpoint
    REQUIRE_FALSE(r.is_exact_node);
}

TEST_CASE("Snap quality report", "[snap]") {
    auto grid = make_coord_grid_10();
    Snapper snapper(grid);

    // Batch of points: mix of good snaps, far snaps, and one impossible
    std::vector<Coord> points = {
        {35.0, -83.0},       // exact node
        {35.002, -82.998},   // near grid
        {35.025, -82.975},   // inside grid
        {36.0, -80.0},       // far away — will fail with small radius
    };

    auto results = snapper.snap_batch(points, 100.0);  // tight radius
    auto report = snap_quality(results, 50.0);

    REQUIRE(report.total == 4);
    REQUIRE(report.succeeded >= 2);  // at least the close ones
    REQUIRE(report.exact_node >= 1);  // first point is exact
    REQUIRE(report.failed >= 1);     // far point should fail
    REQUIRE(report.max_distance_m >= 0.0);
}

TEST_CASE("Random snap round-trip: all snaps produce valid coordinates", "[snap][validation]") {
    auto grid = make_coord_grid_10();
    Snapper snapper(grid);

    std::mt19937_64 rng(42);
    std::uniform_real_distribution<double> lat_dist(35.0, 35.045);
    std::uniform_real_distribution<double> lon_dist(-83.0, -82.955);

    uint32_t valid_count = 0;
    uint32_t total = 100;

    for (uint32_t i = 0; i < total; ++i) {
        Coord point = {lat_dist(rng), lon_dist(rng)};
        auto r = snapper.snap(point, 2000.0);
        if (r.valid()) {
            valid_count++;
            // Snapped coord should be within the grid's bounding box
            REQUIRE(r.snapped_coord.lat >= 34.999);
            REQUIRE(r.snapped_coord.lat <= 35.046);
            REQUIRE(r.snapped_coord.lon >= -83.001);
            REQUIRE(r.snapped_coord.lon <= -82.954);
            // Snap distance should be reasonable
            REQUIRE(r.snap_distance_m < 2000.0);
        }
    }

    // Most random points within the grid should snap successfully
    REQUIRE(valid_count >= 90);
}
