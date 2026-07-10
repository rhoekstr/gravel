#include "gravel/simplify/bridges.h"
#include "gravel/ch/blocked_ch_query.h"
#include <algorithm>
#include <vector>
#include <functional>
#include <unordered_map>

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

EdgeBridgeInfo bridge_edge_info(const ArrayGraph& graph) {
    const NodeID n = graph.node_count();
    const EdgeID m = graph.edge_count();
    EdgeBridgeInfo out;
    out.is_bridge.assign(m, 0);
    out.cut_size.assign(m, 0);
    if (n == 0) return out;

    const auto& off = graph.raw_offsets();
    const auto& tgt = graph.raw_targets();

    // Group directed edges into undirected edges by sorting on the canonical (min,max) key —
    // no hash maps, no per-node vectors. After the sort `es` is grouped: each run of equal keys
    // is one undirected edge, and its entries carry that edge's CSR ids.
    struct KeyEdge { uint64_t key; uint32_t csr; };
    std::vector<KeyEdge> es;
    es.reserve(static_cast<size_t>(m));
    for (NodeID u = 0; u < n; ++u) {
        for (uint32_t e = off[u]; e < off[u + 1]; ++e) {
            const NodeID v = tgt[e];
            if (u == v) continue;  // self-loops are never bridges
            const NodeID lo = std::min(u, v), hi = std::max(u, v);
            es.push_back({(static_cast<uint64_t>(lo) << 32) | hi, e});
        }
    }
    std::sort(es.begin(), es.end(),
              [](const KeyEdge& a, const KeyEdge& b) { return a.key < b.key; });

    // One undirected edge per run of equal keys: endpoints, directed count, and the run's
    // start index into `es` (ue_run[id]..ue_run[id+1] are that edge's CSR ids).
    std::vector<NodeID> ue_u, ue_v;
    std::vector<uint32_t> ue_count, ue_run;
    for (size_t i = 0; i < es.size();) {
        size_t j = i;
        while (j < es.size() && es[j].key == es[i].key) ++j;
        ue_u.push_back(static_cast<NodeID>(es[i].key >> 32));
        ue_v.push_back(static_cast<NodeID>(es[i].key & 0xffffffffu));
        ue_count.push_back(static_cast<uint32_t>(j - i));
        ue_run.push_back(static_cast<uint32_t>(i));
        i = j;
    }
    ue_run.push_back(static_cast<uint32_t>(es.size()));
    const uint32_t nu = static_cast<uint32_t>(ue_u.size());

    // CSR-flattened undirected adjacency (counting sort): per node -> (neighbor, undirected id).
    std::vector<uint32_t> adj_off(n + 1, 0);
    for (uint32_t id = 0; id < nu; ++id) { ++adj_off[ue_u[id] + 1]; ++adj_off[ue_v[id] + 1]; }
    for (NodeID i = 0; i < n; ++i) adj_off[i + 1] += adj_off[i];
    std::vector<NodeID> adj_nbr(adj_off[n]);
    std::vector<uint32_t> adj_id(adj_off[n]);
    std::vector<uint32_t> fill(adj_off.begin(), adj_off.end() - 1);
    for (uint32_t id = 0; id < nu; ++id) {
        const NodeID a = ue_u[id], b = ue_v[id];
        adj_nbr[fill[a]] = b; adj_id[fill[a]] = id; ++fill[a];
        adj_nbr[fill[b]] = a; adj_id[fill[b]] = id; ++fill[b];
    }

    // Iterative Tarjan on the CSR adjacency, carrying DFS subtree sizes; cut sizes finalize
    // per connected component. All working state is vector-indexed.
    std::vector<uint32_t> disc(n, UINT32_MAX), low(n, UINT32_MAX), subtree(n, 0);
    uint32_t timer = 0;
    struct Frame { NodeID node; uint32_t parent_id; uint32_t cur; };  // cur = index into adj_nbr/adj_id
    std::vector<Frame> stk;
    std::vector<std::pair<uint32_t, uint32_t>> comp_bridges;  // (undirected id, subtree[child])

    for (NodeID start = 0; start < n; ++start) {
        if (disc[start] != UINT32_MAX) continue;
        disc[start] = low[start] = timer++;
        subtree[start] = 1;
        stk.push_back({start, UINT32_MAX, adj_off[start]});
        comp_bridges.clear();

        while (!stk.empty()) {
            Frame& f = stk.back();
            const NodeID u = f.node;
            if (f.cur < adj_off[u + 1]) {
                const uint32_t idx = f.cur++;
                const uint32_t eid = adj_id[idx];
                if (eid == f.parent_id) continue;  // don't reuse the tree edge (by id, so parallels count)
                const NodeID v = adj_nbr[idx];
                if (disc[v] == UINT32_MAX) {
                    disc[v] = low[v] = timer++;
                    subtree[v] = 1;
                    stk.push_back({v, eid, adj_off[v]});  // f may dangle after this; not used below
                } else {
                    low[u] = std::min(low[u], disc[v]);
                }
            } else {
                const NodeID child = f.node;
                const uint32_t child_id = f.parent_id;
                stk.pop_back();
                if (!stk.empty()) {
                    const NodeID p = stk.back().node;
                    low[p] = std::min(low[p], low[child]);
                    subtree[p] += subtree[child];
                    // A single (non-parallel) tree edge whose subtree can't reach above p is a bridge.
                    if (low[child] > disc[p] && ue_count[child_id] <= 2) {
                        comp_bridges.push_back({child_id, subtree[child]});
                    }
                }
            }
        }

        const uint32_t comp_size = subtree[start];
        for (const auto [id, sub] : comp_bridges) {
            const uint32_t cut = std::min(sub, comp_size - sub);
            for (uint32_t p = ue_run[id]; p < ue_run[id + 1]; ++p) {
                out.is_bridge[es[p].csr] = 1;
                out.cut_size[es[p].csr] = cut;
            }
        }
    }
    return out;
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
