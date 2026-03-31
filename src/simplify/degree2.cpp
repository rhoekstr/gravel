#include "gravel/simplify/simplify.h"
#include <algorithm>

namespace gravel {

SimplificationResult contract_degree2(
    const ArrayGraph& graph,
    const std::unordered_set<NodeID>& bridge_endpoints,
    const std::unordered_set<NodeID>& boundary_protection) {

    NodeID n = graph.node_count();

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

            // Trace chain: junction → first_d2 → ... → other_junction
            Weight total_weight_fwd = 0.0;
            Weight total_weight_rev = 0.0;
            NodeID prev = junction;
            NodeID cur = first_d2;

            while (contractible[cur]) {
                visited[cur] = true;

                // Find weight of edge prev → cur
                auto targets = graph.outgoing_targets(prev);
                auto weights = graph.outgoing_weights(prev);
                for (size_t i = 0; i < targets.size(); ++i) {
                    if (targets[i] == cur) { total_weight_fwd += weights[i]; break; }
                }

                // Find weight of edge cur → prev (reverse)
                auto rev_targets = graph.outgoing_targets(cur);
                auto rev_weights = graph.outgoing_weights(cur);
                for (size_t i = 0; i < rev_targets.size(); ++i) {
                    if (rev_targets[i] == prev) { total_weight_rev += rev_weights[i]; break; }
                }

                // Move to next node in chain
                NodeID next = INVALID_NODE;
                for (NodeID nb : neighbors[cur]) {
                    if (nb != prev) { next = nb; break; }
                }

                if (next == INVALID_NODE) break;
                prev = cur;
                cur = next;
            }

            // cur is now the other junction at the end of the chain
            // Add weight of last edge (prev → cur)
            {
                auto targets = graph.outgoing_targets(prev);
                auto weights = graph.outgoing_weights(prev);
                for (size_t i = 0; i < targets.size(); ++i) {
                    if (targets[i] == cur) { total_weight_fwd += weights[i]; break; }
                }
                auto rev_targets = graph.outgoing_targets(cur);
                auto rev_weights = graph.outgoing_weights(cur);
                for (size_t i = 0; i < rev_targets.size(); ++i) {
                    if (rev_targets[i] == prev) { total_weight_rev += rev_weights[i]; break; }
                }
            }

            NodeID other_junction = cur;
            if (junction != other_junction) {
                merged_edges.push_back({junction, other_junction, total_weight_fwd});
                if (total_weight_rev > 0) {
                    merged_edges.push_back({other_junction, junction, total_weight_rev});
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
        }
    }

    // Merged edges from chain contraction
    for (const auto& me : merged_edges) {
        auto it_from = old_to_new.find(me.from);
        auto it_to = old_to_new.find(me.to);
        if (it_from != old_to_new.end() && it_to != old_to_new.end()) {
            new_edges.push_back({it_from->second, it_to->second, me.weight});
        }
    }

    // Build CSR
    NodeID new_n = static_cast<NodeID>(kept_nodes.size());
    std::vector<uint32_t> offsets(new_n + 1, 0);
    for (const auto& e : new_edges) offsets[e.source + 1]++;
    for (NodeID i = 1; i <= new_n; ++i) offsets[i] += offsets[i - 1];

    std::vector<NodeID> tgt(new_edges.size());
    std::vector<Weight> wgt(new_edges.size());
    auto pos = offsets;
    for (const auto& e : new_edges) {
        uint32_t idx = pos[e.source]++;
        tgt[idx] = e.target;
        wgt[idx] = e.weight;
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

    return result;
}

}  // namespace gravel
