#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/geo/elevation.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace gravel {

// Closure risk tier: higher = more likely to be closed.
// Tier 0: Low elevation, major road — rarely closes
// Tier 1: Moderate elevation OR minor road — occasional closures
// Tier 2: High elevation AND secondary road — seasonal closures
// Tier 3: Very high elevation AND minor road — frequent/prolonged closures
enum class ClosureRiskTier : uint8_t {
    LOW = 0,
    MODERATE = 1,
    HIGH = 2,
    SEVERE = 3
};

struct ClosureRiskConfig {
    // Elevation thresholds (meters)
    double moderate_elevation = 900.0;     // ~3000 ft
    double high_elevation = 1200.0;        // ~4000 ft
    double severe_elevation = 1500.0;      // ~5000 ft

    // Road class hierarchy: class name -> rank (0=highest, like motorway)
    // Edges without a class mapping default to rank 5 (minor road).
    std::unordered_map<std::string, uint8_t> road_class_rank;

    // Default road class rankings if none provided
    static ClosureRiskConfig defaults() {
        ClosureRiskConfig c;
        c.road_class_rank = {
            {"motorway", 0}, {"motorway_link", 0},
            {"trunk", 1}, {"trunk_link", 1},
            {"primary", 2}, {"primary_link", 2},
            {"secondary", 3}, {"secondary_link", 3},
            {"tertiary", 4}, {"tertiary_link", 4},
            {"residential", 5}, {"unclassified", 5},
            {"service", 6}, {"living_street", 6}
        };
        return c;
    }
};

struct ClosureRiskData {
    std::vector<ClosureRiskTier> edge_tiers;  // one per directed edge

    // Fraction of edges at each tier
    double tier_fraction(ClosureRiskTier tier) const;

    // Maximum tier on a path (list of edge indices)
    ClosureRiskTier max_tier_on_path(const std::vector<uint32_t>& edge_indices) const;
};

// Classify closure risk for every edge based on elevation and road class.
// edge_labels: optional per-edge road class labels (from OSM highway tag).
//   If empty, all edges treated as unclassified (rank 5).
ClosureRiskData classify_closure_risk(
    const ArrayGraph& graph,
    const ElevationData& elevation,
    const std::vector<std::string>& edge_labels = {},
    ClosureRiskConfig config = ClosureRiskConfig::defaults());

// Compute a fragility weight multiplier for seasonal analysis.
// Higher-tier edges get inflated weights to simulate winter conditions.
// Returns a weight multiplier per edge (1.0 = no change, >1.0 = penalized).
std::vector<double> seasonal_weight_multipliers(
    const ClosureRiskData& risk,
    double tier1_multiplier = 1.0,    // moderate: no penalty
    double tier2_multiplier = 1.5,    // high: 50% penalty
    double tier3_multiplier = 3.0);   // severe: 3x penalty

}  // namespace gravel
