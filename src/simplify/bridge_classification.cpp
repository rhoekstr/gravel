#include "gravel/simplify/bridge_classification.h"
#include <algorithm>
#include <unordered_set>

namespace gravel {

BridgeClassification classify_bridges(
    const ArrayGraph& graph,
    const std::function<bool(uint32_t)>& edge_filter) {

    BridgeClassification result;

    // Step 1: Find bridges on the full (unfiltered) graph
    auto full_bridges = find_bridges(graph);
    std::unordered_set<uint64_t> full_bridge_set;
    for (const auto& [u, v] : full_bridges.bridges) {
        full_bridge_set.insert(ContractionResult::pack_edge(u, v));
    }

    // Step 2: Build filtered graph by keeping only edges that pass the filter
    NodeID n = graph.node_count();
    std::vector<Edge> filtered_edges;
    uint32_t edge_idx = 0;
    for (NodeID u = 0; u < n; ++u) {
        auto targets = graph.outgoing_targets(u);
        auto weights = graph.outgoing_weights(u);
        for (size_t i = 0; i < targets.size(); ++i) {
            if (edge_filter(edge_idx)) {
                filtered_edges.push_back({u, targets[i], weights[i]});
            }
            edge_idx++;
        }
    }

    // Build filtered graph (same node count, fewer edges)
    ArrayGraph filtered(n, std::move(filtered_edges));

    // Step 3: Find bridges on the filtered graph
    auto filtered_bridges = find_bridges(filtered);

    // Step 4: Classify each filtered bridge
    result.bridges = filtered_bridges.bridges;
    result.types.resize(result.bridges.size());

    for (size_t i = 0; i < result.bridges.size(); ++i) {
        auto [u, v] = result.bridges[i];
        uint64_t key = ContractionResult::pack_edge(
            std::min(u, v), std::max(u, v));

        if (full_bridge_set.count(key)) {
            result.types[i] = BridgeType::STRUCTURAL;
            result.structural_count++;
        } else {
            result.types[i] = BridgeType::FILTER_INDUCED;
            result.filter_induced_count++;
        }
    }

    return result;
}

}  // namespace gravel
