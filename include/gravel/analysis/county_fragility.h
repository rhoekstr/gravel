#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/core/subgraph.h"
#include "gravel/ch/contraction.h"
#include "gravel/simplify/bridges.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/fragility/fragility_result.h"
#include "gravel/analysis/betweenness.h"
#include "gravel/analysis/accessibility.h"
#include "gravel/analysis/analysis_context.h"
#include <vector>
#include <utility>

namespace gravel {

struct CountyFragilityWeights {
    double bridge_weight = 0.30;
    double connectivity_weight = 0.25;       // algebraic connectivity
    double accessibility_weight = 0.25;
    double fragility_weight = 0.20;          // sampled route fragility
};

struct CountyFragilityConfig {
    Polygon boundary;
    uint32_t betweenness_samples = 0;   // 0 = exact
    uint32_t od_sample_count = 100;     // O-D pairs for route fragility sampling
    uint64_t seed = 42;
    CountyFragilityWeights weights;     // composite scoring weights

    /// Optional per-node importance weights for O-D sampling.
    /// Indexed by ORIGINAL graph node IDs (before subgraph extraction).
    /// Weights for nodes outside the polygon boundary are ignored.
    /// Empty = uniform sampling.
    std::vector<double> node_weights;

    /// Optional edges to treat as blocked (removed) during analysis.
    /// Each pair is (u, v) in ORIGINAL graph node IDs.
    /// Used by progressive elimination and scenario fragility.
    std::vector<std::pair<NodeID, NodeID>> blocked_edges;

    /// Skip spectral metrics (algebraic_connectivity, kirchhoff_index) for speed.
    /// Used by progressive elimination's FAST_APPROXIMATE mode.
    bool skip_spectral = false;
};

struct CountyFragilityResult {
    double composite_index = 0.0;

    // Sub-metrics
    BridgeResult bridges;
    BetweennessResult betweenness;
    double algebraic_connectivity = 0.0;
    double kirchhoff_index_value = 0.0;
    AccessibilityResult accessibility;
    std::vector<FragilityResult> sampled_fragilities;

    /// Fragility ratio distribution across sampled O-D pairs.
    /// These capture the shape of the fragility distribution, not just the mean.
    /// A county where p90=1.1 but p99=15.0 has a few catastrophically fragile routes
    /// hidden by an otherwise resilient network — important for disaster planning.
    double fragility_p25 = 0.0;
    double fragility_p50 = 0.0;   ///< Median bottleneck ratio
    double fragility_p75 = 0.0;
    double fragility_p90 = 0.0;   ///< 90th percentile — "bad day" routes
    double fragility_p99 = 0.0;   ///< 99th percentile — worst-case routes

    // Subgraph info
    uint32_t subgraph_nodes = 0;
    uint32_t subgraph_edges = 0;
    uint32_t entry_point_count = 0;
};

/// Compute the county-level fragility index.
/// Extracts subgraph within polygon, runs all analysis metrics, produces composite score.
///
/// @param ctx  Optional pre-built AnalysisContext. When provided, skips subgraph
///             extraction, simplification, and bridge detection — reusing cached results.
///             This is the recommended path for progressive elimination (600+ calls)
///             and ensemble analysis (multiple seeds on same region).
CountyFragilityResult county_fragility_index(
    const ArrayGraph& full_graph,
    const ContractionResult& full_ch,
    const ShortcutIndex& full_idx,
    const CountyFragilityConfig& config,
    const AnalysisContext* ctx = nullptr);

}  // namespace gravel
