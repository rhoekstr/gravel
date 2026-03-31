/// @file scenario_fragility.h
/// @brief Event-conditional fragility analysis (hazard footprint scenarios).
///
/// For disaster research, the relevant question is never "how fragile is this
/// network?" but "how fragile does this network become when the disaster removes
/// edge set S?" This module answers that by computing county fragility on a
/// degraded network and reporting the delta versus baseline.
///
/// The workflow:
/// 1. Define a hazard footprint (flood polygon, wildfire perimeter, etc.)
/// 2. Convert the footprint to a set of blocked edges via edges_in_polygon()
/// 3. Compute fragility on the degraded network
/// 4. Compare against baseline to measure the event's impact
///
/// This directly supports intersecting FEMA disaster declarations with network
/// fragility — the core of the disaster sociology analytical framework.
///
/// @section usage Example
/// @code
/// // Define a flood polygon
/// Polygon flood_zone;
/// flood_zone.vertices = {{35.4, -83.5}, {35.4, -83.3}, {35.5, -83.3}, {35.5, -83.5}, {35.4, -83.5}};
///
/// // Find affected edges
/// auto blocked = edges_in_polygon(graph, flood_zone);
///
/// // Run scenario analysis
/// ScenarioConfig cfg;
/// cfg.baseline.boundary = county_polygon;
/// cfg.blocked_edges = blocked;
/// auto result = scenario_fragility(graph, ch, idx, cfg);
/// // result.delta_composite → how much fragility increased
/// @endcode

#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/core/subgraph.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/analysis/county_fragility.h"
#include <vector>

namespace gravel {

struct ScenarioConfig {
    /// Baseline county fragility configuration (polygon, samples, weights).
    CountyFragilityConfig baseline;

    /// Edges to treat as blocked in the scenario.
    /// Each pair is (u, v) in original graph node IDs.
    std::vector<std::pair<NodeID, NodeID>> blocked_edges;

    /// Alternative: provide a hazard polygon and auto-detect blocked edges.
    /// If non-empty AND blocked_edges is empty, edges_in_polygon() is called.
    Polygon hazard_footprint;
};

struct ScenarioResult {
    CountyFragilityResult baseline;
    CountyFragilityResult scenario;

    /// Absolute change: scenario.composite_index - baseline.composite_index.
    /// Positive = fragility increased (expected for most hazard scenarios).
    double delta_composite = 0.0;

    /// Relative change: delta / baseline (if baseline > 0).
    /// A value of 0.25 means 25% increase in fragility.
    double relative_change = 0.0;

    /// Number of edges blocked in the scenario.
    uint32_t edges_blocked = 0;

    /// Number of bridges among the blocked edges (critical failures).
    uint32_t bridges_blocked = 0;
};

/// Run baseline + scenario fragility comparison.
///
/// Computes county fragility on the original network (baseline), then removes
/// the blocked edges, rebuilds the CH, and recomputes fragility (scenario).
/// Reports the delta between the two.
ScenarioResult scenario_fragility(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const ScenarioConfig& config);

/// Find all graph edges whose BOTH endpoints fall within a polygon.
/// Returns edges as (u, v) pairs in original graph IDs.
/// Useful for converting a hazard footprint (flood zone, wildfire perimeter)
/// into a set of blocked edges for scenario_fragility().
std::vector<std::pair<NodeID, NodeID>> edges_in_polygon(
    const ArrayGraph& graph,
    const Polygon& polygon);

}  // namespace gravel
