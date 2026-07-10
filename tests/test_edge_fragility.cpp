#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gravel/fragility/edge_fragility.h"
#include "gravel/simplify/bridges.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/core/array_graph.h"

#include <cmath>
#include <utility>
#include <vector>

using namespace gravel;
using Catch::Matchers::WithinAbs;

namespace {

void add_undirected(std::vector<Edge>& edges, NodeID u, NodeID v, Weight w) {
    edges.push_back({u, v, w});
    edges.push_back({v, u, w});
}

// CSR index of directed edge (u, v).
EdgeID at(const ArrayGraph& g, NodeID u, NodeID v) {
    const auto& off = g.raw_offsets();
    const auto& tgt = g.raw_targets();
    for (uint32_t e = off[u]; e < off[u + 1]; ++e) {
        if (tgt[e] == v) return e;
    }
    return 0;  // callers only ask for edges that exist
}

}  // namespace

TEST_CASE("edge_fragility: bridges, cut sizes, and path-inflation ratios", "[fragility]") {
    // Triangle {0,1,2} (a 2-edge-connected core) + chain 2-3-4 (two bridges).
    std::vector<Edge> edges;
    add_undirected(edges, 0, 1, 1.0);
    add_undirected(edges, 1, 2, 1.0);
    add_undirected(edges, 0, 2, 1.0);
    add_undirected(edges, 2, 3, 1.0);  // bridge: strands {3, 4}
    add_undirected(edges, 3, 4, 1.0);  // bridge: strands {4}
    ArrayGraph graph(5, std::move(edges));
    auto ch = build_ch(graph);
    ShortcutIndex idx(ch);

    const auto r = edge_fragility(ch, idx, graph);

    // Triangle edges: not bridges, strand nothing, and removing one forces the length-2
    // detour around the other two (ratio 2.0). Both directed halves carry the same value.
    for (auto [u, v] : std::vector<std::pair<NodeID, NodeID>>{{0, 1}, {1, 2}, {0, 2}}) {
        const EdgeID e = at(graph, u, v);
        REQUIRE(r.is_bridge[e] == 0);
        REQUIRE(r.stranded_count[e] == 0);
        REQUIRE_THAT(r.fragility_ratio[e], WithinAbs(2.0, 1e-6));
        REQUIRE_THAT(r.fragility_ratio[at(graph, v, u)], WithinAbs(2.0, 1e-6));
    }

    // Bridge 2-3: disconnecting it strands {3, 4} = 2 buses; ratio is infinite.
    const EdgeID e23 = at(graph, 2, 3);
    REQUIRE(r.is_bridge[e23] == 1);
    REQUIRE(r.stranded_count[e23] == 2);
    REQUIRE(std::isinf(r.fragility_ratio[e23]));
    REQUIRE(r.replacement_distance[e23] >= INF_WEIGHT);

    // Bridge 3-4: strands only {4} = 1; symmetric across the two directed halves.
    const EdgeID e34 = at(graph, 3, 4);
    REQUIRE(r.is_bridge[e34] == 1);
    REQUIRE(r.stranded_count[e34] == 1);
    REQUIRE(r.stranded_count[at(graph, 4, 3)] == 1);
    REQUIRE(std::isinf(r.fragility_ratio[e34]));
}

TEST_CASE("edge_fragility: compute_ratio=false skips CH queries but still sizes cuts", "[fragility]") {
    std::vector<Edge> edges;
    add_undirected(edges, 0, 1, 1.0);
    add_undirected(edges, 1, 2, 1.0);
    add_undirected(edges, 0, 2, 1.0);
    add_undirected(edges, 2, 3, 1.0);  // the only bridge: strands {3}
    ArrayGraph graph(4, std::move(edges));
    auto ch = build_ch(graph);
    ShortcutIndex idx(ch);

    EdgeFragilityConfig cfg;
    cfg.compute_ratio = false;  // stranded/bridge only
    const auto r = edge_fragility(ch, idx, graph, cfg);

    const EdgeID e23 = at(graph, 2, 3);
    REQUIRE(r.is_bridge[e23] == 1);
    REQUIRE(r.stranded_count[e23] == 1);
    // A triangle edge is still correctly a non-bridge with no cut.
    REQUIRE(r.is_bridge[at(graph, 0, 1)] == 0);
    REQUIRE(r.stranded_count[at(graph, 0, 1)] == 0);
}

TEST_CASE("bridge_edge_info: parallel (double-circuit) edges are not bridges", "[fragility][bridges]") {
    // chain 0-1 (single line) + 1-2 (a DOUBLE circuit: two parallel edges).
    std::vector<Edge> edges;
    add_undirected(edges, 0, 1, 1.0);   // single line
    add_undirected(edges, 1, 2, 1.0);   // circuit A
    add_undirected(edges, 1, 2, 1.0);   // circuit B (parallel to A)
    ArrayGraph graph(3, std::move(edges));

    const auto bi = bridge_edge_info(graph);
    // 0-1 disconnects node 0 -> bridge, strands 1. 1-2 has a twin -> redundant, not a bridge.
    REQUIRE(bi.is_bridge[at(graph, 0, 1)] == 1);
    REQUIRE(bi.cut_size[at(graph, 0, 1)] == 1);
    REQUIRE(bi.is_bridge[at(graph, 1, 2)] == 0);
    REQUIRE(bi.cut_size[at(graph, 1, 2)] == 0);
}

TEST_CASE("bridge_edge_info: cut sizes are per connected component", "[fragility][bridges]") {
    // Component A: 0-1-2-3 chain (4 nodes). Component B: 4-5 (2 nodes). All edges are bridges.
    std::vector<Edge> edges;
    add_undirected(edges, 0, 1, 1.0);   // A: strands {0} = 1
    add_undirected(edges, 1, 2, 1.0);   // A: strands {0,1} vs {2,3} = 2
    add_undirected(edges, 2, 3, 1.0);   // A: strands {3} = 1
    add_undirected(edges, 4, 5, 1.0);   // B: strands 1 (component has only 2 nodes)
    ArrayGraph graph(6, std::move(edges));

    const auto bi = bridge_edge_info(graph);
    REQUIRE(bi.cut_size[at(graph, 0, 1)] == 1);
    REQUIRE(bi.cut_size[at(graph, 1, 2)] == 2);
    REQUIRE(bi.cut_size[at(graph, 2, 3)] == 1);
    REQUIRE(bi.cut_size[at(graph, 4, 5)] == 1);
    for (auto [u, v] : std::vector<std::pair<NodeID, NodeID>>{{0, 1}, {1, 2}, {2, 3}, {4, 5}}) {
        REQUIRE(bi.is_bridge[at(graph, u, v)] == 1);
    }
}
