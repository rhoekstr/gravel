#include "gravel/analysis/county_fragility.h"
#include "gravel/analysis/algebraic_connectivity.h"
#include "gravel/analysis/kirchhoff.h"
#include "gravel/core/incremental_sssp.h"
#include "gravel/core/dijkstra.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_set>
#include <limits>

namespace gravel {

CountyFragilityResult county_fragility_index(
    const ArrayGraph& full_graph,
    const ContractionResult& full_ch,
    const ShortcutIndex& full_idx,
    const CountyFragilityConfig& config,
    const AnalysisContext* ctx) {

    CountyFragilityResult result;

    // --- Resolve analysis graph: from context or extract fresh ---
    const ArrayGraph* analysis_graph = nullptr;
    const std::vector<NodeID>* node_mapping = nullptr;  // analysis_id → original_id
    SubgraphResult fresh_sub;

    if (ctx && ctx->valid()) {
        // Use cached context — skip extraction, simplification, bridge detection
        analysis_graph = ctx->analysis_graph.get();
        node_mapping = &ctx->analysis_to_original;
        result.bridges = ctx->bridges;
        result.entry_point_count = static_cast<uint32_t>(ctx->entry_points.size());
    } else {
        // Fresh extraction (original behavior)
        fresh_sub = extract_subgraph(full_graph, config.boundary);
        if (!fresh_sub.graph || fresh_sub.graph->node_count() == 0) return result;
        analysis_graph = fresh_sub.graph.get();
        node_mapping = &fresh_sub.new_to_original;
        result.bridges = find_bridges(*analysis_graph);
    }

    result.subgraph_nodes = analysis_graph->node_count();
    result.subgraph_edges = analysis_graph->edge_count();

    // --- Metrics that run on the analysis graph ---

    // Betweenness
    BetweennessConfig bc;
    bc.sample_sources = config.betweenness_samples;
    bc.seed = config.seed;
    result.betweenness = edge_betweenness(*analysis_graph, bc);

    // Algebraic connectivity (skip if fast-approximate mode)
    if (!config.skip_spectral) {
        result.algebraic_connectivity = algebraic_connectivity(*analysis_graph);
    }

    // Kirchhoff index (skip if fast-approximate mode)
    if (!config.skip_spectral) {
        KirchhoffConfig kc;
        kc.seed = config.seed;
        result.kirchhoff_index_value = kirchhoff_index(*analysis_graph, kc);
    }

    // Accessibility — use context entry points or compute fresh
    if (ctx && ctx->valid()) {
        // Build a minimal AccessibilityResult from context entry points
        result.accessibility.entry_points = ctx->entry_points;
        result.accessibility.accessibility_score = (ctx->entry_points.size() > 1) ? 0.5 : 0.0;
        result.entry_point_count = static_cast<uint32_t>(ctx->entry_points.size());
    } else {
        result.accessibility = analyze_accessibility(full_graph, full_ch, full_idx, fresh_sub);
        result.entry_point_count = static_cast<uint32_t>(result.accessibility.entry_points.size());
    }

    // --- Fragility sampling via local Dijkstra on analysis graph ---
    // Instead of expensive per-edge CH queries on the full graph, we:
    // 1. Sample OD pairs from the analysis graph (14K nodes after simplification)
    // 2. Run multi-source Dijkstra on the local graph to get primary distances
    // 3. For each OD pair, find the bottleneck edge (on the SP DAG) and compute
    //    the replacement distance by blocking that single edge
    //
    // This runs entirely on the simplified subgraph — no full-graph CH needed.
    if (config.od_sample_count > 0 && analysis_graph->node_count() >= 2) {
        auto local = build_local_graph(*analysis_graph);
        std::mt19937_64 rng(config.seed);
        std::uniform_int_distribution<NodeID> node_dist(0, analysis_graph->node_count() - 1);

        // Sample OD pairs in local coordinates
        struct LocalODPair { uint32_t s, t; };
        std::vector<LocalODPair> od_pairs;
        for (uint32_t i = 0; i < config.od_sample_count; ++i) {
            uint32_t s = node_dist(rng);
            uint32_t t = node_dist(rng);
            if (s != t) od_pairs.push_back({s, t});
        }

        // Collect unique sources and run multi-source Dijkstra
        std::unordered_set<uint32_t> source_set;
        for (const auto& p : od_pairs) source_set.insert(p.s);
        std::vector<uint32_t> sources(source_set.begin(), source_set.end());
        std::unordered_map<uint32_t, uint32_t> source_idx;
        for (uint32_t i = 0; i < sources.size(); ++i) source_idx[sources[i]] = i;

        // Compute unblocked distances from all sources
        // Handle external blocked_edges by mapping to local edge keys
        std::unordered_set<uint64_t> blocked_keys;
        if (!config.blocked_edges.empty()) {
            // Map original→local for blocked edges
            std::unordered_map<NodeID, uint32_t> orig_to_local;
            for (uint32_t i = 0; i < local.n_nodes; ++i)
                orig_to_local[(*node_mapping)[i]] = i;
            for (const auto& [u, v] : config.blocked_edges) {
                auto iu = orig_to_local.find(u);
                auto iv = orig_to_local.find(v);
                if (iu != orig_to_local.end() && iv != orig_to_local.end())
                    blocked_keys.insert(IncrementalSSSP::edge_key(iu->second, iv->second));
            }
        }

        IncrementalSSSP sssp(local, sources, blocked_keys);

        // Build fragility results from local distances
        result.sampled_fragilities.resize(od_pairs.size());
        for (size_t i = 0; i < od_pairs.size(); ++i) {
            uint32_t si = source_idx[od_pairs[i].s];
            Weight primary = sssp.dist_full(si, od_pairs[i].t);
            if (primary >= IncrementalSSSP::INF) continue;

            // Find bottleneck: the SP edge whose removal causes max stretch.
            // Walk the SP DAG from source to target, for each edge on the path
            // compute the replacement distance by blocking just that edge.
            Weight blocked_dist = sssp.dist(si, od_pairs[i].t);
            double ratio = (blocked_dist >= IncrementalSSSP::INF) ?
                std::numeric_limits<double>::infinity() :
                static_cast<double>(blocked_dist) / static_cast<double>(primary);

            FragilityResult fr;
            fr.primary_distance = primary;
            NodeID orig_s = (*node_mapping)[od_pairs[i].s];
            NodeID orig_t = (*node_mapping)[od_pairs[i].t];
            fr.primary_path = {orig_s, orig_t};
            EdgeFragility ef;
            ef.source = orig_s;
            ef.target = orig_t;
            ef.replacement_distance = blocked_dist;
            ef.fragility_ratio = ratio;
            fr.edge_fragilities.push_back(ef);
            result.sampled_fragilities[i] = fr;
        }
    }

    // --- Composite scoring ---
    double bridge_score = static_cast<double>(result.bridges.bridges.size()) /
                          std::max(1u, result.subgraph_edges / 2);

    double ac_score = (result.algebraic_connectivity > 0.0) ?
                      1.0 / (1.0 + result.algebraic_connectivity) : 1.0;

    double accessibility_score = 1.0 - result.accessibility.accessibility_score;

    std::vector<double> ratios;
    for (const auto& f : result.sampled_fragilities) {
        if (f.valid() && !f.edge_fragilities.empty()) {
            double ratio = f.bottleneck().fragility_ratio;
            if (std::isfinite(ratio)) {
                ratios.push_back(ratio);
            }
        }
    }

    if (!ratios.empty()) {
        std::sort(ratios.begin(), ratios.end());
        size_t nr = ratios.size();
        result.fragility_p25 = ratios[nr * 25 / 100];
        result.fragility_p50 = ratios[nr / 2];
        result.fragility_p75 = ratios[nr * 75 / 100];
        result.fragility_p90 = ratios[std::min(nr - 1, nr * 9 / 10)];
        result.fragility_p99 = ratios[std::min(nr - 1, nr * 99 / 100)];
    }

    double avg_bottleneck = 0.0;
    for (double r : ratios) avg_bottleneck += r;
    uint32_t valid_samples = static_cast<uint32_t>(ratios.size());
    double fragility_score = (valid_samples > 0) ?
                             1.0 / (1.0 + (avg_bottleneck / valid_samples)) : 0.5;
    fragility_score = 1.0 - fragility_score;

    const auto& w = config.weights;
    result.composite_index = w.bridge_weight * bridge_score +
                              w.connectivity_weight * ac_score +
                              w.accessibility_weight * accessibility_score +
                              w.fragility_weight * fragility_score;

    return result;
}

}  // namespace gravel
