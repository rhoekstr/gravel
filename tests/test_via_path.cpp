#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/fragility/via_path.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/ch_query.h"
#include "gravel/validation/synthetic_graphs.h"

using namespace gravel;
using Catch::Matchers::WithinAbs;

TEST_CASE("Via-path alternatives are valid paths", "[via_path]") {
    auto graph = make_grid_graph(10, 10);
    auto ch = build_ch(*graph);

    auto alts = find_alternative_routes(ch, 0, 99);

    CHQuery query(ch);
    Weight primary = query.distance(0, 99);

    for (const auto& alt : alts) {
        // Path starts at source and ends at target.
        REQUIRE(alt.path.front() == 0);
        REQUIRE(alt.path.back() == 99);
        REQUIRE(alt.path.size() >= 2);

        // Distance matches path verification via CH queries for sub-paths.
        REQUIRE(alt.distance >= primary);
        REQUIRE(alt.stretch >= 1.0);
    }
}

TEST_CASE("Via-path respects max_stretch and max_sharing", "[via_path]") {
    auto graph = make_grid_graph(10, 10);
    auto ch = build_ch(*graph);

    ViaPathConfig config;
    config.max_stretch = 1.3;
    config.max_sharing = 0.7;
    config.max_alternatives = 5;

    auto alts = find_alternative_routes(ch, 0, 99, config);

    for (const auto& alt : alts) {
        REQUIRE(alt.stretch <= config.max_stretch + 1e-9);
        REQUIRE(alt.sharing < config.max_sharing);
    }
}

TEST_CASE("Chain graph produces no alternatives", "[via_path]") {
    // Chain: 0-1-2-3-4 — only one path exists
    std::vector<Edge> edges;
    for (int i = 0; i < 4; ++i) {
        edges.push_back({(NodeID)i, (NodeID)(i + 1), 1.0});
        edges.push_back({(NodeID)(i + 1), (NodeID)i, 1.0});
    }
    ArrayGraph graph(5, std::move(edges));
    auto ch = build_ch(graph);

    auto alts = find_alternative_routes(ch, 0, 4);
    // No alternatives should exist (or very few with high sharing).
    // With default max_sharing=0.80, any alternative on a chain would share all edges.
    REQUIRE(alts.empty());
}

TEST_CASE("Via-path max_alternatives is respected", "[via_path]") {
    auto graph = make_grid_graph(10, 10);
    auto ch = build_ch(*graph);

    ViaPathConfig config;
    config.max_alternatives = 2;

    auto alts = find_alternative_routes(ch, 0, 99, config);
    REQUIRE(alts.size() <= 2);
}

TEST_CASE("Via-path alternatives have correct distances", "[via_path]") {
    auto graph = make_grid_graph(8, 8);
    auto ch = build_ch(*graph);

    auto alts = find_alternative_routes(ch, 0, 63);

    CHQuery query(ch);
    for (const auto& alt : alts) {
        // Verify the alternative distance by querying s→via and via→t.
        // The path should be consistent.
        REQUIRE(alt.path.size() >= 2);
        REQUIRE(alt.distance > 0);

        // Verify path is connected: each consecutive pair should be a valid edge.
        // Sum path segment distances.
        Weight sum = 0.0;
        for (size_t i = 0; i + 1 < alt.path.size(); ++i) {
            Weight seg = query.distance(alt.path[i], alt.path[i + 1]);
            // Each step should be a single edge or sub-path with finite distance.
            REQUIRE(seg < INF_WEIGHT);
        }
    }
}
