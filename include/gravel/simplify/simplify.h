/// @file simplify.h
/// @brief Graph simplification pipeline with degradation estimation.
///
/// Reduces graph size through three composable, independently optional stages:
///
/// 1. **Degree-2 contraction** [graph-agnostic, lossless]:
///    Merges chains of degree-2 nodes into single edges. Preserves all shortest-path
///    distances exactly. Typical reduction: ~25-35% of nodes.
///
/// 2. **Edge category filtering** [generic predicate]:
///    Removes edges that don't pass a user-defined filter. OSM road class filtering
///    is a common specialization (see edge_labels.h). Typical reduction: 60-75% when
///    dropping residential/service roads.
///
/// 3. **CH-level pruning** [graph-agnostic, requires pre-built CH]:
///    Removes structurally unimportant nodes based on contraction hierarchy levels.
///    Bridges are always preserved. Typical reduction: 20-30% additional.
///
/// After simplification, an optional degradation estimation samples O-D pairs to
/// quantify accuracy loss: stretch factors, connectivity preservation, bridge safety.
///
/// @section usage Example
/// @code
/// auto ch = build_ch(*graph);
/// ShortcutIndex idx(ch);
///
/// SimplificationConfig config;
/// config.contract_degree2 = true;
/// config.ch_level_keep_fraction = 0.7;  // keep top 70% of CH hierarchy
/// config.estimate_degradation = true;
///
/// auto result = simplify_graph(*graph, &ch, &idx, config);
/// // result.degradation.median_stretch → e.g. 1.01 (1% longer routes)
/// // result.simplified_nodes → reduced node count
/// @endcode

#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/shortcut_index.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gravel {

/// Degradation metrics from comparing original vs simplified graph routing.
/// All stretch values are ratios ≥ 1.0 (1.0 = no degradation).
struct DegradationReport {
    uint32_t od_pairs_sampled = 0;

    // Stretch factor distribution: d_simplified / d_original
    double max_stretch = 0.0;
    double p99_stretch = 0.0;
    double p95_stretch = 0.0;
    double p90_stretch = 0.0;
    double median_stretch = 0.0;
    double mean_stretch = 0.0;

    // Connectivity preservation
    uint32_t pairs_connected_before = 0;   // reachable in original
    uint32_t pairs_connected_after = 0;    // reachable in simplified
    uint32_t pairs_disconnected = 0;       // reachable before, not after
    double connectivity_ratio = 0.0;       // after / before

    // Bridge safety
    uint32_t original_bridges = 0;
    uint32_t preserved_bridges = 0;
    bool all_bridges_preserved = true;

    // Per-stage breakdown
    struct StageReport {
        std::string stage_name;
        uint32_t nodes_before = 0, nodes_after = 0;
        uint32_t edges_before = 0, edges_after = 0;
    };
    std::vector<StageReport> stages;
};

/// Configuration for the simplification pipeline.
/// All stages are independently optional. Stages execute in order:
/// edge filtering → degree-2 contraction → CH-level pruning.
struct SimplificationConfig {
    /// Stage 1: Degree-2 chain contraction.
    /// Merges chains of degree-2 nodes into single edges.
    /// Error: exactly 0% (lossless). Works on any graph topology.
    bool contract_degree2 = true;

    /// Stage 2: Edge category filter.
    /// Generic predicate: given a CSR edge index, return true to KEEP the edge.
    /// Runs on the ORIGINAL graph's CSR indices (before any contraction).
    /// Use make_category_filter() from edge_labels.h for label-based filtering.
    std::function<bool(uint32_t edge_index)> edge_filter;

    /// Stage 3: CH-level pruning.
    /// Fraction of nodes to keep, by CH contraction level (top N%).
    /// 1.0 = keep all (disabled), 0.5 = keep top 50%, 0.3 = keep top 30%.
    /// Requires a pre-built CH (ch parameter must be non-null).
    double ch_level_keep_fraction = 1.0;

    /// Preserve bridges (edges whose removal disconnects the graph).
    /// When true, bridge endpoints are never pruned regardless of CH level.
    bool preserve_bridges = true;

    /// Run degradation estimation after simplification.
    bool estimate_degradation = true;

    /// Number of O-D pairs to sample for degradation estimation.
    uint32_t degradation_samples = 1000;

    /// Random seed for reproducible sampling.
    uint64_t seed = 42;
};

/// Result of graph simplification.
struct SimplificationResult {
    std::shared_ptr<ArrayGraph> graph;

    // Node ID mapping between simplified and original graph
    std::vector<NodeID> new_to_original;
    std::unordered_map<NodeID, NodeID> original_to_new;

    // Size metrics
    uint32_t original_nodes = 0, original_edges = 0;
    uint32_t simplified_nodes = 0, simplified_edges = 0;

    // Degradation metrics (populated if estimate_degradation was true)
    DegradationReport degradation;
};

/// Run the simplification pipeline.
///
/// @param graph   The original graph to simplify.
/// @param ch      Pre-built CH on the original graph. Required for CH-level pruning
///                and degradation estimation. Pass nullptr to skip those stages.
/// @param idx     ShortcutIndex for the CH. Required for degradation estimation.
///                Pass nullptr to skip degradation.
/// @param config  Pipeline configuration.
/// @return        Simplified graph with node mapping and degradation metrics.
SimplificationResult simplify_graph(
    const ArrayGraph& graph,
    const ContractionResult* ch = nullptr,
    const ShortcutIndex* idx = nullptr,
    SimplificationConfig config = {});

// --- Internal stage functions (exposed for testing) ---

/// Degree-2 chain contraction. Lossless.
/// @param bridge_endpoints      Set of node IDs that are bridge endpoints (never contracted).
/// @param boundary_protection   Additional nodes to protect (e.g., region boundary nodes).
///                              The union of both sets is protected from contraction.
SimplificationResult contract_degree2(
    const ArrayGraph& graph,
    const std::unordered_set<NodeID>& bridge_endpoints = {},
    const std::unordered_set<NodeID>& boundary_protection = {});

/// CH-level pruning. Requires pre-built CH.
SimplificationResult prune_by_ch_level(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    double keep_fraction,
    const std::unordered_set<NodeID>& bridge_endpoints = {});

/// Edge filtering by predicate.
SimplificationResult filter_edges(
    const ArrayGraph& graph,
    const std::function<bool(uint32_t)>& predicate);

}  // namespace gravel
