#include <catch2/catch_test_macros.hpp>
#include "gravel/core/incremental_sssp.h"
#include "gravel/core/array_graph.h"

using namespace gravel;

// Simple path graph: 0 -- 1 -- 2 -- 3 -- 4 (all weight 10)
static LocalGraph make_path_graph() {
    LocalGraph g;
    g.n_nodes = 5;
    g.adj.resize(5);
    for (uint32_t u = 0; u < 4; ++u) {
        g.adj[u].push_back({u + 1, 10});
        g.adj[u + 1].push_back({u, 10});
    }
    g.local_to_original = {0, 1, 2, 3, 4};
    return g;
}

// Diamond graph:  0 --10-- 1 --10-- 3
//                  \                /
//                   20-- 2 --20---
static LocalGraph make_diamond_graph() {
    LocalGraph g;
    g.n_nodes = 4;
    g.adj.resize(4);
    // 0-1: weight 10, 1-3: weight 10
    g.adj[0].push_back({1, 10}); g.adj[1].push_back({0, 10});
    g.adj[1].push_back({3, 10}); g.adj[3].push_back({1, 10});
    // 0-2: weight 20, 2-3: weight 20
    g.adj[0].push_back({2, 20}); g.adj[2].push_back({0, 20});
    g.adj[2].push_back({3, 20}); g.adj[3].push_back({2, 20});
    g.local_to_original = {0, 1, 2, 3};
    return g;
}

TEST_CASE("IncrementalSSSP unblocked matches full Dijkstra", "[incremental_sssp]") {
    auto g = make_path_graph();

    std::unordered_set<uint64_t> no_blocks;
    IncrementalSSSP engine(g, {0}, no_blocks);

    // With no blocks, dist should equal dist_full
    REQUIRE(engine.dist(0, 0) == 0);
    REQUIRE(engine.dist(0, 1) == 10);
    REQUIRE(engine.dist(0, 2) == 20);
    REQUIRE(engine.dist(0, 3) == 30);
    REQUIRE(engine.dist(0, 4) == 40);
}

TEST_CASE("IncrementalSSSP blocked edge increases distance", "[incremental_sssp]") {
    auto g = make_path_graph();

    // Block edge 1-2 → node 2,3,4 should become unreachable from 0
    std::unordered_set<uint64_t> blocked;
    blocked.insert(IncrementalSSSP::edge_key(1, 2));

    IncrementalSSSP engine(g, {0}, blocked);

    REQUIRE(engine.dist(0, 0) == 0);
    REQUIRE(engine.dist(0, 1) == 10);
    REQUIRE(engine.dist(0, 2) >= IncrementalSSSP::INF);
    REQUIRE(engine.dist(0, 3) >= IncrementalSSSP::INF);
    REQUIRE(engine.dist(0, 4) >= IncrementalSSSP::INF);
}

TEST_CASE("IncrementalSSSP restore_edge fixes distances", "[incremental_sssp]") {
    auto g = make_path_graph();

    std::unordered_set<uint64_t> blocked;
    blocked.insert(IncrementalSSSP::edge_key(1, 2));

    IncrementalSSSP engine(g, {0}, blocked);

    // Before restore: 2,3,4 unreachable
    REQUIRE(engine.dist(0, 2) >= IncrementalSSSP::INF);

    // Restore edge 1-2 (weight 10)
    engine.restore_edge(1, 2, 10);

    // Now distances should match unblocked
    REQUIRE(engine.dist(0, 2) == 20);
    REQUIRE(engine.dist(0, 3) == 30);
    REQUIRE(engine.dist(0, 4) == 40);
    REQUIRE(engine.dist(0, 2) == engine.dist_full(0, 2));
}

TEST_CASE("IncrementalSSSP multiple sources", "[incremental_sssp]") {
    auto g = make_path_graph();

    std::unordered_set<uint64_t> blocked;
    blocked.insert(IncrementalSSSP::edge_key(2, 3));

    IncrementalSSSP engine(g, {0, 4}, blocked);

    // Source 0: can reach 0,1,2 but not 3,4
    REQUIRE(engine.dist(0, 2) == 20);
    REQUIRE(engine.dist(0, 3) >= IncrementalSSSP::INF);

    // Source 1 (node 4): can reach 4,3 but not 2,1,0
    REQUIRE(engine.dist(1, 3) == 10);
    REQUIRE(engine.dist(1, 2) >= IncrementalSSSP::INF);

    engine.restore_edge(2, 3, 10);

    // Both sources should now reach all nodes
    REQUIRE(engine.dist(0, 4) == 40);
    REQUIRE(engine.dist(1, 0) == 40);
}

TEST_CASE("IncrementalSSSP diamond graph — alternate path", "[incremental_sssp]") {
    auto g = make_diamond_graph();

    // Block the short path edge 0-1
    std::unordered_set<uint64_t> blocked;
    blocked.insert(IncrementalSSSP::edge_key(0, 1));

    IncrementalSSSP engine(g, {0}, blocked);

    // Without 0-1, path 0→3 goes through 0→2→3 = 40
    REQUIRE(engine.dist(0, 3) == 40);

    // Restore 0-1 (weight 10) → path 0→1→3 = 20
    engine.restore_edge(0, 1, 10);

    REQUIRE(engine.dist(0, 1) == 10);
    REQUIRE(engine.dist(0, 3) == 20);
    REQUIRE(engine.dist(0, 3) == engine.dist_full(0, 3));
}

TEST_CASE("IncrementalSSSP build_local_graph from ArrayGraph", "[incremental_sssp]") {
    std::vector<Edge> edges = {
        {0, 1, 10}, {1, 0, 10},
        {1, 2, 20}, {2, 1, 20},
    };
    ArrayGraph ag(3, std::move(edges));
    auto lg = build_local_graph(ag);

    REQUIRE(lg.n_nodes == 3);
    REQUIRE(lg.adj[0].size() == 1);
    REQUIRE(lg.adj[0][0].to == 1);
    REQUIRE(lg.adj[0][0].weight == 10);
    REQUIRE(lg.adj[1].size() == 2);
}
