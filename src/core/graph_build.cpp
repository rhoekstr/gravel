#include "gravel/core/graph_build.h"

#include "gravel/core/types.h"

#include <cmath>
#include <map>
#include <utility>

namespace gravel {

std::shared_ptr<ArrayGraph> graph_from_endpoints(
    const std::vector<Coord>& src_coords,
    const std::vector<Coord>& tgt_coords,
    const std::vector<double>& weights,
    int precision,
    bool directed) {
    const double scale = std::pow(10.0, precision);
    std::map<std::pair<int64_t, int64_t>, uint32_t> node_ids;
    std::vector<Coord> node_coords;

    auto node_for = [&](const Coord& c) -> uint32_t {
        // Round-half-to-even (banker's) to match Python's round(), which the prior pure-Python
        // from_geodataframe used for endpoint quantization. std::llrint honors the default
        // FE_TONEAREST rounding mode; std::llround would be half-away-from-zero and could snap an
        // exact-half boundary to a different node than the 2.4.x release did.
        std::pair<int64_t, int64_t> key{static_cast<int64_t>(std::llrint(c.lat * scale)),
                                        static_cast<int64_t>(std::llrint(c.lon * scale))};
        auto it = node_ids.find(key);
        if (it != node_ids.end()) return it->second;
        auto id = static_cast<uint32_t>(node_coords.size());
        node_ids.emplace(key, id);
        node_coords.push_back(c);
        return id;
    };

    const size_t input_edges = src_coords.size();
    std::vector<Edge> edges;
    edges.reserve(directed ? input_edges : input_edges * 2);
    for (size_t e = 0; e < input_edges; ++e) {
        uint32_t a = node_for(src_coords[e]);
        uint32_t b = node_for(tgt_coords[e]);
        edges.push_back({a, b, weights[e]});
        if (!directed) edges.push_back({b, a, weights[e]});
    }

    const uint32_t n = static_cast<uint32_t>(node_coords.size());
    std::vector<uint32_t> offsets(n + 1, 0);
    for (const auto& ed : edges) offsets[ed.source + 1]++;
    for (uint32_t i = 1; i <= n; ++i) offsets[i] += offsets[i - 1];

    std::vector<NodeID> targets(edges.size());
    std::vector<Weight> wgt(edges.size());
    auto pos = offsets;
    for (const auto& ed : edges) {
        uint32_t idx = pos[ed.source]++;
        targets[idx] = ed.target;
        wgt[idx] = ed.weight;
    }
    return std::make_shared<ArrayGraph>(std::move(offsets), std::move(targets), std::move(wgt),
                                        std::move(node_coords));
}

}  // namespace gravel
