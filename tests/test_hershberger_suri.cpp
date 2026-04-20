#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/fragility/hershberger_suri.h"
#include "gravel/fragility/route_fragility.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/ch/contraction.h"
#include "gravel/core/dijkstra.h"
#include "gravel/validation/synthetic_graphs.h"
#include <memory>
#include <utility>
#include <vector>

using namespace gravel;
using Catch::Matchers::WithinAbs;

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

TEST_CASE("Hershberger-Suri on grid matches leave-one-out Dijkstra", "[hs]") {
    auto graph = make_grid_graph(5, 5);

    auto result = hershberger_suri(*graph, 0, 24);
    REQUIRE(result.valid());
    REQUIRE(result.primary_path.size() >= 2);

    for (const auto& ef : result.edge_fragilities) {
        auto modified = remove_edge(*graph, ef.source, ef.target);
        Weight oracle = dijkstra_pair(*modified, 0, 24);

        if (oracle >= INF_WEIGHT) {
            REQUIRE(ef.replacement_distance >= INF_WEIGHT);
        } else {
            REQUIRE_THAT(ef.replacement_distance, WithinAbs(oracle, 1e-6));
        }
    }
}

TEST_CASE("Hershberger-Suri on tree - all INF", "[hs]") {
    std::vector<Edge> edges;
    for (int i = 0; i < 5; ++i) {
        edges.push_back({(NodeID)i, (NodeID)(i + 1), 1.0});
        edges.push_back({(NodeID)(i + 1), (NodeID)i, 1.0});
    }
    ArrayGraph graph(6, std::move(edges));

    auto result = hershberger_suri(graph, 0, 5);
    REQUIRE(result.valid());
    for (const auto& ef : result.edge_fragilities) {
        REQUIRE(ef.replacement_distance >= INF_WEIGHT);
    }
}

TEST_CASE("Hershberger-Suri matches route_fragility on grid", "[hs]") {
    auto graph = make_grid_graph(8, 8);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    NodeID s = 0, t = 63;
    auto hs_result = hershberger_suri(*graph, s, t);
    auto ch_result = route_fragility(ch, idx, *graph, s, t);

    REQUIRE(hs_result.valid());
    REQUIRE(ch_result.valid());
    REQUIRE_THAT(hs_result.primary_distance, WithinAbs(ch_result.primary_distance, 1e-9));

    // Both should find the same path (assuming deterministic Dijkstra/CH).
    // Compare replacement distances for each path edge against the leave-one-out oracle.
    for (const auto& ef : hs_result.edge_fragilities) {
        auto modified = remove_edge(*graph, ef.source, ef.target);
        Weight oracle = dijkstra_pair(*modified, s, t);
        if (oracle >= INF_WEIGHT) {
            REQUIRE(ef.replacement_distance >= INF_WEIGHT);
        } else {
            REQUIRE_THAT(ef.replacement_distance, WithinAbs(oracle, 1e-6));
        }
    }
}

TEST_CASE("Hershberger-Suri on random graph matches leave-one-out", "[hs]") {
    auto graph = make_random_graph(100, 500, 1.0, 10.0, 42);

    std::vector<std::pair<NodeID, NodeID>> pairs = {{0, 99}, {5, 95}, {10, 90}};

    for (auto [s, t] : pairs) {
        auto result = hershberger_suri(*graph, s, t);
        if (!result.valid()) continue;

        for (const auto& ef : result.edge_fragilities) {
            auto modified = remove_edge(*graph, ef.source, ef.target);
            Weight oracle = dijkstra_pair(*modified, s, t);

            if (oracle >= INF_WEIGHT) {
                REQUIRE(ef.replacement_distance >= INF_WEIGHT);
            } else {
                INFO("s=" << s << " t=" << t << " edge=(" << ef.source << "," << ef.target << ")"
                     << " hs=" << ef.replacement_distance << " oracle=" << oracle);
                REQUIRE_THAT(ef.replacement_distance, WithinAbs(oracle, 1e-6));
            }
        }
    }
}
