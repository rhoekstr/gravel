#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/fragility/route_fragility.h"
#include "gravel/core/dijkstra.h"
#include "gravel/ch/contraction.h"
#include "gravel/validation/synthetic_graphs.h"

using namespace gravel;
using Catch::Matchers::WithinAbs;

// Build a graph with one edge removed.
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

TEST_CASE("route_fragility on grid matches leave-one-out Dijkstra", "[fragility]") {
    auto graph = make_grid_graph(5, 5);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    auto result = route_fragility(ch, idx, *graph, 0, 24);
    REQUIRE(result.valid());
    REQUIRE(result.primary_path.size() >= 2);
    REQUIRE(result.edge_fragilities.size() == result.primary_path.size() - 1);

    for (size_t i = 0; i < result.edge_fragilities.size(); ++i) {
        const auto& ef = result.edge_fragilities[i];
        REQUIRE(ef.source == result.primary_path[i]);
        REQUIRE(ef.target == result.primary_path[i + 1]);

        // Oracle: Dijkstra on graph with edge removed
        auto modified = remove_edge(*graph, ef.source, ef.target);
        Weight oracle = dijkstra_pair(*modified, 0, 24);

        if (oracle >= INF_WEIGHT) {
            REQUIRE(ef.replacement_distance >= INF_WEIGHT);
        } else {
            REQUIRE_THAT(ef.replacement_distance, WithinAbs(oracle, 1e-6));
        }
        REQUIRE(ef.fragility_ratio >= 1.0);
    }
}

TEST_CASE("route_fragility on tree — all edges critical", "[fragility]") {
    // Chain: 0-1-2-3-4-5
    std::vector<Edge> edges;
    for (int i = 0; i < 5; ++i) {
        edges.push_back({(NodeID)i, (NodeID)(i + 1), 1.0});
        edges.push_back({(NodeID)(i + 1), (NodeID)i, 1.0});
    }
    ArrayGraph graph(6, std::move(edges));
    auto ch = build_ch(graph);
    ShortcutIndex idx(ch);

    auto result = route_fragility(ch, idx, graph, 0, 5);
    REQUIRE(result.valid());
    REQUIRE_THAT(result.primary_distance, WithinAbs(5.0, 1e-9));

    // Every edge is a bridge — all replacement distances should be INF
    for (const auto& ef : result.edge_fragilities) {
        REQUIRE(ef.replacement_distance >= INF_WEIGHT);
        REQUIRE(std::isinf(ef.fragility_ratio));
    }
}

TEST_CASE("fragility_ratio is always >= 1.0", "[fragility]") {
    auto graph = make_grid_graph(8, 8);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    auto result = route_fragility(ch, idx, *graph, 0, 63);
    REQUIRE(result.valid());

    for (const auto& ef : result.edge_fragilities) {
        REQUIRE(ef.fragility_ratio >= 1.0);
    }
}

TEST_CASE("bottleneck_index returns most fragile edge", "[fragility]") {
    auto graph = make_grid_graph(5, 5);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    auto result = route_fragility(ch, idx, *graph, 0, 24);
    REQUIRE(result.valid());
    REQUIRE(!result.edge_fragilities.empty());

    size_t bi = result.bottleneck_index();
    double max_ratio = result.edge_fragilities[bi].fragility_ratio;
    for (const auto& ef : result.edge_fragilities) {
        REQUIRE(max_ratio >= ef.fragility_ratio);
    }
}

TEST_CASE("batch_fragility matches single-pair results", "[fragility]") {
    auto graph = make_grid_graph(5, 5);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    std::vector<std::pair<NodeID, NodeID>> pairs = {{0, 24}, {4, 20}, {12, 12}};
    auto batch = batch_fragility(ch, idx, *graph, pairs);
    REQUIRE(batch.size() == 3);

    for (size_t p = 0; p < pairs.size(); ++p) {
        auto single = route_fragility(ch, idx, *graph, pairs[p].first, pairs[p].second);
        REQUIRE_THAT(batch[p].primary_distance, WithinAbs(single.primary_distance, 1e-9));
        REQUIRE(batch[p].edge_fragilities.size() == single.edge_fragilities.size());
        for (size_t i = 0; i < single.edge_fragilities.size(); ++i) {
            REQUIRE_THAT(batch[p].edge_fragilities[i].replacement_distance,
                         WithinAbs(single.edge_fragilities[i].replacement_distance, 1e-9));
        }
    }
}

TEST_CASE("route_fragility on random graph matches leave-one-out", "[fragility]") {
    auto graph = make_random_graph(100, 500, 1.0, 10.0, 42);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    // Test a few pairs
    std::vector<std::pair<NodeID, NodeID>> pairs = {{0, 99}, {5, 95}, {10, 90}};

    for (auto [s, t] : pairs) {
        auto result = route_fragility(ch, idx, *graph, s, t);
        if (!result.valid()) continue;

        for (const auto& ef : result.edge_fragilities) {
            auto modified = remove_edge(*graph, ef.source, ef.target);
            Weight oracle = dijkstra_pair(*modified, s, t);

            if (oracle >= INF_WEIGHT) {
                REQUIRE(ef.replacement_distance >= INF_WEIGHT);
            } else {
                INFO("s=" << s << " t=" << t << " edge=(" << ef.source << "," << ef.target << ")"
                     << " replacement=" << ef.replacement_distance << " oracle=" << oracle);
                REQUIRE_THAT(ef.replacement_distance, WithinAbs(oracle, 1e-6));
            }
        }
    }
}
