#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/core/hilbert_order.h"
#include "gravel/core/dijkstra.h"

using namespace gravel;

TEST_CASE("Hilbert permutation is valid bijection", "[hilbert]") {
    // Create a small graph with coordinates
    std::vector<uint32_t> offsets = {0, 1, 2, 3, 3};
    std::vector<NodeID> targets = {1, 2, 3};
    std::vector<Weight> weights = {1.0, 1.0, 1.0};
    std::vector<Coord> coords = {
        {35.0, -83.0}, {35.1, -82.9}, {35.2, -82.8}, {35.3, -82.7}
    };
    ArrayGraph g(std::move(offsets), std::move(targets), std::move(weights), std::move(coords));

    auto perm = hilbert_permutation(g);
    REQUIRE(perm.size() == 4);

    // Check it's a valid permutation (every old ID appears exactly once)
    std::vector<bool> seen(4, false);
    for (NodeID id : perm) {
        REQUIRE(id < 4);
        REQUIRE(!seen[id]);
        seen[id] = true;
    }
}

TEST_CASE("Hilbert returns empty for graph without coordinates", "[hilbert]") {
    std::vector<Edge> edges = {{0, 1, 1.0}, {1, 0, 1.0}};
    ArrayGraph g(2, std::move(edges));

    auto perm = hilbert_permutation(g);
    REQUIRE(perm.empty());
}

TEST_CASE("Reorder graph preserves Dijkstra distances", "[hilbert]") {
    // Build a small graph with coords: chain 0->1->2->3 with bidirectional
    std::vector<Edge> edges;
    for (int i = 0; i < 3; ++i) {
        edges.push_back({(NodeID)i, (NodeID)(i + 1), 1.0 + i});
        edges.push_back({(NodeID)(i + 1), (NodeID)i, 1.0 + i});
    }

    // Add coordinates
    std::vector<uint32_t> offsets;
    std::vector<NodeID> targets;
    std::vector<Weight> weights;

    // Build CSR manually from edge list
    ArrayGraph temp(4, std::move(edges));
    auto temp_off = temp.raw_offsets();
    auto temp_tgt = std::vector<NodeID>(temp.raw_targets().begin(), temp.raw_targets().end());
    auto temp_wgt = std::vector<Weight>(temp.raw_weights().begin(), temp.raw_weights().end());

    std::vector<Coord> coords = {
        {35.0, -83.0}, {35.1, -83.0}, {35.0, -82.9}, {35.1, -82.9}
    };

    ArrayGraph g(std::move(temp_off), std::move(temp_tgt), std::move(temp_wgt), std::move(coords));

    auto perm = hilbert_permutation(g);
    REQUIRE(!perm.empty());

    auto reordered = reorder_graph(g, perm);
    REQUIRE(reordered != nullptr);
    REQUIRE(reordered->node_count() == g.node_count());
    REQUIRE(reordered->edge_count() == g.edge_count());

    // Compute inverse permutation
    std::vector<NodeID> inv(4);
    for (NodeID i = 0; i < 4; ++i) inv[perm[i]] = i;

    // Check all-pairs distances are preserved
    for (NodeID s = 0; s < 4; ++s) {
        for (NodeID t = 0; t < 4; ++t) {
            Weight orig = dijkstra_pair(g, s, t);
            Weight reord = dijkstra_pair(*reordered, inv[s], inv[t]);
            REQUIRE_THAT(orig, Catch::Matchers::WithinAbs(reord, 1e-9));
        }
    }
}
