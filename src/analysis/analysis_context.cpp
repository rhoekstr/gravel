#include "gravel/analysis/analysis_context.h"
#include "gravel/simplify/simplify.h"
#include <chrono>
#include <utility>

namespace gravel {

AnalysisContext build_analysis_context(
    const ArrayGraph& full_graph,
    const ContractionResult& full_ch,
    const ShortcutIndex& full_idx,
    const AnalysisContextConfig& config) {

    using Clock = std::chrono::steady_clock;
    auto t_start = Clock::now();

    AnalysisContext ctx;

    // Step 1: Extract subgraph within polygon
    auto t0 = Clock::now();
    ctx.raw_subgraph = extract_subgraph(full_graph, config.boundary);
    auto t1 = Clock::now();
    ctx.stats.subgraph_extract_seconds = std::chrono::duration<double>(t1 - t0).count();

    if (!ctx.raw_subgraph.graph || ctx.raw_subgraph.graph->node_count() == 0) {
        return ctx;
    }

    ctx.stats.original_subgraph_nodes = ctx.raw_subgraph.graph->node_count();
    ctx.stats.original_subgraph_edges = ctx.raw_subgraph.graph->edge_count();

    // Step 2: Simplify (degree-2 contraction, lossless)
    if (config.simplify && ctx.raw_subgraph.graph->node_count() > 100) {
        auto t2 = Clock::now();
        auto simplified = contract_degree2(*ctx.raw_subgraph.graph);
        auto t3 = Clock::now();
        ctx.stats.simplification_seconds = std::chrono::duration<double>(t3 - t2).count();

        if (simplified.graph && simplified.graph->node_count() > 0) {
            ctx.analysis_graph = simplified.graph;
            ctx.simplified = true;

            // Compose mapping: simplified → raw_subgraph → full_graph
            ctx.analysis_to_original.resize(simplified.new_to_original.size());
            for (size_t i = 0; i < simplified.new_to_original.size(); ++i) {
                NodeID raw_id = simplified.new_to_original[i];
                if (raw_id < ctx.raw_subgraph.new_to_original.size()) {
                    ctx.analysis_to_original[i] = ctx.raw_subgraph.new_to_original[raw_id];
                } else {
                    ctx.analysis_to_original[i] = INVALID_NODE;
                }
            }
        } else {
            // Simplification produced empty graph, fall back to raw
            ctx.analysis_graph = ctx.raw_subgraph.graph;
            ctx.analysis_to_original = ctx.raw_subgraph.new_to_original;
            ctx.simplified = false;
        }
    } else {
        ctx.analysis_graph = ctx.raw_subgraph.graph;
        ctx.analysis_to_original = ctx.raw_subgraph.new_to_original;
        ctx.simplified = false;
    }

    ctx.stats.analysis_nodes = ctx.analysis_graph->node_count();
    ctx.stats.analysis_edges = ctx.analysis_graph->edge_count();
    ctx.stats.simplification_ratio = (ctx.stats.original_subgraph_nodes > 0) ?
        static_cast<double>(ctx.stats.analysis_nodes) / ctx.stats.original_subgraph_nodes : 1.0;

    // Step 3: Bridge detection on analysis graph
    auto t4 = Clock::now();
    ctx.bridges = find_bridges(*ctx.analysis_graph);
    auto t5 = Clock::now();
    ctx.stats.bridge_detection_seconds = std::chrono::duration<double>(t5 - t4).count();

    // Step 4: Entry point identification
    // Entry points are analysis graph nodes whose ORIGINAL IDs have external neighbors
    auto t6 = Clock::now();
    for (NodeID new_id = 0; new_id < ctx.analysis_graph->node_count(); ++new_id) {
        if (new_id >= ctx.analysis_to_original.size()) continue;
        NodeID orig = ctx.analysis_to_original[new_id];
        if (orig == INVALID_NODE) continue;

        auto targets = full_graph.outgoing_targets(orig);
        std::vector<NodeID> external;
        for (NodeID t : targets) {
            if (ctx.raw_subgraph.original_to_new.find(t) == ctx.raw_subgraph.original_to_new.end()) {
                external.push_back(t);
            }
        }
        if (!external.empty()) {
            ctx.entry_points.push_back({orig, new_id, std::move(external)});
        }
    }
    auto t7 = Clock::now();
    ctx.stats.entry_point_seconds = std::chrono::duration<double>(t7 - t6).count();

    ctx.stats.total_seconds = std::chrono::duration<double>(t7 - t_start).count();

    return ctx;
}

}  // namespace gravel
