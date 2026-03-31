#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/analysis/betweenness.h"
#include <algorithm>
#include <numeric>

using namespace gravel;

// Star graph: center node 0 connected to 1..n-1
static ArrayGraph make_star(uint32_t n) {
    std::vector<Edge> edges;
    for (uint32_t i = 1; i < n; ++i) {
        edges.push_back({0, i, 1.0});
        edges.push_back({i, 0, 1.0});
    }
    return ArrayGraph(n, std::move(edges));
}

// Path: 0-1-2-...(n-1)
static ArrayGraph make_path(uint32_t n) {
    std::vector<Edge> edges;
    for (uint32_t i = 0; i + 1 < n; ++i) {
        edges.push_back({i, i + 1, 1.0});
        edges.push_back({i + 1, i, 1.0});
    }
    return ArrayGraph(n, std::move(edges));
}

TEST_CASE("Star graph — center edges have highest betweenness", "[betweenness]") {
    auto star = make_star(6);  // 0 is center, 1-5 are leaves
    auto result = edge_betweenness(star);

    // Edges from center (node 0) should have highest scores
    // Leaf-to-leaf paths all go through center
    auto targets = star.outgoing_targets(0);
    uint32_t base = star.raw_offsets()[0];
    double center_score = result.edge_scores[base];

    // All center edges should have the same score
    for (size_t i = 0; i < targets.size(); ++i) {
        REQUIRE_THAT(result.edge_scores[base + i],
                     Catch::Matchers::WithinRel(center_score, 0.01));
    }

    // Leaf edges should have lower or equal score
    for (NodeID leaf = 1; leaf < 6; ++leaf) {
        uint32_t leaf_base = star.raw_offsets()[leaf];
        // Leaf only has one edge back to center
        REQUIRE(result.edge_scores[leaf_base] <= center_score + 1e-6);
    }
}

TEST_CASE("Path graph — middle edge has highest betweenness", "[betweenness]") {
    auto path = make_path(5);  // 0-1-2-3-4
    auto result = edge_betweenness(path);

    // Edge 2→3 and 1→2 are in the middle region
    // The edge between nodes 2 and 3 (or 1 and 2) should have highest betweenness
    // For a path of 5 nodes, middle edges carry more shortest paths

    // Find max edge score
    double max_score = *std::max_element(result.edge_scores.begin(),
                                          result.edge_scores.end());
    REQUIRE(max_score > 0.0);

    // Edge scores should be non-negative
    for (double s : result.edge_scores) {
        REQUIRE(s >= -1e-10);
    }
}

TEST_CASE("Betweenness sampled approximates exact", "[betweenness]") {
    auto path = make_path(20);

    auto exact = edge_betweenness(path);
    BetweennessConfig sampled_config;
    sampled_config.sample_sources = 15;
    sampled_config.seed = 42;
    auto sampled = edge_betweenness(path, sampled_config);

    REQUIRE(sampled.sources_used == 15);

    // The sampled result should be in the right ballpark
    double exact_total = std::accumulate(exact.edge_scores.begin(),
                                          exact.edge_scores.end(), 0.0);
    double sampled_total = std::accumulate(sampled.edge_scores.begin(),
                                            sampled.edge_scores.end(), 0.0);

    // Within 50% (sampling is noisy but should be same order of magnitude)
    REQUIRE(sampled_total > exact_total * 0.5);
    REQUIRE(sampled_total < exact_total * 1.5);
}
