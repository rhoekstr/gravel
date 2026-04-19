#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/ch/blocked_ch_query.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/ch/ch_query.h"
#include "gravel/ch/contraction.h"
#include "gravel/core/dijkstra.h"
#include "gravel/validation/synthetic_graphs.h"
#include <memory>
#include <utility>
#include <vector>

using namespace gravel;
using Catch::Matchers::WithinAbs;

// Build a graph with one edge removed (for leave-one-out oracle).
static std::unique_ptr<ArrayGraph> remove_edge(const ArrayGraph& g, NodeID u, NodeID v) {
    std::vector<Edge> edges;
    for (NodeID src = 0; src < g.node_count(); ++src) {
        auto targets = g.outgoing_targets(src);
        auto weights = g.outgoing_weights(src);
        for (uint32_t i = 0; i < targets.size(); ++i) {
            if (src == u && targets[i] == v) continue;
            edges.push_back({src, targets[i], weights[i]});
        }
    }
    return std::make_unique<ArrayGraph>(g.node_count(), std::move(edges));
}

TEST_CASE("ShortcutIndex builds from CH", "[blocked_ch]") {
    auto graph = make_grid_graph(5, 5);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    // Should have some entries (grid graphs produce shortcuts)
    // Not all original edges appear in shortcuts, but some should
    REQUIRE(idx.size() > 0);
}

TEST_CASE("BlockedCHQuery matches unblocked when no edges blocked", "[blocked_ch]") {
    auto graph = make_grid_graph(5, 5);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    CHQuery normal(ch);
    BlockedCHQuery blocked(ch, idx, *graph);

    for (NodeID s = 0; s < 5; ++s) {
        for (NodeID t = 20; t < 25; ++t) {
            Weight normal_dist = normal.distance(s, t);
            Weight blocked_dist = blocked.distance_blocking(s, t, {});
            REQUIRE_THAT(normal_dist, WithinAbs(blocked_dist, 1e-9));
        }
    }
}

TEST_CASE("BlockedCHQuery on grid matches leave-one-out Dijkstra", "[blocked_ch]") {
    auto graph = make_grid_graph(5, 5);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    CHQuery query(ch);
    BlockedCHQuery blocked(ch, idx, *graph);

    NodeID s = 0, t = 24;  // top-left to bottom-right
    auto route = query.route(s, t);
    REQUIRE(route.distance < INF_WEIGHT);
    REQUIRE(route.path.size() >= 2);

    // For each edge on the primary path, block it and compare
    for (size_t i = 0; i + 1 < route.path.size(); ++i) {
        NodeID u = route.path[i];
        NodeID v = route.path[i + 1];

        Weight ch_blocked = blocked.distance_blocking(s, t, {{u, v}});

        // Oracle: Dijkstra on graph with edge removed
        auto modified = remove_edge(*graph, u, v);
        Weight oracle = dijkstra_pair(*modified, s, t);

        if (oracle >= INF_WEIGHT) {
            REQUIRE(ch_blocked >= INF_WEIGHT);
        } else {
            REQUIRE_THAT(ch_blocked, WithinAbs(oracle, 1e-6));
        }
    }
}

TEST_CASE("BlockedCHQuery returns INF for bridge edge", "[blocked_ch]") {
    // Chain: 0-1-2-3 — every edge is a bridge
    std::vector<Edge> edges;
    for (int i = 0; i < 3; ++i) {
        edges.push_back({(NodeID)i, (NodeID)(i + 1), 1.0});
        edges.push_back({(NodeID)(i + 1), (NodeID)i, 1.0});
    }
    ArrayGraph graph(4, std::move(edges));
    auto ch = build_ch(graph);
    ShortcutIndex idx(ch);
    BlockedCHQuery blocked(ch, idx, graph);

    // Block edge 1→2 — should disconnect 0 from 3
    Weight d = blocked.distance_blocking(0, 3, {{1, 2}});
    REQUIRE(d >= INF_WEIGHT);
}

TEST_CASE("Blocking non-path edge doesn't change distance", "[blocked_ch]") {
    auto graph = make_grid_graph(5, 5);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    CHQuery query(ch);
    BlockedCHQuery blocked(ch, idx, *graph);

    NodeID s = 0, t = 4;  // top-left to top-right
    Weight normal_dist = query.distance(s, t);

    // Block an edge far from the path (bottom row)
    Weight blocked_dist = blocked.distance_blocking(s, t, {{20, 21}});
    REQUIRE_THAT(normal_dist, WithinAbs(blocked_dist, 1e-9));
}

TEST_CASE("BlockedCHQuery on random graph matches leave-one-out", "[blocked_ch]") {
    auto graph = make_random_graph(100, 500, 1.0, 10.0, 42);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    CHQuery query(ch);
    BlockedCHQuery blocked(ch, idx, *graph);

    // Test 10 sampled pairs
    for (NodeID s = 0; s < 10; ++s) {
        NodeID t = 90 + s;
        auto route = query.route(s, t);
        if (route.distance >= INF_WEIGHT) continue;

        for (size_t i = 0; i + 1 < route.path.size(); ++i) {
            NodeID u = route.path[i];
            NodeID v = route.path[i + 1];

            // Block both directions (road blocked = blocked both ways)
            Weight ch_blocked = blocked.distance_blocking(s, t, {{u, v}, {v, u}});

            // Oracle: remove BOTH directions of the edge
            auto modified = remove_edge(*graph, u, v);
            auto modified2 = remove_edge(*modified, v, u);
            Weight oracle = dijkstra_pair(*modified2, s, t);

            if (oracle >= INF_WEIGHT) {
                REQUIRE(ch_blocked >= INF_WEIGHT);
            } else {
                INFO("s=" << s << " t=" << t << " blocked=(" << u << "," << v << ")"
                     << " ch_blocked=" << ch_blocked << " oracle=" << oracle);
                REQUIRE_THAT(ch_blocked, WithinAbs(oracle, 1e-6));
            }
        }
    }
}
