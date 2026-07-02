#include "gravel/simplify/simplify.h"
#include <algorithm>
#include <memory>
#include <optional>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gravel {

SimplificationResult contract_degree2(
    const ArrayGraph& graph,
    const std::unordered_set<NodeID>& bridge_endpoints,
    const std::unordered_set<NodeID>& boundary_protection,
    bool emit_geometry) {

    NodeID n = graph.node_count();

    auto coord_of = [&](NodeID v) { return graph.node_coordinate(v).value_or(Coord{}); };

    // Geometry is meaningless without coordinates; skip it even if requested.
    emit_geometry = emit_geometry && !graph.raw_coords().empty();

    // Directed weight of edge a→b if it exists, else nullopt. Existence — not a positive
    // weight sum — decides whether a merged direction is real: a one-way chain must not
    // synthesize a reverse edge, and a legitimately zero-weight edge must still survive.
    auto edge_weight = [&](NodeID a, NodeID b) -> std::optional<Weight> {
        auto t = graph.outgoing_targets(a);
        auto w = graph.outgoing_weights(a);
        for (size_t i = 0; i < t.size(); ++i) if (t[i] == b) return w[i];
        return std::nullopt;
    };

    // Step 1: Build undirected neighbor sets to find degree-2 nodes.
    // For directed graphs, undirected degree = number of distinct neighbors
    // (combining both outgoing and incoming edges).
    std::vector<std::unordered_set<NodeID>> neighbors(n);
    for (NodeID u = 0; u < n; ++u) {
        auto targets = graph.outgoing_targets(u);
        for (NodeID v : targets) {
            neighbors[u].insert(v);
            neighbors[v].insert(u);
        }
    }

    // Step 2: Mark contractible nodes: undirected degree == 2, not a bridge endpoint
    std::vector<bool> contractible(n, false);
    for (NodeID v = 0; v < n; ++v) {
        if (neighbors[v].size() == 2 && !bridge_endpoints.count(v) &&
            !boundary_protection.count(v)) {
            contractible[v] = true;
        }
    }

    // Step 3: Build chains by following contractible nodes.
    // A chain starts at a junction (non-contractible), goes through contractible nodes,
    // and ends at another junction.
    std::vector<bool> visited(n, false);

    struct MergedEdge {
        NodeID from, to;
        Weight weight;
        std::vector<Coord> points;  // polyline from→to (empty unless emit_geometry)
    };
    std::vector<MergedEdge> merged_edges;

    // For each contractible node, trace the chain it belongs to
    for (NodeID start = 0; start < n; ++start) {
        if (!contractible[start] || visited[start]) continue;

        // Walk backward to find the junction at the start of this chain
        NodeID chain_start = start;
        {
            NodeID prev = INVALID_NODE;
            NodeID cur = start;
            while (contractible[cur]) {
                visited[cur] = true;
                auto& nbrs = neighbors[cur];
                NodeID next = INVALID_NODE;
                for (NodeID nb : nbrs) {
                    if (nb != prev) { next = nb; break; }
                }
                if (next == INVALID_NODE) break;
                prev = cur;
                cur = next;
                if (contractible[cur] && visited[cur]) break;  // cycle of degree-2 nodes
            }
            chain_start = cur;
        }

        // Now walk forward from chain_start through the chain
        // Reset visited for chain nodes (we'll re-walk them)
        for (NodeID v = 0; v < n; ++v) {
            // Only reset nodes visited in the backward walk
            // Actually, let's just re-trace from chain_start
        }
    }

    // Simpler approach: iterate junctions, trace chains from each junction
    std::fill(visited.begin(), visited.end(), false);
    merged_edges.clear();

    for (NodeID junction = 0; junction < n; ++junction) {
        if (contractible[junction]) continue;  // not a junction

        // For each neighbor of this junction that is contractible, trace the chain
        for (NodeID first_d2 : neighbors[junction]) {
            if (!contractible[first_d2] || visited[first_d2]) continue;

            // Trace chain: junction → first_d2 → ... → other_junction. Track existence of
            // every fragment in each direction; a direction is emitted only if the through
            // path actually exists (no phantom edges from one-way or mixed-direction chains).
            Weight total_weight_fwd = 0.0;
            Weight total_weight_rev = 0.0;
            bool fwd_exists = true;
            bool rev_exists = true;
            NodeID prev = junction;
            NodeID cur = first_d2;

            std::vector<Coord> chain_pts;
            if (emit_geometry) chain_pts.push_back(coord_of(junction));

            while (contractible[cur]) {
                visited[cur] = true;
                if (emit_geometry) chain_pts.push_back(coord_of(cur));

                if (auto w = edge_weight(prev, cur)) total_weight_fwd += *w; else fwd_exists = false;
                if (auto w = edge_weight(cur, prev)) total_weight_rev += *w; else rev_exists = false;

                // Move to next node in chain
                NodeID next = INVALID_NODE;
                for (NodeID nb : neighbors[cur]) {
                    if (nb != prev) { next = nb; break; }
                }

                if (next == INVALID_NODE) break;
                prev = cur;
                cur = next;
            }

            // Final fragment: prev → other_junction (cur).
            if (auto w = edge_weight(prev, cur)) total_weight_fwd += *w; else fwd_exists = false;
            if (auto w = edge_weight(cur, prev)) total_weight_rev += *w; else rev_exists = false;

            NodeID other_junction = cur;
            if (emit_geometry) chain_pts.push_back(coord_of(other_junction));
            if (junction != other_junction) {
                if (fwd_exists) {
                    std::vector<Coord> fwd_pts = emit_geometry ? chain_pts : std::vector<Coord>{};
                    merged_edges.push_back(
                        {junction, other_junction, total_weight_fwd, std::move(fwd_pts)});
                }
                if (rev_exists) {
                    std::vector<Coord> rev_pts;
                    if (emit_geometry) rev_pts.assign(chain_pts.rbegin(), chain_pts.rend());
                    merged_edges.push_back(
                        {other_junction, junction, total_weight_rev, std::move(rev_pts)});
                }
            }
        }
    }

    // Step 4: Build simplified graph from junctions + original non-chain edges + merged edges
    SimplificationResult result;
    result.original_nodes = n;
    result.original_edges = graph.edge_count();

    // Collect kept nodes (junctions = non-contractible nodes with at least one edge)
    std::vector<NodeID> kept_nodes;
    std::unordered_map<NodeID, NodeID> old_to_new;
    for (NodeID v = 0; v < n; ++v) {
        if (!contractible[v] && !neighbors[v].empty()) {
            old_to_new[v] = static_cast<NodeID>(kept_nodes.size());
            kept_nodes.push_back(v);
        }
    }

    // Also keep isolated nodes (degree 0) — they might matter for other analyses
    for (NodeID v = 0; v < n; ++v) {
        if (neighbors[v].empty() && old_to_new.find(v) == old_to_new.end()) {
            old_to_new[v] = static_cast<NodeID>(kept_nodes.size());
            kept_nodes.push_back(v);
        }
    }

    // Collect edges: original non-chain edges + merged edges
    std::vector<Edge> new_edges;
    std::vector<std::vector<Coord>> new_edge_pts;  // parallel to new_edges (emit_geometry only)

    // Original edges between non-contractible nodes
    for (NodeID u = 0; u < n; ++u) {
        if (contractible[u]) continue;
        auto it_u = old_to_new.find(u);
        if (it_u == old_to_new.end()) continue;

        auto targets = graph.outgoing_targets(u);
        auto weights = graph.outgoing_weights(u);
        for (size_t i = 0; i < targets.size(); ++i) {
            NodeID v = targets[i];
            if (contractible[v]) continue;  // skip — this connects to a chain, handled by merged edges
            auto it_v = old_to_new.find(v);
            if (it_v == old_to_new.end()) continue;
            new_edges.push_back({it_u->second, it_v->second, weights[i]});
            if (emit_geometry) new_edge_pts.push_back({coord_of(u), coord_of(v)});
        }
    }

    // Merged edges from chain contraction
    for (auto& me : merged_edges) {
        auto it_from = old_to_new.find(me.from);
        auto it_to = old_to_new.find(me.to);
        if (it_from != old_to_new.end() && it_to != old_to_new.end()) {
            new_edges.push_back({it_from->second, it_to->second, me.weight});
            if (emit_geometry) new_edge_pts.push_back(std::move(me.points));
        }
    }

    // Build CSR
    NodeID new_n = static_cast<NodeID>(kept_nodes.size());
    std::vector<uint32_t> offsets(new_n + 1, 0);
    for (const auto& e : new_edges) offsets[e.source + 1]++;
    for (NodeID i = 1; i <= new_n; ++i) offsets[i] += offsets[i - 1];

    std::vector<NodeID> tgt(new_edges.size());
    std::vector<Weight> wgt(new_edges.size());
    std::vector<std::vector<Coord>> ordered_pts;
    if (emit_geometry) ordered_pts.resize(new_edges.size());
    auto pos = offsets;
    for (size_t k = 0; k < new_edges.size(); ++k) {
        const auto& e = new_edges[k];
        uint32_t idx = pos[e.source]++;
        tgt[idx] = e.target;
        wgt[idx] = e.weight;
        if (emit_geometry) ordered_pts[idx] = std::move(new_edge_pts[k]);
    }

    // Preserve coordinates
    std::vector<Coord> coords;
    for (NodeID orig : kept_nodes) {
        auto c = graph.node_coordinate(orig);
        coords.push_back(c.value_or(Coord{}));
    }

    result.graph = std::make_shared<ArrayGraph>(
        std::move(offsets), std::move(tgt), std::move(wgt), std::move(coords));
    result.new_to_original = kept_nodes;
    result.original_to_new = old_to_new;
    result.simplified_nodes = new_n;
    result.simplified_edges = result.graph->edge_count();

    if (emit_geometry) {
        EdgeGeometry geom;
        geom.offsets.assign(ordered_pts.size() + 1, 0);
        for (size_t e = 0; e < ordered_pts.size(); ++e) {
            geom.offsets[e + 1] =
                geom.offsets[e] + static_cast<uint32_t>(ordered_pts[e].size());
        }
        geom.points.reserve(geom.offsets.back());
        for (auto& p : ordered_pts) {
            geom.points.insert(geom.points.end(), p.begin(), p.end());
        }
        result.edge_geometry = std::move(geom);
    }

    return result;
}

}  // namespace gravel
