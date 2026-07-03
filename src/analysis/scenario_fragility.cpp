#include "gravel/analysis/scenario_fragility.h"
#include "gravel/simplify/bridges.h"
#include "gravel/core/geo_math.h"
#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

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

std::vector<double> hazard_edge_probabilities(
    const ArrayGraph& graph,
    const std::vector<std::pair<Polygon, double>>& zones,
    double baseline) {
    const uint32_t n = graph.node_count();
    const auto& offsets = graph.raw_offsets();
    const auto& targets = graph.raw_targets();
    const uint32_t m = static_cast<uint32_t>(targets.size());
    std::vector<double> probs(m, baseline);

    // Per-edge source (implicit from the CSR offset array).
    std::vector<uint32_t> src(m);
    for (uint32_t u = 0; u < n; ++u) {
        for (uint32_t e = offsets[u]; e < offsets[u + 1]; ++e) src[e] = u;
    }

    std::vector<char> tested(n, 0);
    std::vector<char> inside(n, 0);
    for (const auto& [polygon, prob] : zones) {
        if (polygon.vertices.empty()) continue;
        double min_lat = std::numeric_limits<double>::infinity();
        double max_lat = -std::numeric_limits<double>::infinity();
        double min_lon = std::numeric_limits<double>::infinity();
        double max_lon = -std::numeric_limits<double>::infinity();
        for (const Coord& v : polygon.vertices) {
            min_lat = std::min(min_lat, v.lat);
            max_lat = std::max(max_lat, v.lat);
            min_lon = std::min(min_lon, v.lon);
            max_lon = std::max(max_lon, v.lon);
        }
        std::fill(tested.begin(), tested.end(), 0);
        auto node_inside = [&](uint32_t node) -> bool {
            if (tested[node]) return inside[node] != 0;
            tested[node] = 1;
            auto c = graph.node_coordinate(node);
            bool r = c && c->lat >= min_lat && c->lat <= max_lat && c->lon >= min_lon &&
                     c->lon <= max_lon && point_in_polygon(*c, polygon.vertices);
            inside[node] = r ? 1 : 0;
            return r;
        };
        for (uint32_t e = 0; e < m; ++e) {
            if (prob > probs[e] && node_inside(src[e]) && node_inside(targets[e])) {
                probs[e] = prob;
            }
        }
    }
    return probs;
}

}  // namespace gravel
