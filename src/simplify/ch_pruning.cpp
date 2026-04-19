#include "gravel/simplify/simplify.h"
#include <algorithm>
#include <unordered_set>
#include <functional>
#include <memory>
#include <utility>

namespace gravel {

SimplificationResult prune_by_ch_level(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    double keep_fraction,
    const std::unordered_set<NodeID>& bridge_endpoints) {

    NodeID n = graph.node_count();
    SimplificationResult result;
    result.original_nodes = n;
    result.original_edges = graph.edge_count();

    if (keep_fraction >= 1.0) {
        // No pruning — return copy of original
        result.graph = std::make_shared<ArrayGraph>(
            std::vector<uint32_t>(graph.raw_offsets()),
            std::vector<NodeID>(graph.raw_targets().begin(), graph.raw_targets().end()),
            std::vector<Weight>(graph.raw_weights().begin(), graph.raw_weights().end()),
            std::vector<Coord>(graph.raw_coords()));
        result.new_to_original.resize(n);
        for (NodeID v = 0; v < n; ++v) {
            result.new_to_original[v] = v;
            result.original_to_new[v] = v;
        }
        result.simplified_nodes = n;
        result.simplified_edges = graph.edge_count();
        return result;
    }

    // Compute level threshold
    std::vector<Level> sorted_levels(ch.node_levels.begin(), ch.node_levels.end());
    std::sort(sorted_levels.begin(), sorted_levels.end());
    size_t thresh_idx = static_cast<size_t>((1.0 - keep_fraction) * sorted_levels.size());
    thresh_idx = std::min(thresh_idx, sorted_levels.size() - 1);
    Level threshold = sorted_levels[thresh_idx];

    // Mark nodes to keep
    std::vector<NodeID> kept_nodes;
    std::unordered_map<NodeID, NodeID> old_to_new;

    for (NodeID v = 0; v < n; ++v) {
        bool keep = (ch.node_levels[v] >= threshold) || bridge_endpoints.count(v);
        if (keep) {
            old_to_new[v] = static_cast<NodeID>(kept_nodes.size());
            kept_nodes.push_back(v);
        }
    }

    // Collect edges where both endpoints are kept
    std::vector<Edge> edges;
    for (NodeID u = 0; u < n; ++u) {
        auto it_u = old_to_new.find(u);
        if (it_u == old_to_new.end()) continue;

        auto targets = graph.outgoing_targets(u);
        auto weights = graph.outgoing_weights(u);
        for (size_t i = 0; i < targets.size(); ++i) {
            auto it_v = old_to_new.find(targets[i]);
            if (it_v != old_to_new.end()) {
                edges.push_back({it_u->second, it_v->second, weights[i]});
            }
        }
    }

    // Build CSR
    NodeID new_n = static_cast<NodeID>(kept_nodes.size());
    std::vector<uint32_t> offsets(new_n + 1, 0);
    for (const auto& e : edges) offsets[e.source + 1]++;
    for (NodeID i = 1; i <= new_n; ++i) offsets[i] += offsets[i - 1];

    std::vector<NodeID> tgt(edges.size());
    std::vector<Weight> wgt(edges.size());
    auto pos = offsets;
    for (const auto& e : edges) {
        uint32_t idx = pos[e.source]++;
        tgt[idx] = e.target;
        wgt[idx] = e.weight;
    }

    std::vector<Coord> coords;
    for (NodeID orig : kept_nodes) {
        auto c = graph.node_coordinate(orig);
        coords.push_back(c.value_or(Coord{}));
    }

    result.graph = std::make_shared<ArrayGraph>(
        std::move(offsets), std::move(tgt), std::move(wgt), std::move(coords));
    result.new_to_original = kept_nodes;
    result.original_to_new = std::move(old_to_new);
    result.simplified_nodes = new_n;
    result.simplified_edges = result.graph->edge_count();

    return result;
}

SimplificationResult filter_edges(
    const ArrayGraph& graph,
    const std::function<bool(uint32_t)>& predicate) {

    NodeID n = graph.node_count();
    SimplificationResult result;
    result.original_nodes = n;
    result.original_edges = graph.edge_count();

    // Collect kept edges and the nodes they reference
    std::unordered_set<NodeID> active_nodes;
    struct KeptEdge { NodeID src, tgt; Weight weight; };
    std::vector<KeptEdge> kept_edges;

    uint32_t edge_idx = 0;
    for (NodeID u = 0; u < n; ++u) {
        auto targets = graph.outgoing_targets(u);
        auto weights = graph.outgoing_weights(u);
        for (size_t i = 0; i < targets.size(); ++i) {
            if (predicate(edge_idx)) {
                kept_edges.push_back({u, targets[i], weights[i]});
                active_nodes.insert(u);
                active_nodes.insert(targets[i]);
            }
            edge_idx++;
        }
    }

    // Build node mapping
    std::vector<NodeID> kept_nodes(active_nodes.begin(), active_nodes.end());
    std::sort(kept_nodes.begin(), kept_nodes.end());

    std::unordered_map<NodeID, NodeID> old_to_new;
    for (size_t i = 0; i < kept_nodes.size(); ++i) {
        old_to_new[kept_nodes[i]] = static_cast<NodeID>(i);
    }

    // Remap edges
    std::vector<Edge> edges;
    for (const auto& ke : kept_edges) {
        edges.push_back({old_to_new[ke.src], old_to_new[ke.tgt], ke.weight});
    }

    // Build CSR
    NodeID new_n = static_cast<NodeID>(kept_nodes.size());
    std::vector<uint32_t> offsets(new_n + 1, 0);
    for (const auto& e : edges) offsets[e.source + 1]++;
    for (NodeID i = 1; i <= new_n; ++i) offsets[i] += offsets[i - 1];

    std::vector<NodeID> tgt(edges.size());
    std::vector<Weight> wgt(edges.size());
    auto pos = offsets;
    for (const auto& e : edges) {
        uint32_t idx = pos[e.source]++;
        tgt[idx] = e.target;
        wgt[idx] = e.weight;
    }

    std::vector<Coord> coords;
    for (NodeID orig : kept_nodes) {
        auto c = graph.node_coordinate(orig);
        coords.push_back(c.value_or(Coord{}));
    }

    result.graph = std::make_shared<ArrayGraph>(
        std::move(offsets), std::move(tgt), std::move(wgt), std::move(coords));
    result.new_to_original = kept_nodes;
    result.original_to_new = std::move(old_to_new);
    result.simplified_nodes = new_n;
    result.simplified_edges = result.graph->edge_count();

    return result;
}

}  // namespace gravel
