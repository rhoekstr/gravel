#include "gravel/simplify/simplify.h"
#include "gravel/simplify/bridges.h"
#include <unordered_set>

namespace gravel {

// Forward declaration for degradation (defined in degradation.cpp)
DegradationReport estimate_degradation(
    const ArrayGraph& orig_graph,
    const ContractionResult& orig_ch,
    const ArrayGraph& simp_graph,
    const std::vector<NodeID>& new_to_original,
    const std::unordered_map<NodeID, NodeID>& original_to_new,
    uint32_t num_samples,
    uint64_t seed);

// Compose two node mappings: if stage1 maps A→B and stage2 maps B→C,
// the composed mapping maps A→C.
static void compose_mappings(
    const std::vector<NodeID>& stage1_new_to_orig,
    const SimplificationResult& stage2,
    std::vector<NodeID>& composed_new_to_orig,
    std::unordered_map<NodeID, NodeID>& composed_orig_to_new) {

    composed_new_to_orig.resize(stage2.new_to_original.size());
    composed_orig_to_new.clear();

    for (size_t i = 0; i < stage2.new_to_original.size(); ++i) {
        // stage2.new_to_original[i] is a node ID in stage1's output
        NodeID stage1_id = stage2.new_to_original[i];
        // Map through stage1 to get original ID
        NodeID orig_id = (stage1_id < stage1_new_to_orig.size()) ?
                          stage1_new_to_orig[stage1_id] : stage1_id;
        composed_new_to_orig[i] = orig_id;
        composed_orig_to_new[orig_id] = static_cast<NodeID>(i);
    }
}

SimplificationResult simplify_graph(
    const ArrayGraph& graph,
    const ContractionResult* ch,
    const ShortcutIndex* idx,
    SimplificationConfig config) {

    SimplificationResult result;
    result.original_nodes = graph.node_count();
    result.original_edges = graph.edge_count();

    // Detect bridges if needed for any stage
    std::unordered_set<NodeID> bridge_endpoints;
    if (config.preserve_bridges) {
        auto bridges = find_bridges(graph);
        for (const auto& [u, v] : bridges.bridges) {
            bridge_endpoints.insert(u);
            bridge_endpoints.insert(v);
        }
    }

    // Track the current graph through the pipeline
    const ArrayGraph* current = &graph;
    std::shared_ptr<ArrayGraph> owned_current;  // owns intermediate results
    std::vector<NodeID> cumulative_new_to_orig;
    std::unordered_map<NodeID, NodeID> cumulative_orig_to_new;

    // Initialize identity mapping
    cumulative_new_to_orig.resize(graph.node_count());
    for (NodeID v = 0; v < graph.node_count(); ++v) {
        cumulative_new_to_orig[v] = v;
        cumulative_orig_to_new[v] = v;
    }

    // --- Stage 1: Edge category filtering (runs on original CSR indices) ---
    if (config.edge_filter) {
        auto stage = filter_edges(*current, config.edge_filter);

        DegradationReport::StageReport sr;
        sr.stage_name = "edge_category_filter";
        sr.nodes_before = current->node_count();
        sr.edges_before = current->edge_count();
        sr.nodes_after = stage.simplified_nodes;
        sr.edges_after = stage.simplified_edges;
        result.degradation.stages.push_back(sr);

        // Compose mappings
        auto prev_map = cumulative_new_to_orig;
        compose_mappings(prev_map, stage, cumulative_new_to_orig, cumulative_orig_to_new);

        owned_current = stage.graph;
        current = owned_current.get();
    }

    // --- Stage 2: Degree-2 contraction ---
    if (config.contract_degree2) {
        // Remap bridge endpoints to current graph's node IDs
        std::unordered_set<NodeID> current_bridge_eps;
        if (config.preserve_bridges) {
            for (NodeID orig : bridge_endpoints) {
                auto it = cumulative_orig_to_new.find(orig);
                if (it != cumulative_orig_to_new.end()) {
                    current_bridge_eps.insert(it->second);
                }
            }
        }

        auto stage = contract_degree2(*current, current_bridge_eps);

        DegradationReport::StageReport sr;
        sr.stage_name = "degree2_contraction";
        sr.nodes_before = current->node_count();
        sr.edges_before = current->edge_count();
        sr.nodes_after = stage.simplified_nodes;
        sr.edges_after = stage.simplified_edges;
        result.degradation.stages.push_back(sr);

        auto prev_map = cumulative_new_to_orig;
        compose_mappings(prev_map, stage, cumulative_new_to_orig, cumulative_orig_to_new);

        owned_current = stage.graph;
        current = owned_current.get();
    }

    // --- Stage 3: CH-level pruning ---
    if (ch && config.ch_level_keep_fraction < 1.0) {
        // Need to map CH levels to current graph's nodes
        // Build a "virtual CH" with levels for current nodes
        // Each current node maps to an original node; use original CH levels
        ContractionResult virtual_ch;
        virtual_ch.num_nodes = current->node_count();
        virtual_ch.node_levels.resize(current->node_count(), 0);

        for (NodeID new_id = 0; new_id < current->node_count(); ++new_id) {
            NodeID orig_id = cumulative_new_to_orig[new_id];
            if (orig_id < ch->node_levels.size()) {
                virtual_ch.node_levels[new_id] = ch->node_levels[orig_id];
            }
        }

        // Remap bridge endpoints
        std::unordered_set<NodeID> current_bridge_eps;
        if (config.preserve_bridges) {
            for (NodeID orig : bridge_endpoints) {
                auto it = cumulative_orig_to_new.find(orig);
                if (it != cumulative_orig_to_new.end()) {
                    current_bridge_eps.insert(it->second);
                }
            }
        }

        auto stage = prune_by_ch_level(*current, virtual_ch,
                                        config.ch_level_keep_fraction,
                                        current_bridge_eps);

        DegradationReport::StageReport sr;
        sr.stage_name = "ch_level_pruning";
        sr.nodes_before = current->node_count();
        sr.edges_before = current->edge_count();
        sr.nodes_after = stage.simplified_nodes;
        sr.edges_after = stage.simplified_edges;
        result.degradation.stages.push_back(sr);

        auto prev_map = cumulative_new_to_orig;
        compose_mappings(prev_map, stage, cumulative_new_to_orig, cumulative_orig_to_new);

        owned_current = stage.graph;
        current = owned_current.get();
    }

    // --- Final result ---
    result.graph = owned_current ? owned_current :
        std::make_shared<ArrayGraph>(
            std::vector<uint32_t>(graph.raw_offsets()),
            std::vector<NodeID>(graph.raw_targets().begin(), graph.raw_targets().end()),
            std::vector<Weight>(graph.raw_weights().begin(), graph.raw_weights().end()),
            std::vector<Coord>(graph.raw_coords()));
    result.new_to_original = cumulative_new_to_orig;
    result.original_to_new = cumulative_orig_to_new;
    result.simplified_nodes = result.graph->node_count();
    result.simplified_edges = result.graph->edge_count();

    // --- Degradation estimation ---
    if (config.estimate_degradation && ch) {
        auto stages_backup = result.degradation.stages;
        result.degradation = estimate_degradation(
            graph, *ch, *result.graph,
            result.new_to_original, result.original_to_new,
            config.degradation_samples, config.seed);
        result.degradation.stages = std::move(stages_backup);
    }

    return result;
}

}  // namespace gravel
