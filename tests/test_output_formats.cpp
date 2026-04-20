#include <catch2/catch_test_macros.hpp>
#include "gravel/io/arrow_output.h"
#include "gravel/io/geojson_output.h"
#include "gravel/validation/synthetic_graphs.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/fragility/route_fragility.h"
#include "test_temp_path.h"
#include <fstream>
#include <filesystem>
#include <utility>
#include <vector>
#include <string>

using namespace gravel;

static ArrayGraph make_coord_grid_5() {
    uint32_t rows = 5, cols = 5;
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

TEST_CASE("JSONL fragility output", "[output]") {
    auto grid = make_grid_graph(5, 5);
    auto ch = build_ch(*grid);
    ShortcutIndex idx(ch);

    std::vector<std::pair<NodeID, NodeID>> pairs = {{0, 24}, {5, 19}};
    std::vector<FragilityResult> results;
    for (auto [s, t] : pairs) {
        results.push_back(route_fragility(ch, idx, *grid, s, t));
    }

    std::string path = gravel::test::test_temp_path("test_fragility.jsonl");
    write_fragility_jsonl(results, pairs, path);

    // Verify file exists and has 2 lines. Scope the ifstream so its
    // destructor (and the underlying HANDLE) is released before the
    // filesystem::remove below — on Windows, remove() will fail with
    // "file in use" if the stream is still alive.
    {
        std::ifstream in(path);
        REQUIRE(in.good());
        int lines = 0;
        std::string line;
        while (std::getline(in, line)) {
            REQUIRE(!line.empty());
            lines++;
        }
        REQUIRE(lines == 2);
    }

    std::filesystem::remove(path);
}

TEST_CASE("GeoJSON path output", "[output]") {
    auto grid = make_coord_grid_5();
    auto ch = build_ch(grid);
    ShortcutIndex idx(ch);

    auto frag = route_fragility(ch, idx, grid, 0, 24);
    REQUIRE(frag.valid());

    auto geojson = path_to_geojson(grid, frag.primary_path, &frag);

    REQUIRE(geojson["type"] == "Feature");
    REQUIRE(geojson["geometry"]["type"] == "LineString");
    REQUIRE(geojson["geometry"]["coordinates"].size() > 0);
    REQUIRE(geojson["properties"]["primary_distance"].get<double>() > 0.0);
}

TEST_CASE("GeoJSON location fragility output", "[output]") {
    auto grid = make_coord_grid_5();
    auto ch = build_ch(grid);

    LocationFragilityConfig config;
    config.center = {35.02, -82.98};
    config.radius_meters = 5000.0;
    config.seed = 42;

    auto result = location_fragility(grid, ch, config);
    auto geojson = location_fragility_to_geojson(result, config.center);

    REQUIRE(geojson["type"] == "FeatureCollection");
    REQUIRE(geojson["features"].size() > 0);

    // First feature should be the center point
    auto& first = geojson["features"][0];
    REQUIRE(first["properties"]["type"] == "center");
    REQUIRE(first["geometry"]["type"] == "Point");

    // Write to file and verify
    std::string path = gravel::test::test_temp_path("test_location.geojson");
    write_geojson(geojson, path);
    REQUIRE(std::filesystem::exists(path));
    REQUIRE(std::filesystem::file_size(path) > 0);
    std::filesystem::remove(path);
}
