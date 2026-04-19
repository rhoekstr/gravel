#include "gravel/simplify/reduced_graph.h"
#include "gravel/ch/ch_query.h"
#include "gravel/core/geo_math.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <memory>
#include <utility>
#include <unordered_map>
#include <vector>

namespace gravel {

namespace {

// Pick one central node per region by the configured centrality method.
// Returns: region index → original NodeID of central, or INVALID_NODE.
std::vector<NodeID> select_central_nodes(
    const ArrayGraph& graph,
    const std::vector<int32_t>& node_region,
    const std::vector<RegionInfo>& regions,
    const ReducedGraphConfig& config) {

    uint32_t n_regions = static_cast<uint32_t>(regions.size());
    NodeID n = graph.node_count();
    std::vector<NodeID> centrals(n_regions, INVALID_NODE);

    // Group nodes by region
    std::vector<std::vector<NodeID>> region_nodes(n_regions);
    for (NodeID v = 0; v < n; ++v) {
        int32_t r = node_region[v];
        if (r >= 0 && static_cast<uint32_t>(r) < n_regions) {
            region_nodes[r].push_back(v);
        }
    }

    if (config.method == ReducedGraphConfig::Centrality::GEOMETRIC_CENTROID) {
        for (uint32_t r = 0; r < n_regions; ++r) {
            if (region_nodes[r].empty()) continue;
            const auto& poly = regions[r].boundary;
            if (poly.vertices.empty()) continue;

            double clat = 0.0, clon = 0.0;
            for (const auto& v : poly.vertices) { clat += v.lat; clon += v.lon; }
            clat /= poly.vertices.size();
            clon /= poly.vertices.size();
            Coord centroid{clat, clon};

            NodeID best = INVALID_NODE;
            double best_dist = std::numeric_limits<double>::infinity();
            for (NodeID v : region_nodes[r]) {
                auto c = graph.node_coordinate(v);
                if (!c) continue;
                double d = haversine_meters(centroid, *c);
                if (d < best_dist) { best_dist = d; best = v; }
            }
            centrals[r] = best;
        }

    } else if (config.method == ReducedGraphConfig::Centrality::HIGHEST_DEGREE) {
        for (uint32_t r = 0; r < n_regions; ++r) {
            if (region_nodes[r].empty()) continue;
            NodeID best = INVALID_NODE;
            uint32_t best_deg = 0;
            for (NodeID v : region_nodes[r]) {
                uint32_t d = graph.degree(v);
                if (d > best_deg) { best_deg = d; best = v; }
            }
            centrals[r] = best;
        }

    } else {  // PROVIDED
        if (config.precomputed_centrals.size() != n_regions) {
            throw std::invalid_argument(
                "ReducedGraphConfig::PROVIDED requires precomputed_centrals of length == regions.size()");
        }
        for (uint32_t r = 0; r < n_regions; ++r) {
            NodeID c = config.precomputed_centrals[r];
            if (c != INVALID_NODE && c < n && node_region[c] == static_cast<int32_t>(r)) {
                centrals[r] = c;
            }
        }
    }

    return centrals;
}

// Identify border nodes: nodes with at least one edge to a different region.
std::unordered_set<NodeID> identify_border_nodes(
    const ArrayGraph& graph,
    const std::vector<int32_t>& node_region) {

    std::unordered_set<NodeID> result;
    NodeID n = graph.node_count();

    for (NodeID u = 0; u < n; ++u) {
        int32_t u_region = node_region[u];
        if (u_region < 0) continue;

        auto targets = graph.outgoing_targets(u);
        for (NodeID v : targets) {
            int32_t v_region = node_region[v];
            if (v_region >= 0 && v_region != u_region) {
                result.insert(u);
                result.insert(v);
            }
        }
    }
    return result;
}

}  // namespace

ReducedGraph build_reduced_graph(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const std::vector<int32_t>& node_region,
    const std::vector<RegionInfo>& regions,
    const ReducedGraphConfig& config) {

    ReducedGraph result;
    uint32_t n_regions = static_cast<uint32_t>(regions.size());
    if (n_regions == 0) return result;

    if (node_region.size() != graph.node_count()) {
        throw std::invalid_argument(
            "node_region size must equal graph.node_count()");
    }

    auto centrals = select_central_nodes(graph, node_region, regions, config);
    auto border_set = identify_border_nodes(graph, node_region);

    // Layout reduced graph: [centrals in region order] + [border nodes]
    std::vector<NodeID> reduced_to_original;
    std::unordered_map<NodeID, NodeID> original_to_reduced;

    for (uint32_t r = 0; r < n_regions; ++r) {
        if (centrals[r] != INVALID_NODE) {
            NodeID reduced_id = static_cast<NodeID>(reduced_to_original.size());
            original_to_reduced[centrals[r]] = reduced_id;
            reduced_to_original.push_back(centrals[r]);
            result.central_of[regions[r].region_id] = reduced_id;
        }
    }

    for (NodeID b : border_set) {
        if (original_to_reduced.find(b) == original_to_reduced.end()) {
            NodeID reduced_id = static_cast<NodeID>(reduced_to_original.size());
            original_to_reduced[b] = reduced_id;
            reduced_to_original.push_back(b);
        }
    }

    NodeID n_reduced = static_cast<NodeID>(reduced_to_original.size());

    // Per-node metadata
    result.node_region.resize(n_reduced);
    result.is_central.resize(n_reduced, false);
    for (NodeID i = 0; i < n_reduced; ++i) {
        NodeID orig = reduced_to_original[i];
        int32_t r = node_region[orig];
        result.node_region[i] = (r >= 0) ? regions[r].region_id : "";
    }
    for (const auto& [region_id, central_reduced_id] : result.central_of) {
        result.is_central[central_reduced_id] = true;
    }

    // Build edges
    struct ReducedEdge { NodeID from, to; Weight w; };
    std::vector<ReducedEdge> edges;
    edges.reserve(border_set.size() * 2);

    // Intra-region: central → border (via CH distance)
    CHQuery chq(ch);
    for (uint32_t r = 0; r < n_regions; ++r) {
        if (centrals[r] == INVALID_NODE) continue;
        NodeID central_orig = centrals[r];
        NodeID central_reduced = original_to_reduced[central_orig];

        for (NodeID b : border_set) {
            int32_t b_region = node_region[b];
            if (b_region < 0 || static_cast<uint32_t>(b_region) != r) continue;
            if (b == central_orig) continue;

            NodeID border_reduced = original_to_reduced[b];
            Weight d = chq.distance(central_orig, b);
            if (d >= INF_WEIGHT) continue;

            edges.push_back({central_reduced, border_reduced, d});
            edges.push_back({border_reduced, central_reduced, d});
        }
    }

    // Inter-region: preserve original border-crossing edges
    NodeID n_orig = graph.node_count();
    for (NodeID u = 0; u < n_orig; ++u) {
        int32_t u_region = node_region[u];
        if (u_region < 0) continue;

        auto targets = graph.outgoing_targets(u);
        auto weights = graph.outgoing_weights(u);
        for (size_t i = 0; i < targets.size(); ++i) {
            NodeID v = targets[i];
            int32_t v_region = node_region[v];
            if (v_region < 0 || v_region == u_region) continue;

            auto it_u = original_to_reduced.find(u);
            auto it_v = original_to_reduced.find(v);
            if (it_u == original_to_reduced.end() || it_v == original_to_reduced.end()) continue;

            edges.push_back({it_u->second, it_v->second, weights[i]});

            RegionPair key{std::min(u_region, v_region), std::max(u_region, v_region)};
            result.inter_region_edges[key].push_back({it_u->second, it_v->second});
        }
    }

    // CSR construction
    std::sort(edges.begin(), edges.end(), [](const ReducedEdge& a, const ReducedEdge& b) {
        return a.from < b.from || (a.from == b.from && a.to < b.to);
    });

    std::vector<uint32_t> offsets(n_reduced + 1, 0);
    for (const auto& e : edges) offsets[e.from + 1]++;
    for (NodeID i = 1; i <= n_reduced; ++i) offsets[i] += offsets[i - 1];

    std::vector<NodeID> csr_targets(edges.size());
    std::vector<Weight> csr_weights(edges.size());
    auto pos = offsets;
    for (const auto& e : edges) {
        uint32_t idx = pos[e.from]++;
        csr_targets[idx] = e.to;
        csr_weights[idx] = e.w;
    }

    std::vector<Coord> coords(n_reduced);
    for (NodeID i = 0; i < n_reduced; ++i) {
        auto c = graph.node_coordinate(reduced_to_original[i]);
        coords[i] = c.value_or(Coord{0.0, 0.0});
    }

    result.graph = std::make_unique<ArrayGraph>(
        std::move(offsets), std::move(csr_targets), std::move(csr_weights), std::move(coords));
    result.reduced_to_original = std::move(reduced_to_original);
    result.original_to_reduced = std::move(original_to_reduced);

    return result;
}

}  // namespace gravel
