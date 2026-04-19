#include "gravel/core/subgraph.h"
#include "gravel/core/geo_math.h"
#include <memory>
#include <utility>
#include <vector>

namespace gravel {

SubgraphResult extract_subgraph(const ArrayGraph& graph, const Polygon& boundary) {
    SubgraphResult result;

    // Phase 1: identify nodes inside polygon
    NodeID n = graph.node_count();
    std::vector<NodeID> inside_nodes;
    inside_nodes.reserve(n / 4);  // heuristic

    for (NodeID v = 0; v < n; ++v) {
        auto coord = graph.node_coordinate(v);
        if (!coord) continue;
        if (point_in_polygon(*coord, boundary.vertices)) {
            NodeID new_id = static_cast<NodeID>(inside_nodes.size());
            result.original_to_new[v] = new_id;
            result.new_to_original.push_back(v);
            inside_nodes.push_back(v);
        }
    }

    // Phase 2: collect edges where both endpoints are inside
    std::vector<Edge> edges;
    for (NodeID new_src = 0; new_src < inside_nodes.size(); ++new_src) {
        NodeID orig_src = inside_nodes[new_src];
        auto targets = graph.outgoing_targets(orig_src);
        auto weights = graph.outgoing_weights(orig_src);
        for (size_t i = 0; i < targets.size(); ++i) {
            auto it = result.original_to_new.find(targets[i]);
            if (it != result.original_to_new.end()) {
                edges.push_back({new_src, it->second, weights[i]});
            }
        }
    }

    // Build subgraph with coordinates
    NodeID sub_n = static_cast<NodeID>(inside_nodes.size());
    std::vector<Coord> coords;
    coords.reserve(sub_n);
    for (NodeID orig : inside_nodes) {
        coords.push_back(*graph.node_coordinate(orig));
    }

    // Use SoA constructor: build offsets/targets/weights from edge list
    std::vector<uint32_t> offsets(sub_n + 1, 0);
    for (const auto& e : edges) {
        offsets[e.source + 1]++;
    }
    for (NodeID i = 1; i <= sub_n; ++i) {
        offsets[i] += offsets[i - 1];
    }

    std::vector<NodeID> tgt(edges.size());
    std::vector<Weight> wgt(edges.size());
    std::vector<uint32_t> pos = offsets;  // copy for scatter
    for (const auto& e : edges) {
        uint32_t idx = pos[e.source]++;
        tgt[idx] = e.target;
        wgt[idx] = e.weight;
    }

    result.graph = std::make_shared<ArrayGraph>(
        std::move(offsets), std::move(tgt), std::move(wgt), std::move(coords));

    return result;
}

}  // namespace gravel
