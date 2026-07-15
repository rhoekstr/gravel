#include <catch2/catch_test_macros.hpp>

#include "gravel/simplify/simplify.h"
#include "gravel/simplify/bridges.h"
#include "gravel/core/array_graph.h"

#include <utility>
#include <vector>

using namespace gravel;

TEST_CASE("condense_parallel_edges collapses parallels and flips bridge semantics", "[simplify]") {
    // 0-1 is a single line; 1-2 is a DOUBLE circuit (two parallel edges each way, weights 3 and 5).
    std::vector<Edge> edges = {
        {0, 1, 1.0}, {1, 0, 1.0},   // single span
        {1, 2, 3.0}, {2, 1, 3.0},   // circuit A
        {1, 2, 5.0}, {2, 1, 5.0},   // circuit B (parallel to A)
    };
    ArrayGraph g(3, std::move(edges));

    // Before condensing: the doubled 1-2 span is genuine redundancy -> NOT a bridge; only 0-1 is.
    const auto before = find_bridges(g);
    REQUIRE(before.bridges.size() == 1);
    REQUIRE(before.bridges[0] == std::make_pair(NodeID{0}, NodeID{1}));

    // Condense (default MIN): each parallel (u,v) collapses to one edge carrying the min weight.
    const ArrayGraph c = condense_parallel_edges(g);
    REQUIRE(c.node_count() == 3);
    REQUIRE(c.edge_count() == 4);          // was 6; the two 1<->2 parallels merged to one each way

    const auto tgt = c.outgoing_targets(1);
    const auto wt = c.outgoing_weights(1);
    for (size_t i = 0; i < tgt.size(); ++i) {
        if (tgt[i] == 2) REQUIRE(wt[i] == 3.0);   // min(3, 5)
    }

    // After condensing: 1-2 is now a single edge -> BOTH spans are bridges.
    const auto after = find_bridges(c);
    REQUIRE(after.bridges.size() == 2);
}

TEST_CASE("condense_parallel_edges weight policies", "[simplify]") {
    std::vector<Edge> edges = {{0, 1, 2.0}, {0, 1, 4.0}, {0, 1, 6.0}};  // three parallels 0->1
    ArrayGraph g(2, std::move(edges));

    REQUIRE(condense_parallel_edges(g).edge_count() == 1);  // 3 parallels -> 1
    REQUIRE(condense_parallel_edges(g, ParallelWeightPolicy::MIN).outgoing_weights(0)[0] == 2.0);
    REQUIRE(condense_parallel_edges(g, ParallelWeightPolicy::MAX).outgoing_weights(0)[0] == 6.0);
    REQUIRE(condense_parallel_edges(g, ParallelWeightPolicy::MEAN).outgoing_weights(0)[0] == 4.0);
}
