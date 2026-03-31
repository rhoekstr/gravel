/// @file analysis_context.h
/// @brief Cached analysis context for efficient repeated fragility computations.
///
/// AnalysisContext pre-computes and caches expensive shared state:
/// - Polygon-scoped subgraph (extracted once, reused across calls)
/// - Degree-2 simplified subgraph (lossless, default on)
/// - CH + ShortcutIndex on the analysis graph
/// - Bridge detection
/// - Entry point identification
///
/// All fragility analysis functions (county, location, progressive, scenario)
/// accept an optional AnalysisContext to avoid redundant computation.
/// Without a context, each call extracts its own subgraph from scratch.
///
/// Usage:
/// @code
/// // Build context once
/// AnalysisContextConfig ctx_cfg;
/// ctx_cfg.boundary = county_polygon;
/// ctx_cfg.simplify = true;  // degree-2 contraction (default, lossless)
/// auto ctx = build_analysis_context(full_graph, full_ch, full_idx, ctx_cfg);
///
/// // Reuse for multiple analyses
/// auto result1 = county_fragility_index(full_graph, full_ch, full_idx, cfg1, &ctx);
/// auto result2 = county_fragility_index(full_graph, full_ch, full_idx, cfg2, &ctx);
/// auto prog = progressive_fragility(full_graph, full_ch, full_idx, prog_cfg, &ctx);
/// @endcode

#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/core/subgraph.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/simplify/bridges.h"
#include "gravel/analysis/accessibility.h"
#include <memory>
#include <chrono>

namespace gravel {

struct AnalysisContextConfig {
    /// Polygon boundary for subgraph extraction.
    Polygon boundary;

    /// Apply degree-2 simplification to the subgraph (default: true).
    /// This is lossless — zero stretch factor, zero connectivity loss.
    /// Reduces node count by 30-50% on typical road networks.
    bool simplify = true;

    /// Build a local CH on the (simplified) subgraph.
    /// Enables local route queries without using the full-graph CH.
    /// Default: false (uses full-graph CH with original node IDs).
    bool build_local_ch = false;
};

struct AnalysisContextStats {
    double subgraph_extract_seconds = 0.0;
    double simplification_seconds = 0.0;
    double bridge_detection_seconds = 0.0;
    double entry_point_seconds = 0.0;
    double total_seconds = 0.0;
    uint32_t original_subgraph_nodes = 0;
    uint32_t original_subgraph_edges = 0;
    uint32_t analysis_nodes = 0;  // after simplification
    uint32_t analysis_edges = 0;
    double simplification_ratio = 1.0;  // analysis_nodes / original_subgraph_nodes
};

/// Pre-computed analysis context for a geographic region.
/// Built once, reused across county_fragility_index, location_fragility,
/// progressive_fragility, and scenario_fragility calls.
struct AnalysisContext {
    /// The extracted subgraph (within boundary).
    SubgraphResult raw_subgraph;

    /// The analysis graph: either raw_subgraph.graph (if simplify=false)
    /// or the degree-2 simplified version. This is what all metrics run on.
    std::shared_ptr<ArrayGraph> analysis_graph;

    /// Node mapping from analysis graph to original full graph.
    /// For simplified graphs, this composes: analysis → raw_subgraph → full.
    std::vector<NodeID> analysis_to_original;

    /// Bridges detected on the analysis graph.
    BridgeResult bridges;

    /// Entry points: analysis graph nodes with external neighbors in the full graph.
    std::vector<EntryPoint> entry_points;

    /// Build statistics for logging/profiling.
    AnalysisContextStats stats;

    /// Whether simplification was applied.
    bool simplified = false;

    /// Whether this context is valid (has been built).
    bool valid() const { return analysis_graph && analysis_graph->node_count() > 0; }
};

/// Build an analysis context for a geographic region.
/// This is the expensive step — do it once, then pass to all analysis functions.
AnalysisContext build_analysis_context(
    const ArrayGraph& full_graph,
    const ContractionResult& full_ch,
    const ShortcutIndex& full_idx,
    const AnalysisContextConfig& config);

}  // namespace gravel
