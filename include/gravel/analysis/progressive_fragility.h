/// @file progressive_fragility.h
/// @brief Progressive elimination fragility — multi-edge failure resilience analysis.
///
/// Single-edge fragility answers "what if one road closes?" Progressive elimination
/// answers "how quickly does this network fall apart as roads close one after another?"
///
/// The output is a **degradation curve**: composite fragility indexed by k (number of
/// edges removed). The curve shape reveals whether a network is gracefully degrading
/// (linear rise), hiding a breaking point (flat then sharp jump), or dominated by a
/// single bottleneck (steep initial rise then plateau).
///
/// Three selection strategies model different failure assumptions:
/// - **GREEDY_FRAGILITY**: worst-case adversarial removal (which edges hurt most?)
/// - **GREEDY_BETWEENNESS**: high-traffic corridor failure (what if busy roads fail first?)
/// - **MONTE_CARLO**: stochastic random failure (how robust to random disruption?)
///
/// For cross-county comparative research, Monte Carlo with auc_excess as the dependent
/// variable is recommended. It makes no failure mechanism assumptions and produces
/// distributional outputs suitable for regression.

#pragma once
#include "gravel/analysis/county_fragility.h"
#include "gravel/analysis/betweenness.h"
#include "gravel/core/edge_sampler.h"
#include "gravel/core/subgraph.h"
#include <functional>
#include <optional>
#include <vector>
#include <utility>

namespace gravel {

enum class SelectionStrategy {
    GREEDY_FRAGILITY,     ///< Worst-case: remove edge causing max fragility increase per step
    GREEDY_BETWEENNESS,   ///< High-traffic: remove highest-betweenness edges first
    MONTE_CARLO,          ///< Stochastic: random removal with N trials per k level
};

enum class RecomputeMode {
    FULL_RECOMPUTE,       ///< Exact: full county_fragility_index per candidate (default)
    FAST_APPROXIMATE,     ///< Skip spectral metrics per candidate (faster, approximate)
};

struct ProgressiveFragilityConfig {
    /// Base county fragility configuration (polygon, OD samples, weights).
    CountyFragilityConfig base_config;

    /// Edge selection strategy.
    SelectionStrategy selection_strategy = SelectionStrategy::MONTE_CARLO;

    /// Maximum edges to remove (absolute count).
    /// If k_max_fraction is > 0, this field is ignored.
    uint32_t k_max = 5;

    /// Set k_max as a fraction of subgraph edges, e.g. 0.01 = 1% of edges.
    /// When > 0, overrides k_max. Convenient for comparing networks of different sizes.
    float k_max_fraction = 0.0f;

    // --- Monte Carlo options ---
    uint32_t monte_carlo_runs = 50;   ///< Independent trials per k level
    uint64_t base_seed = 42;          ///< Trial i uses seed base_seed + i

    /// Optional: restrict random edge pool (e.g., only bridges, only a road class).
    /// nullptr = all subgraph edges are candidates.
    std::function<bool(uint32_t)> edge_pool_filter;

    /// Optional: use EdgeSampler for candidate edge selection.
    /// When set, candidate edges are sampled from the subgraph using this config
    /// instead of using all subgraph edges. The edge_pool_filter (above) is still
    /// applied as a pre-filter before sampling.
    std::optional<SamplerConfig> sampler_config;

    // --- Greedy fragility options ---
    RecomputeMode recompute_mode = RecomputeMode::FULL_RECOMPUTE;

    // --- Greedy betweenness options ---
    /// If non-null, skip betweenness recomputation and use these scores.
    const BetweennessResult* precomputed_betweenness = nullptr;

    // --- Threshold detection ---
    double critical_k_threshold = 0.7;  ///< Fragility level that defines "critical"
    double jump_detection_delta = 0.15; ///< Min step increase (as fraction of f(0)) to flag as jump
};

/// Per-k-level statistics on the degradation curve.
struct KLevelResult {
    uint32_t k = 0;

    /// Mean composite fragility at this k.
    /// Single value for greedy strategies, mean across MC runs for Monte Carlo.
    double mean_composite = 0.0;

    // --- Monte Carlo only (zero/empty for greedy) ---
    double std_composite = 0.0;
    double p25 = 0.0, p50 = 0.0, p75 = 0.0, p90 = 0.0;
    double iqr = 0.0;                              ///< p75 - p25
    double fraction_disconnected = 0.0;             ///< Fraction of MC runs with INF distance
    std::vector<double> run_values;                 ///< All N composite values

    // --- Greedy only (invalid for MC) ---
    std::pair<NodeID, NodeID> removed_edge = {INVALID_NODE, INVALID_NODE};
};

/// Full result of progressive elimination analysis.
struct ProgressiveFragilityResult {
    /// Degradation curve: curve[0]=baseline, curve[k]=result after k removals.
    /// Length = k_max_used + 1.
    std::vector<KLevelResult> curve;

    /// Area under curve metrics for cross-subgraph comparison.
    double auc_raw = 0.0;         ///< Sum of mean_composite[k] for k=0..K_max
    double auc_normalized = 0.0;  ///< auc_raw / (k_max + 1)
    double auc_excess = 0.0;      ///< (auc_raw - baseline*(k_max+1)) / k_max

    /// Threshold detection: smallest k where mean_composite > critical_k_threshold.
    int32_t critical_k = -1;      ///< -1 if threshold never exceeded

    /// Discontinuity detection: sharp jump in curve.
    bool jump_detected = false;
    int32_t jump_at_k = -1;       ///< k where jump occurred
    double jump_magnitude = 0.0;  ///< Size of the jump

    /// Ordered removal sequence (greedy strategies only, empty for MC).
    std::vector<std::pair<NodeID, NodeID>> removal_sequence;

    /// Metadata
    SelectionStrategy strategy_used = SelectionStrategy::MONTE_CARLO;
    uint32_t subgraph_nodes = 0;
    uint32_t subgraph_edges = 0;
    uint32_t k_max_used = 0;
};

/// Run progressive elimination fragility analysis.
///
/// Extracts the subgraph within base_config.boundary, then progressively removes
/// edges according to the selection strategy, computing composite fragility at
/// each step to build the degradation curve.
ProgressiveFragilityResult progressive_fragility(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const ProgressiveFragilityConfig& config);

// --- Internal strategy functions (exposed for testing) ---

ProgressiveFragilityResult run_monte_carlo(
    const ArrayGraph& graph, const ContractionResult& ch, const ShortcutIndex& idx,
    const ProgressiveFragilityConfig& config,
    const std::vector<std::pair<NodeID, NodeID>>& candidate_edges,
    const SubgraphResult& sub,
    double baseline_composite);

ProgressiveFragilityResult run_greedy_betweenness(
    const ArrayGraph& graph, const ContractionResult& ch, const ShortcutIndex& idx,
    const ProgressiveFragilityConfig& config,
    const std::vector<std::pair<NodeID, NodeID>>& candidate_edges,
    const SubgraphResult& subgraph,
    double baseline_composite);

ProgressiveFragilityResult run_greedy_fragility(
    const ArrayGraph& graph, const ContractionResult& ch, const ShortcutIndex& idx,
    const ProgressiveFragilityConfig& config,
    const std::vector<std::pair<NodeID, NodeID>>& candidate_edges,
    double baseline_composite);

}  // namespace gravel
