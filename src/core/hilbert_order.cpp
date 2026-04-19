#include "gravel/core/hilbert_order.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <memory>
#include <utility>
#include <vector>

namespace gravel {

namespace {

// Standard 2D Hilbert curve: map (x, y) in [0, 2^order) to a 1D index.
// Uses the rotation-based algorithm.
uint64_t xy_to_hilbert(uint32_t x, uint32_t y, int order) {
    uint64_t d = 0;
    for (int s = order - 1; s >= 0; --s) {
        uint32_t rx = (x >> s) & 1;
        uint32_t ry = (y >> s) & 1;
        d += static_cast<uint64_t>((3 * rx) ^ ry) << (2 * s);
        // Rotate
        if (ry == 0) {
            if (rx == 1) {
                x = ((1u << s) - 1) - x;
                y = ((1u << s) - 1) - y;
            }
            std::swap(x, y);
        }
    }
    return d;
}

}  // namespace

std::vector<NodeID> hilbert_permutation(const ArrayGraph& graph) {
    const auto& coords = graph.raw_coords();
    if (coords.empty()) return {};

    NodeID n = graph.node_count();

    // Find bounding box
    double min_lat = coords[0].lat, max_lat = coords[0].lat;
    double min_lon = coords[0].lon, max_lon = coords[0].lon;
    for (NodeID i = 1; i < n; ++i) {
        min_lat = std::min(min_lat, coords[i].lat);
        max_lat = std::max(max_lat, coords[i].lat);
        min_lon = std::min(min_lon, coords[i].lon);
        max_lon = std::max(max_lon, coords[i].lon);
    }

    double range_lat = max_lat - min_lat;
    double range_lon = max_lon - min_lon;
    if (range_lat < 1e-12) range_lat = 1.0;
    if (range_lon < 1e-12) range_lon = 1.0;

    constexpr int HILBERT_ORDER = 16;
    constexpr uint32_t HILBERT_MAX = (1u << HILBERT_ORDER) - 1;

    // Compute Hilbert index for each node
    std::vector<std::pair<uint64_t, NodeID>> indexed(n);
    for (NodeID i = 0; i < n; ++i) {
        uint32_t x = static_cast<uint32_t>(
            std::clamp((coords[i].lon - min_lon) / range_lon, 0.0, 1.0) * HILBERT_MAX);
        uint32_t y = static_cast<uint32_t>(
            std::clamp((coords[i].lat - min_lat) / range_lat, 0.0, 1.0) * HILBERT_MAX);
        indexed[i] = {xy_to_hilbert(x, y, HILBERT_ORDER), i};
    }

    std::sort(indexed.begin(), indexed.end());

    // perm[new_id] = old_id
    std::vector<NodeID> perm(n);
    for (NodeID i = 0; i < n; ++i) {
        perm[i] = indexed[i].second;
    }
    return perm;
}

std::unique_ptr<ArrayGraph> reorder_graph(const ArrayGraph& graph,
                                           const std::vector<NodeID>& perm) {
    NodeID n = graph.node_count();
    if (perm.size() != n) return nullptr;

    // Inverse permutation: inv[old_id] = new_id
    std::vector<NodeID> inv(n);
    for (NodeID i = 0; i < n; ++i) {
        inv[perm[i]] = i;
    }

    // Build edge list with remapped IDs
    std::vector<Edge> edges;
    edges.reserve(graph.edge_count());
    for (NodeID old_u = 0; old_u < n; ++old_u) {
        NodeID new_u = inv[old_u];
        auto targets = graph.outgoing_targets(old_u);
        auto weights = graph.outgoing_weights(old_u);
        for (uint32_t i = 0; i < targets.size(); ++i) {
            edges.push_back({new_u, inv[targets[i]], weights[i]});
        }
    }

    auto result = std::make_unique<ArrayGraph>(n, std::move(edges));

    // Reorder coordinates if present
    const auto& old_coords = graph.raw_coords();
    if (!old_coords.empty()) {
        std::vector<Coord> new_coords(n);
        for (NodeID i = 0; i < n; ++i) {
            new_coords[i] = old_coords[perm[i]];
        }
        // Reconstruct with coords
        auto offsets = result->raw_offsets();
        auto targets = std::vector<NodeID>(result->raw_targets().begin(), result->raw_targets().end());
        auto weights = std::vector<Weight>(result->raw_weights().begin(), result->raw_weights().end());
        result = std::make_unique<ArrayGraph>(
            std::move(offsets), std::move(targets), std::move(weights), std::move(new_coords));
    }

    return result;
}

}  // namespace gravel
