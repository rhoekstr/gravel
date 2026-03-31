#include "gravel/simplify/bridges.h"
#include "gravel/ch/blocked_ch_query.h"
#include <algorithm>
#include <vector>

namespace gravel {

BridgeResult find_bridges(const ArrayGraph& graph) {
    NodeID n = graph.node_count();
    if (n == 0) return {};

    // Build undirected adjacency: for each node, store (neighbor, edge_id)
    // where edge_id uniquely identifies the undirected edge.
    // Count directed edges between each ordered pair to detect multi-edges.

    struct AdjEntry {
        NodeID neighbor;
        uint32_t undirected_id;  // same id for both directions of same edge
    };

    // First pass: collect all undirected edges with multiplicity
    struct UndirectedEdge {
        NodeID u, v;       // u < v
        uint32_t count;    // number of directed edges forming this undirected edge
    };

    // Map (u,v) → index in undirected edges list
    // Use sorted edge pairs
    struct PairHash {
        size_t operator()(uint64_t key) const { return std::hash<uint64_t>{}(key); }
    };
    std::unordered_map<uint64_t, uint32_t, PairHash> pair_to_id;
    std::vector<UndirectedEdge> und_edges;

    for (NodeID u = 0; u < n; ++u) {
        auto targets = graph.outgoing_targets(u);
        for (NodeID v : targets) {
            if (u == v) continue;  // skip self-loops
            NodeID lo = std::min(u, v), hi = std::max(u, v);
            uint64_t key = (uint64_t(lo) << 32) | hi;
            auto it = pair_to_id.find(key);
            if (it == pair_to_id.end()) {
                uint32_t id = static_cast<uint32_t>(und_edges.size());
                pair_to_id[key] = id;
                und_edges.push_back({lo, hi, 1});
            } else {
                und_edges[it->second].count++;
            }
        }
    }

    // Build adjacency list with undirected edge ids
    std::vector<std::vector<AdjEntry>> adj(n);
    for (uint32_t id = 0; id < und_edges.size(); ++id) {
        auto [u, v, count] = und_edges[id];
        adj[u].push_back({v, id});
        adj[v].push_back({u, id});
    }

    // Iterative Tarjan's bridge-finding
    std::vector<uint32_t> disc(n, UINT32_MAX);
    std::vector<uint32_t> low(n, UINT32_MAX);
    uint32_t timer = 0;

    BridgeResult result;

    struct Frame {
        NodeID node;
        uint32_t parent_edge_id;  // UINT32_MAX if root
        uint32_t child_idx;
    };

    std::vector<Frame> stk;

    for (NodeID start = 0; start < n; ++start) {
        if (disc[start] != UINT32_MAX) continue;

        disc[start] = low[start] = timer++;
        stk.push_back({start, UINT32_MAX, 0});

        while (!stk.empty()) {
            auto& frame = stk.back();
            NodeID u = frame.node;

            if (frame.child_idx < adj[u].size()) {
                auto [v, eid] = adj[u][frame.child_idx];
                frame.child_idx++;

                // Skip the tree edge we came from (by edge id, not by node)
                if (eid == frame.parent_edge_id) continue;

                if (disc[v] == UINT32_MAX) {
                    // Tree edge
                    disc[v] = low[v] = timer++;
                    stk.push_back({v, eid, 0});
                } else {
                    // Back/cross edge
                    low[u] = std::min(low[u], disc[v]);
                }
            } else {
                // Done with u — pop and update parent
                stk.pop_back();
                if (!stk.empty()) {
                    auto& parent_frame = stk.back();
                    NodeID p = parent_frame.node;
                    low[p] = std::min(low[p], low[u]);

                    if (low[u] > disc[p]) {
                        // Edge p-u is a bridge candidate
                        // Find the undirected edge id
                        NodeID lo = std::min(p, u), hi = std::max(p, u);
                        uint64_t key = (uint64_t(lo) << 32) | hi;
                        uint32_t eid = pair_to_id[key];
                        // Only a bridge if there's exactly one directed edge (or two for bidirectional)
                        // A single undirected edge has count 1 (one-way) or 2 (bidirectional)
                        // Multi-edges have count > 2
                        if (und_edges[eid].count <= 2) {
                            result.bridges.push_back({lo, hi});
                        }
                    }
                }
            }
        }
    }

    std::sort(result.bridges.begin(), result.bridges.end());
    return result;
}

void compute_bridge_costs(
    BridgeResult& bridges,
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx) {

    BlockedCHQuery bcq(ch, idx, graph);
    bridges.replacement_costs.resize(bridges.bridges.size());

    for (size_t i = 0; i < bridges.bridges.size(); ++i) {
        auto [u, v] = bridges.bridges[i];
        // Compute shortest u→v with edge {u,v} blocked
        bridges.replacement_costs[i] =
            bcq.distance_blocking(u, v, {{u, v}});
    }
}

}  // namespace gravel
