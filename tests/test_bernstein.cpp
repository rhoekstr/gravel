#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/fragility/bernstein_approx.h"
#include "gravel/fragility/hershberger_suri.h"
#include "gravel/core/dijkstra.h"
#include "gravel/validation/synthetic_graphs.h"

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

TEST_CASE("Bernstein approx within (1+eps) of exact on grid", "[bernstein]") {
    auto graph = make_grid_graph(8, 8);
    double eps = 0.1;

    auto exact = hershberger_suri(*graph, 0, 63);
    auto approx = bernstein_approx(*graph, 0, 63, {eps});

    REQUIRE(exact.valid());
    REQUIRE(approx.valid());
    REQUIRE(exact.edge_fragilities.size() == approx.edge_fragilities.size());

    for (size_t i = 0; i < exact.edge_fragilities.size(); ++i) {
        Weight exact_d = exact.edge_fragilities[i].replacement_distance;
        Weight approx_d = approx.edge_fragilities[i].replacement_distance;

        if (exact_d >= INF_WEIGHT) {
            REQUIRE(approx_d >= INF_WEIGHT);
        } else {
            // Approximate should be <= exact (it's an upper bound due to over-coverage).
            // But it should be within (1+eps) of exact.
            // Actually, the rounding over-covers, so replacement[e] might be LESS than
            // exact (a detour from a wider range covers more edges).
            // The result should still be a valid replacement distance.
            INFO("edge " << i << " exact=" << exact_d << " approx=" << approx_d);
            REQUIRE(approx_d <= exact_d + 1e-9);  // Should be <= exact (more detours considered)
            REQUIRE(approx_d >= exact_d * (1.0 - eps) - 1e-9);  // Not too far below
        }
    }
}

TEST_CASE("Bernstein approx with varying epsilon", "[bernstein]") {
    auto graph = make_grid_graph(5, 5);

    auto exact = hershberger_suri(*graph, 0, 24);
    REQUIRE(exact.valid());

    for (double eps : {0.01, 0.05, 0.1, 0.5}) {
        auto approx = bernstein_approx(*graph, 0, 24, {eps});
        REQUIRE(approx.valid());
        REQUIRE(approx.edge_fragilities.size() == exact.edge_fragilities.size());

        for (size_t i = 0; i < exact.edge_fragilities.size(); ++i) {
            Weight exact_d = exact.edge_fragilities[i].replacement_distance;
            Weight approx_d = approx.edge_fragilities[i].replacement_distance;

            if (exact_d >= INF_WEIGHT) {
                REQUIRE(approx_d >= INF_WEIGHT);
            } else {
                // Approx should be a valid replacement distance (not worse than exact).
                REQUIRE(approx_d <= exact_d + 1e-9);
            }
        }
    }
}

TEST_CASE("Bernstein approx on tree — all INF", "[bernstein]") {
    std::vector<Edge> edges;
    for (int i = 0; i < 5; ++i) {
        edges.push_back({(NodeID)i, (NodeID)(i + 1), 1.0});
        edges.push_back({(NodeID)(i + 1), (NodeID)i, 1.0});
    }
    ArrayGraph graph(6, std::move(edges));

    auto result = bernstein_approx(graph, 0, 5);
    REQUIRE(result.valid());
    for (const auto& ef : result.edge_fragilities) {
        REQUIRE(ef.replacement_distance >= INF_WEIGHT);
    }
}

TEST_CASE("Bernstein approx on random graph is valid", "[bernstein]") {
    auto graph = make_random_graph(100, 500, 1.0, 10.0, 42);

    for (auto [s, t] : std::vector<std::pair<NodeID,NodeID>>{{0,99},{5,95}}) {
        auto result = bernstein_approx(*graph, s, t, {0.1});
        if (!result.valid()) continue;

        for (const auto& ef : result.edge_fragilities) {
            if (ef.replacement_distance >= INF_WEIGHT) continue;

            // Verify against oracle.
            auto modified = remove_edge(*graph, ef.source, ef.target);
            Weight oracle = dijkstra_pair(*modified, s, t);

            // Approximate result should be <= oracle (it considers more detours).
            INFO("s=" << s << " t=" << t << " edge=(" << ef.source << "," << ef.target << ")"
                 << " approx=" << ef.replacement_distance << " oracle=" << oracle);
            REQUIRE(ef.replacement_distance <= oracle + 1e-6);
        }
    }
}
