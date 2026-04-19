#include "gravel/analysis/progressive_fragility.h"
#include "gravel/core/subgraph.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>

namespace gravel {

namespace {

// Collect all directed edges in a subgraph as (original_u, original_v) pairs
std::vector<std::pair<NodeID, NodeID>> collect_subgraph_edges(
    const ArrayGraph& subgraph,
    const std::vector<NodeID>& new_to_original) {

    std::vector<std::pair<NodeID, NodeID>> edges;
    for (NodeID u = 0; u < subgraph.node_count(); ++u) {
        auto targets = subgraph.outgoing_targets(u);
        for (NodeID v : targets) {
            edges.push_back({new_to_original[u], new_to_original[v]});
        }
    }
    return edges;
}

// Compute AUC metrics and threshold detection on a completed curve
void compute_curve_metrics(ProgressiveFragilityResult& result,
                            const ProgressiveFragilityConfig& config) {
    if (result.curve.empty()) return;

    double baseline = result.curve[0].mean_composite;
    uint32_t k_max = result.k_max_used;

    // AUC
    result.auc_raw = 0.0;
    for (const auto& level : result.curve) {
        result.auc_raw += level.mean_composite;
    }
    result.auc_normalized = result.auc_raw / (k_max + 1);
    result.auc_excess = (k_max > 0) ?
        (result.auc_raw - baseline * (k_max + 1)) / k_max : 0.0;

    // Critical k: first k where mean_composite > threshold
    result.critical_k = -1;
    for (uint32_t k = 1; k <= k_max; ++k) {
        if (result.curve[k].mean_composite > config.critical_k_threshold) {
            result.critical_k = static_cast<int32_t>(k);
            break;
        }
    }

    // Jump detection: step delta > jump_detection_delta * baseline
    result.jump_detected = false;
    result.jump_at_k = -1;
    result.jump_magnitude = 0.0;
    double jump_threshold = config.jump_detection_delta * std::max(baseline, 0.01);

    for (uint32_t k = 1; k <= k_max; ++k) {
        double delta = result.curve[k].mean_composite - result.curve[k - 1].mean_composite;
        if (delta > jump_threshold && delta > result.jump_magnitude) {
            result.jump_detected = true;
            result.jump_at_k = static_cast<int32_t>(k);
            result.jump_magnitude = delta;
        }
    }
}

// ---------------------------------------------------------------------------
// Config validation
// ---------------------------------------------------------------------------
void validate_config(const ProgressiveFragilityConfig& config) {
    // Boundary must have at least 3 vertices to form a polygon
    if (config.base_config.boundary.vertices.size() < 3) {
        throw std::invalid_argument(
            "progressive_fragility: boundary polygon must have at least 3 vertices");
    }

    // k_max or k_max_fraction must request at least 1 removal
    if (config.k_max == 0 && config.k_max_fraction <= 0.0f) {
        throw std::invalid_argument(
            "progressive_fragility: k_max must be > 0 or k_max_fraction must be > 0");
    }

    // Monte Carlo needs at least 1 run
    if (config.selection_strategy == SelectionStrategy::MONTE_CARLO &&
        config.monte_carlo_runs == 0) {
        throw std::invalid_argument(
            "progressive_fragility: monte_carlo_runs must be > 0 for MONTE_CARLO strategy");
    }

    // Threshold parameters must be sensible
    assert(config.critical_k_threshold > 0.0 && config.critical_k_threshold <= 1.0 &&
           "critical_k_threshold must be in (0, 1]");
    assert(config.jump_detection_delta > 0.0 &&
           "jump_detection_delta must be > 0");

    // OD sample count should be positive for SSSP-based strategies
    if (config.selection_strategy != SelectionStrategy::GREEDY_FRAGILITY &&
        config.base_config.od_sample_count == 0) {
        throw std::invalid_argument(
            "progressive_fragility: od_sample_count must be > 0 for MC/greedy-betweenness");
    }
}

// ---------------------------------------------------------------------------
// Strategy dispatch table
// ---------------------------------------------------------------------------
using StrategyFn = std::function<ProgressiveFragilityResult(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const ProgressiveFragilityConfig& config,
    const std::vector<std::pair<NodeID, NodeID>>& candidate_edges,
    const SubgraphResult& sub,
    double baseline_composite)>;

// Greedy fragility adapter: wraps the different signature into the common one
ProgressiveFragilityResult greedy_fragility_adapter(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const ProgressiveFragilityConfig& config,
    const std::vector<std::pair<NodeID, NodeID>>& candidate_edges,
    const SubgraphResult& /*sub*/,
    double baseline_composite) {
    return run_greedy_fragility(graph, ch, idx, config, candidate_edges, baseline_composite);
}

StrategyFn get_strategy(SelectionStrategy s) {
    switch (s) {
        case SelectionStrategy::MONTE_CARLO:
            return run_monte_carlo;
        case SelectionStrategy::GREEDY_BETWEENNESS:
            return run_greedy_betweenness;
        case SelectionStrategy::GREEDY_FRAGILITY:
            return greedy_fragility_adapter;
    }
    throw std::invalid_argument("progressive_fragility: unknown selection strategy");
}

}  // namespace

ProgressiveFragilityResult progressive_fragility(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const ProgressiveFragilityConfig& config) {

    // Validate config before any work
    validate_config(config);

    ProgressiveFragilityResult result;
    result.strategy_used = config.selection_strategy;

    // Step 1: Extract subgraph to get candidate edge pool
    auto sub = extract_subgraph(graph, config.base_config.boundary);
    if (!sub.graph || sub.graph->node_count() < 2) return result;

    result.subgraph_nodes = sub.graph->node_count();
    result.subgraph_edges = sub.graph->edge_count();

    // Step 2: Collect candidate edges (in original graph IDs)
    auto all_edges = collect_subgraph_edges(*sub.graph, sub.new_to_original);

    // Apply edge pool filter and/or EdgeSampler
    std::vector<std::pair<NodeID, NodeID>> candidate_edges;
    if (config.sampler_config) {
        EdgeSampler sampler(*sub.graph);
        SamplerConfig sc = *config.sampler_config;
        if (config.edge_pool_filter) {
            auto original_filter = sc.edge_filter;
            auto pool_filter = config.edge_pool_filter;
            sc.edge_filter = [original_filter, pool_filter](uint32_t idx) {
                if (pool_filter && !pool_filter(idx)) return false;
                if (original_filter && !original_filter(idx)) return false;
                return true;
            };
        }
        auto sampled_indices = sampler.sample(sc);
        for (uint32_t idx : sampled_indices) {
            if (idx < all_edges.size())
                candidate_edges.push_back(all_edges[idx]);
        }
    } else if (config.edge_pool_filter) {
        uint32_t edge_idx = 0;
        for (NodeID u = 0; u < sub.graph->node_count(); ++u) {
            auto targets = sub.graph->outgoing_targets(u);
            for (size_t i = 0; i < targets.size(); ++i) {
                if (config.edge_pool_filter(edge_idx)) {
                    candidate_edges.push_back(all_edges[edge_idx]);
                }
                edge_idx++;
            }
        }
    } else {
        candidate_edges = all_edges;
    }

    if (candidate_edges.size() < 2) return result;

    // Resolve k_max: fraction takes precedence over absolute count
    uint32_t effective_k_max = config.k_max;
    if (config.k_max_fraction > 0.0f) {
        effective_k_max = static_cast<uint32_t>(
            config.k_max_fraction * static_cast<float>(candidate_edges.size()));
    }
    effective_k_max = std::min(effective_k_max,
                               static_cast<uint32_t>(candidate_edges.size()) - 1);
    if (effective_k_max == 0) return result;

    result.k_max_used = effective_k_max;

    // Propagate effective k_max through a local config copy
    ProgressiveFragilityConfig eff_cfg = config;
    eff_cfg.k_max = effective_k_max;
    eff_cfg.k_max_fraction = 0.0f;  // already resolved

    // Step 3: Baseline (k=0) = 0.0 (no degradation when no edges removed)
    double baseline_composite = 0.0;

    // Step 4: Dispatch to strategy via table lookup
    auto strategy = get_strategy(eff_cfg.selection_strategy);
    auto strategy_result = strategy(graph, ch, idx, eff_cfg,
                                     candidate_edges, sub, baseline_composite);

    // Merge strategy result into main result
    result.curve = std::move(strategy_result.curve);
    result.removal_sequence = std::move(strategy_result.removal_sequence);

    // Ensure baseline is curve[0]
    if (!result.curve.empty()) {
        result.curve[0].mean_composite = baseline_composite;
        result.curve[0].k = 0;
    }

    // Step 5: Compute AUC and threshold metrics
    compute_curve_metrics(result, config);

    return result;
}

}  // namespace gravel
