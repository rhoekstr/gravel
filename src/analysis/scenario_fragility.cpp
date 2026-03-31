#include "gravel/analysis/scenario_fragility.h"
#include "gravel/simplify/bridges.h"
#include "gravel/core/geo_math.h"
#include <unordered_set>

namespace gravel {

std::vector<std::pair<NodeID, NodeID>> edges_in_polygon(
    const ArrayGraph& graph,
    const Polygon& polygon) {

    std::vector<std::pair<NodeID, NodeID>> result;
    NodeID n = graph.node_count();

    // Pre-compute which nodes are inside the polygon
    std::vector<bool> inside(n, false);
    for (NodeID v = 0; v < n; ++v) {
        auto coord = graph.node_coordinate(v);
        if (coord && point_in_polygon(*coord, polygon.vertices)) {
            inside[v] = true;
        }
    }

    // Collect edges where both endpoints are inside
    for (NodeID u = 0; u < n; ++u) {
        if (!inside[u]) continue;
        auto targets = graph.outgoing_targets(u);
        for (NodeID v : targets) {
            if (inside[v]) {
                result.push_back({u, v});
            }
        }
    }

    return result;
}

ScenarioResult scenario_fragility(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const ScenarioConfig& config) {

    ScenarioResult result;

    // Determine blocked edges
    auto blocked = config.blocked_edges;
    if (blocked.empty() && !config.hazard_footprint.vertices.empty()) {
        blocked = edges_in_polygon(graph, config.hazard_footprint);
    }
    result.edges_blocked = static_cast<uint32_t>(blocked.size());

    // Check how many blocked edges are bridges
    auto bridges = find_bridges(graph);
    std::unordered_set<uint64_t> bridge_set;
    for (const auto& [u, v] : bridges.bridges) {
        bridge_set.insert(ContractionResult::pack_edge(u, v));
        bridge_set.insert(ContractionResult::pack_edge(v, u));
    }
    for (const auto& [u, v] : blocked) {
        if (bridge_set.count(ContractionResult::pack_edge(u, v))) {
            result.bridges_blocked++;
        }
    }
    // Deduplicate bridge count (each undirected bridge may appear twice in directed blocked list)
    result.bridges_blocked /= 2;

    // Step 1: Compute baseline fragility
    result.baseline = county_fragility_index(graph, ch, idx, config.baseline);

    // Step 2: Compute scenario fragility using BlockedCHQuery fast path.
    // Instead of rebuilding the graph and CH from scratch, we pass the
    // blocked edges through to county_fragility_index which uses
    // BlockedCHQuery internally for route fragility queries.
    CountyFragilityConfig scenario_config = config.baseline;
    scenario_config.blocked_edges = blocked;
    result.scenario = county_fragility_index(graph, ch, idx, scenario_config);

    // Step 3: Compute deltas
    result.delta_composite = result.scenario.composite_index - result.baseline.composite_index;
    if (result.baseline.composite_index > 0.0) {
        result.relative_change = result.delta_composite / result.baseline.composite_index;
    }

    return result;
}

}  // namespace gravel
