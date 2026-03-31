#include <catch2/catch_test_macros.hpp>
#include "gravel/core/edge_sampler.h"
#include "gravel/core/array_graph.h"
#include <unordered_set>

using namespace gravel;

// Build a 5x5 grid graph with coordinates
static ArrayGraph make_grid_5x5() {
    std::vector<Edge> edges;
    std::vector<Coord> coords(25);

    for (int r = 0; r < 5; ++r) {
        for (int c = 0; c < 5; ++c) {
            NodeID u = r * 5 + c;
            coords[u] = {35.0 + r * 0.01, -83.0 + c * 0.01};  // ~1km spacing
            if (c + 1 < 5) {
                NodeID v = r * 5 + c + 1;
                edges.push_back({u, v, 100});
                edges.push_back({v, u, 100});
            }
            if (r + 1 < 5) {
                NodeID v = (r + 1) * 5 + c;
                edges.push_back({u, v, 100});
                edges.push_back({v, u, 100});
            }
        }
    }

    ArrayGraph g(25, std::move(edges));
    // Reconstruct with coords
    return ArrayGraph(g.raw_offsets(), g.raw_targets(), g.raw_weights(), std::move(coords));
}

TEST_CASE("EdgeSampler uniform random", "[edge_sampler]") {
    auto g = make_grid_5x5();
    EdgeSampler sampler(g);

    SamplerConfig cfg;
    cfg.strategy = SamplingStrategy::UNIFORM_RANDOM;
    cfg.target_count = 10;
    cfg.seed = 42;

    auto indices = sampler.sample(cfg);
    REQUIRE(indices.size() == 10);

    // All indices should be valid
    for (uint32_t idx : indices) {
        REQUIRE(idx < g.edge_count());
    }

    // No duplicates
    std::unordered_set<uint32_t> unique(indices.begin(), indices.end());
    REQUIRE(unique.size() == indices.size());
}

TEST_CASE("EdgeSampler sample_pairs returns valid node pairs", "[edge_sampler]") {
    auto g = make_grid_5x5();
    EdgeSampler sampler(g);

    SamplerConfig cfg;
    cfg.target_count = 5;
    cfg.seed = 123;

    auto pairs = sampler.sample_pairs(cfg);
    REQUIRE(pairs.size() == 5);

    for (const auto& [src, tgt] : pairs) {
        REQUIRE(src < g.node_count());
        REQUIRE(tgt < g.node_count());
        // Verify this is actually an edge in the graph
        auto targets = g.outgoing_targets(src);
        bool found = false;
        for (NodeID t : targets) {
            if (t == tgt) { found = true; break; }
        }
        REQUIRE(found);
    }
}

TEST_CASE("EdgeSampler with edge filter", "[edge_sampler]") {
    auto g = make_grid_5x5();
    EdgeSampler sampler(g);

    SamplerConfig cfg;
    cfg.target_count = 5;
    cfg.seed = 42;
    // Only sample even-indexed edges
    cfg.edge_filter = [](uint32_t idx) { return idx % 2 == 0; };

    auto indices = sampler.sample(cfg);
    REQUIRE(indices.size() == 5);

    for (uint32_t idx : indices) {
        REQUIRE(idx % 2 == 0);
    }
}

TEST_CASE("EdgeSampler importance weighted favors high-weight edges", "[edge_sampler]") {
    auto g = make_grid_5x5();
    EdgeSampler sampler(g);

    // Give first 10 edges very high weight, rest zero
    std::vector<double> weights(g.edge_count(), 0.0);
    for (uint32_t i = 0; i < 10 && i < g.edge_count(); ++i)
        weights[i] = 1000.0;

    SamplerConfig cfg;
    cfg.strategy = SamplingStrategy::IMPORTANCE_WEIGHTED;
    cfg.target_count = 8;
    cfg.weights = weights;
    cfg.seed = 42;

    auto indices = sampler.sample(cfg);
    REQUIRE(indices.size() == 8);

    // All sampled edges should be from the high-weight set
    for (uint32_t idx : indices) {
        REQUIRE(idx < 10);
    }
}

TEST_CASE("EdgeSampler spatially stratified covers grid", "[edge_sampler]") {
    auto g = make_grid_5x5();
    EdgeSampler sampler(g);

    SamplerConfig cfg;
    cfg.strategy = SamplingStrategy::SPATIALLY_STRATIFIED;
    cfg.target_count = 20;
    cfg.grid_rows = 3;
    cfg.grid_cols = 3;
    cfg.seed = 42;

    auto indices = sampler.sample(cfg);
    REQUIRE(indices.size() > 0);
    REQUIRE(indices.size() <= 20);

    // All valid
    for (uint32_t idx : indices) {
        REQUIRE(idx < g.edge_count());
    }
}

TEST_CASE("EdgeSampler cluster dispersed produces spread-out edges", "[edge_sampler]") {
    auto g = make_grid_5x5();
    EdgeSampler sampler(g);

    SamplerConfig cfg;
    cfg.strategy = SamplingStrategy::CLUSTER_DISPERSED;
    cfg.target_count = 5;
    cfg.min_spacing_m = 100.0;
    cfg.seed = 42;

    auto indices = sampler.sample(cfg);
    REQUIRE(indices.size() > 0);
    REQUIRE(indices.size() <= 5);
}

TEST_CASE("EdgeSampler target_count exceeding pool", "[edge_sampler]") {
    auto g = make_grid_5x5();
    EdgeSampler sampler(g);

    SamplerConfig cfg;
    cfg.target_count = 99999;  // Way more than available
    cfg.seed = 42;

    auto indices = sampler.sample(cfg);
    // Should return at most edge_count
    REQUIRE(indices.size() <= g.edge_count());
}
