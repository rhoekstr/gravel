#include "gravel/geo/graph_coarsening.h"
#include <algorithm>
#include <numeric>
#include <memory>
#include <utility>

namespace gravel {

CoarseningResult coarsen_graph(
    const ArrayGraph& graph,
    const RegionAssignment& assignment,
    const BorderEdgeResult& border_edges,
    const CoarseningConfig& config) {

    CoarseningResult result;
    uint32_t n_regions = static_cast<uint32_t>(assignment.regions.size());
    if (n_regions == 0) return result;

    // Build region metadata
    result.region_indices.resize(n_regions);
    result.region_labels.resize(n_regions);
    result.region_ids.resize(n_regions);
    result.node_counts.resize(n_regions, 0);
    result.internal_edge_counts.resize(n_regions, 0);

    for (uint32_t r = 0; r < n_regions; ++r) {
        result.region_indices[r] = static_cast<int32_t>(r);
        result.region_labels[r] = assignment.regions[r].label;
        result.region_ids[r] = assignment.regions[r].region_id;
        result.node_counts[r] = assignment.region_node_counts[r];
    }

    // Count internal edges per region
    NodeID n = graph.node_count();
    for (NodeID u = 0; u < n; ++u) {
        int32_t u_region = assignment.region_index[u];
        if (u_region < 0) continue;
        auto targets = graph.outgoing_targets(u);
        for (NodeID v : targets) {
            if (assignment.region_index[v] == u_region) {
                result.internal_edge_counts[u_region]++;
            }
        }
    }

    // Build coarsened edges from border edge summaries
    struct CoarseEdge {
        uint32_t from, to;
        Weight weight;
    };
    std::vector<CoarseEdge> edges;

    for (const auto& [pair, summary] : border_edges.pair_summaries) {
        if (summary.edge_count < config.min_border_edges) continue;

        Weight w;
        if (config.weight_mode == CoarseningConfig::EdgeWeightMode::SHORTEST_PATH &&
            summary.shortest_path > 0) {
            w = summary.shortest_path;
        } else {
            w = static_cast<Weight>(summary.min_weight);
        }

        // Add both directions
        edges.push_back({static_cast<uint32_t>(pair.region_a),
                         static_cast<uint32_t>(pair.region_b), w});
        edges.push_back({static_cast<uint32_t>(pair.region_b),
                         static_cast<uint32_t>(pair.region_a), w});
    }

    // Sort by source for CSR construction
    std::sort(edges.begin(), edges.end(), [](const CoarseEdge& a, const CoarseEdge& b) {
        return a.from < b.from || (a.from == b.from && a.to < b.to);
    });

    // Build CSR arrays
    std::vector<uint32_t> offsets(n_regions + 1, 0);
    for (const auto& e : edges) offsets[e.from + 1]++;
    for (uint32_t i = 1; i <= n_regions; ++i) offsets[i] += offsets[i - 1];

    std::vector<NodeID> targets(edges.size());
    std::vector<Weight> weights(edges.size());
    auto pos = offsets;
    for (const auto& e : edges) {
        uint32_t idx = pos[e.from]++;
        targets[idx] = e.to;
        weights[idx] = e.weight;
    }

    // Compute centroids if requested
    std::vector<Coord> coords;
    if (config.compute_centroids) {
        coords.resize(n_regions, {0.0, 0.0});
        std::vector<uint32_t> counts(n_regions, 0);
        for (NodeID u = 0; u < n; ++u) {
            int32_t r = assignment.region_index[u];
            if (r < 0) continue;
            auto c = graph.node_coordinate(u);
            if (c) {
                coords[r].lat += c->lat;
                coords[r].lon += c->lon;
                counts[r]++;
            }
        }
        for (uint32_t r = 0; r < n_regions; ++r) {
            if (counts[r] > 0) {
                coords[r].lat /= counts[r];
                coords[r].lon /= counts[r];
            }
        }
    }

    result.graph = std::make_unique<ArrayGraph>(
        std::move(offsets), std::move(targets), std::move(weights), std::move(coords));
    return result;
}

}  // namespace gravel
