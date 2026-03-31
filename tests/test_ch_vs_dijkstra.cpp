#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <random>
#include "gravel/ch/contraction.h"
#include "gravel/ch/ch_query.h"
#include "gravel/core/dijkstra.h"
#include "gravel/validation/validator.h"
#include "gravel/validation/synthetic_graphs.h"

using namespace gravel;

TEST_CASE("CH matches Dijkstra on 10x10 grid (exhaustive)", "[validation][ch]") {
    auto graph = make_grid_graph(10, 10);  // 100 nodes
    auto ch = build_ch(*graph);
    CHQuery query(ch);

    auto report = validate_ch(*graph, query, {.mode = ValidationConfig::EXHAUSTIVE});
    INFO("Mismatches: " << report.mismatches << "/" << report.pairs_tested);
    if (!report.failures.empty()) {
        auto [s, t, expected, got] = report.failures[0];
        INFO("First failure: " << s << " -> " << t
             << " expected=" << expected << " got=" << got);
    }
    REQUIRE(report.passed);
    REQUIRE(report.mismatches == 0);
}

TEST_CASE("CH matches Dijkstra on 500-node random graph (exhaustive)", "[validation][ch]") {
    auto graph = make_random_graph(500, 2000, 1.0, 100.0, 42);
    auto ch = build_ch(*graph);
    CHQuery query(ch);

    auto report = validate_ch(*graph, query, {.mode = ValidationConfig::EXHAUSTIVE});
    INFO("Mismatches: " << report.mismatches << "/" << report.pairs_tested);
    if (!report.failures.empty()) {
        auto [s, t, expected, got] = report.failures[0];
        INFO("First failure: " << s << " -> " << t
             << " expected=" << expected << " got=" << got);
    }
    REQUIRE(report.passed);
}

TEST_CASE("CH matches Dijkstra on 500-node tree-with-bridges (exhaustive)", "[validation][ch]") {
    auto graph = make_tree_with_bridges(500, 50, 99);
    auto ch = build_ch(*graph);
    CHQuery query(ch);

    auto report = validate_ch(*graph, query, {.mode = ValidationConfig::EXHAUSTIVE});
    INFO("Mismatches: " << report.mismatches << "/" << report.pairs_tested);
    if (!report.failures.empty()) {
        auto [s, t, expected, got] = report.failures[0];
        INFO("First failure: " << s << " -> " << t
             << " expected=" << expected << " got=" << got);
    }
    REQUIRE(report.passed);
}

TEST_CASE("CH on tiny 3-node graph", "[validation][ch]") {
    // 0 -> 1 (w=1), 1 -> 2 (w=2), 0 -> 2 (w=5)
    std::vector<Edge> edges = {
        {0, 1, 1.0}, {1, 0, 1.0},
        {1, 2, 2.0}, {2, 1, 2.0},
        {0, 2, 5.0}, {2, 0, 5.0},
    };
    ArrayGraph graph(3, std::move(edges));
    auto ch = build_ch(graph);
    CHQuery query(ch);

    auto report = validate_ch(graph, query, {.mode = ValidationConfig::EXHAUSTIVE});
    REQUIRE(report.passed);
}

TEST_CASE("CH path unpacking produces valid paths on grid", "[validation][ch][path]") {
    auto graph = make_grid_graph(5, 5);  // 25 nodes
    auto ch = build_ch(*graph);
    CHQuery query(ch);

    // Check all reachable pairs: path must start at source, end at target,
    // each consecutive pair must be an actual edge, and total weight must match distance.
    int paths_checked = 0;
    for (NodeID s = 0; s < graph->node_count(); ++s) {
        for (NodeID t = 0; t < graph->node_count(); ++t) {
            if (s == t) continue;
            auto result = query.route(s, t);
            Weight dij_dist = dijkstra_pair(*graph, s, t);

            if (dij_dist >= INF_WEIGHT) {
                REQUIRE(result.distance >= INF_WEIGHT);
                continue;
            }

            REQUIRE(result.distance == Catch::Approx(dij_dist).epsilon(1e-9));
            REQUIRE(result.path.size() >= 2);
            REQUIRE(result.path.front() == s);
            REQUIRE(result.path.back() == t);

            // Verify each edge in path exists and sum weights
            // Use min weight for parallel edges (random graphs can have duplicates)
            Weight path_weight = 0.0;
            for (size_t i = 0; i + 1 < result.path.size(); ++i) {
                NodeID u = result.path[i];
                NodeID v = result.path[i + 1];
                Weight best_w = INF_WEIGHT;
                auto targets = graph->outgoing_targets(u);
                auto weights = graph->outgoing_weights(u);
                for (uint32_t ei = 0; ei < targets.size(); ++ei) {
                    if (targets[ei] == v && weights[ei] < best_w) {
                        best_w = weights[ei];
                    }
                }
                INFO("Edge " << u << " -> " << v << " not in graph (path " << s << " -> " << t << ")");
                REQUIRE(best_w < INF_WEIGHT);
                path_weight += best_w;
            }
            REQUIRE(path_weight == Catch::Approx(dij_dist).epsilon(1e-9));
            paths_checked++;
        }
    }
    REQUIRE(paths_checked > 0);
}

TEST_CASE("CH path unpacking on random graph", "[validation][ch][path]") {
    auto graph = make_random_graph(200, 800, 1.0, 50.0, 123);
    auto ch = build_ch(*graph);
    CHQuery query(ch);

    // Sampled path validation on 500 pairs
    std::mt19937 rng(42);
    int paths_checked = 0;
    for (int i = 0; i < 500; ++i) {
        NodeID s = rng() % graph->node_count();
        NodeID t = rng() % graph->node_count();
        if (s == t) continue;

        auto result = query.route(s, t);
        Weight dij_dist = dijkstra_pair(*graph, s, t);

        if (dij_dist >= INF_WEIGHT) continue;

        REQUIRE(result.distance == Catch::Approx(dij_dist).epsilon(1e-9));
        REQUIRE(result.path.front() == s);
        REQUIRE(result.path.back() == t);

        // No duplicate consecutive nodes
        for (size_t j = 0; j + 1 < result.path.size(); ++j) {
            REQUIRE(result.path[j] != result.path[j + 1]);
        }

        // Verify edges exist and sum matches (use min weight for parallel edges)
        Weight path_weight = 0.0;
        for (size_t j = 0; j + 1 < result.path.size(); ++j) {
            NodeID u = result.path[j];
            NodeID v = result.path[j + 1];
            Weight best_w = INF_WEIGHT;
            auto targets = graph->outgoing_targets(u);
                auto weights = graph->outgoing_weights(u);
                for (uint32_t ei = 0; ei < targets.size(); ++ei) {
                    if (targets[ei] == v && weights[ei] < best_w) {
                        best_w = weights[ei];
                    }
                }
            INFO("Edge " << u << " -> " << v << " not in graph");
            REQUIRE(best_w < INF_WEIGHT);
            path_weight += best_w;
        }
        REQUIRE(path_weight == Catch::Approx(dij_dist).epsilon(1e-9));
        paths_checked++;
    }
    REQUIRE(paths_checked > 100);
}

TEST_CASE("CH on disconnected graph handles unreachable pairs", "[validation][ch]") {
    // Two components: {0,1} and {2,3}
    std::vector<Edge> edges = {
        {0, 1, 1.0}, {1, 0, 1.0},
        {2, 3, 1.0}, {3, 2, 1.0},
    };
    ArrayGraph graph(4, std::move(edges));
    auto ch = build_ch(graph);
    CHQuery query(ch);

    auto report = validate_ch(graph, query, {.mode = ValidationConfig::EXHAUSTIVE});
    REQUIRE(report.passed);
}
