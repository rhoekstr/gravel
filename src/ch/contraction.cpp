#include "gravel/ch/contraction.h"
#include <algorithm>
#include <cassert>
#include <numeric>
#include <queue>
#include <thread>

namespace gravel {

namespace {

struct MutableGraph {
    struct MEdge {
        NodeID target;
        Weight weight;
        NodeID shortcut_mid;
    };

    uint32_t num_nodes;
    std::vector<std::vector<MEdge>> fwd;
    std::vector<std::vector<MEdge>> bwd;
    std::vector<bool> contracted;

    explicit MutableGraph(const ArrayGraph& g) : num_nodes(g.node_count()) {
        fwd.resize(num_nodes);
        bwd.resize(num_nodes);
        contracted.assign(num_nodes, false);
        for (NodeID u = 0; u < num_nodes; ++u) {
            auto targets = g.outgoing_targets(u);
            auto weights = g.outgoing_weights(u);
            for (uint32_t i = 0; i < targets.size(); ++i) {
                fwd[u].push_back({targets[i], weights[i], INVALID_NODE});
                bwd[targets[i]].push_back({u, weights[i], INVALID_NODE});
            }
        }
    }

    void add_shortcut(NodeID from, NodeID to, Weight w, NodeID mid) {
        for (auto& e : fwd[from]) {
            if (e.target == to) {
                if (w < e.weight) {
                    e.weight = w;
                    e.shortcut_mid = mid;
                    for (auto& be : bwd[to]) {
                        if (be.target == from) {
                            be.weight = w;
                            be.shortcut_mid = mid;
                            break;
                        }
                    }
                }
                return;
            }
        }
        fwd[from].push_back({to, w, mid});
        bwd[to].push_back({from, w, mid});
    }
};

struct WitnessWorkspace {
    struct QEntry {
        Weight dist;
        NodeID node;
        bool operator>(const QEntry& o) const { return dist > o.dist; }
    };
    std::vector<Weight> dist;
    std::vector<NodeID> touched;
    std::priority_queue<QEntry, std::vector<QEntry>, std::greater<>> pq;

    void init(uint32_t n) {
        dist.assign(n, INF_WEIGHT);
        touched.reserve(256);
    }
    void reset() {
        for (NodeID v : touched) dist[v] = INF_WEIGHT;
        touched.clear();
    }
};

bool witness_search(const MutableGraph& g, WitnessWorkspace& ws,
                    NodeID source, NodeID target,
                    Weight max_cost, NodeID excluded, int max_settle) {
    ws.pq.push({0.0, source});
    ws.dist[source] = 0.0;
    ws.touched.push_back(source);

    int settled = 0;
    bool found = false;

    while (!ws.pq.empty() && settled < max_settle) {
        auto [d, u] = ws.pq.top();
        ws.pq.pop();
        if (d > ws.dist[u]) continue;
        if (d > max_cost) break;
        if (u == target) { found = true; break; }
        ++settled;
        for (const auto& e : g.fwd[u]) {
            if (e.target == excluded || g.contracted[e.target]) continue;
            Weight nd = d + e.weight;
            if (nd < ws.dist[e.target] && nd <= max_cost) {
                ws.dist[e.target] = nd;
                ws.touched.push_back(e.target);
                ws.pq.push({nd, e.target});
            }
        }
    }
    while (!ws.pq.empty()) ws.pq.pop();
    ws.reset();
    return found;
}

// Compute priority for a node. Uses witness search for edge diff.
double compute_priority(const MutableGraph& g, WitnessWorkspace& ws,
                        NodeID node, int deleted_nbrs, Level level,
                        const CHBuildConfig& config) {
    int shortcuts = 0;
    int edges_removed = 0;

    for (const auto& e : g.fwd[node]) {
        if (!g.contracted[e.target]) edges_removed++;
    }
    for (const auto& e : g.bwd[node]) {
        if (!g.contracted[e.target]) edges_removed++;
    }

    for (const auto& in : g.bwd[node]) {
        if (g.contracted[in.target]) continue;
        for (const auto& out : g.fwd[node]) {
            if (g.contracted[out.target]) continue;
            if (in.target == out.target) continue;
            Weight sc = in.weight + out.weight;
            if (!witness_search(g, ws, in.target, out.target, sc,
                                node, config.max_settle_for_priority)) {
                shortcuts++;
            }
        }
    }

    int edge_diff = shortcuts - edges_removed;
    return edge_diff * config.edge_diff_weight
         + deleted_nbrs * config.deleted_nbr_weight
         + level * config.level_weight;
}

// Build the final CSR result from a mutable graph and metadata.
ContractionResult build_result(MutableGraph& mg, NodeID n,
                               const std::vector<Level>& levels,
                               const std::vector<NodeID>& order) {
    ContractionResult result;
    result.num_nodes = n;
    result.node_levels = levels;
    result.order = order;

    struct TempEdge { NodeID source, target; Weight weight; NodeID shortcut_mid; };
    std::vector<TempEdge> up_list, down_list;

    for (NodeID u = 0; u < n; ++u) {
        for (const auto& e : mg.fwd[u]) {
            if (levels[e.target] > levels[u])
                up_list.push_back({u, e.target, e.weight, e.shortcut_mid});
        }
        for (const auto& e : mg.bwd[u]) {
            if (levels[e.target] > levels[u])
                down_list.push_back({u, e.target, e.weight, e.shortcut_mid});
        }
    }

    auto build_csr = [n](std::vector<TempEdge>& list,
                         std::vector<uint32_t>& offsets,
                         std::vector<NodeID>& targets,
                         std::vector<Weight>& weights,
                         std::vector<NodeID>& shortcuts) {
        std::sort(list.begin(), list.end(),
                  [](const TempEdge& a, const TempEdge& b) { return a.source < b.source; });
        offsets.assign(n + 1, 0);
        targets.reserve(list.size());
        weights.reserve(list.size());
        shortcuts.reserve(list.size());
        for (const auto& e : list) offsets[e.source + 1]++;
        for (NodeID i = 1; i <= n; ++i) offsets[i] += offsets[i - 1];
        for (const auto& e : list) {
            targets.push_back(e.target);
            weights.push_back(e.weight);
            shortcuts.push_back(e.shortcut_mid);
        }
    };

    build_csr(up_list, result.up_offsets, result.up_targets, result.up_weights, result.up_shortcut_mid);
    build_csr(down_list, result.down_offsets, result.down_targets, result.down_weights, result.down_shortcut_mid);

    // Build unpacking table
    {
        struct WeightedMid { Weight weight; NodeID mid; };
        std::unordered_map<uint64_t, WeightedMid> best;
        for (NodeID u = 0; u < n; ++u) {
            for (const auto& e : mg.fwd[u]) {
                auto key = ContractionResult::pack_edge(u, e.target);
                auto it = best.find(key);
                if (it == best.end() || e.weight < it->second.weight)
                    best[key] = {e.weight, e.shortcut_mid};
            }
        }
        result.unpack_map.reserve(best.size());
        for (const auto& [key, wm] : best)
            result.unpack_map[key] = wm.mid;
    }

    return result;
}

// Select an independent set of uncontracted nodes with low priority.
// Independent = no two selected nodes are adjacent in the uncontracted graph.
std::vector<NodeID> select_independent_set(
    const MutableGraph& mg, const std::vector<double>& priorities,
    uint32_t max_size) {

    // Sort uncontracted nodes by priority
    std::vector<NodeID> candidates;
    for (NodeID v = 0; v < mg.num_nodes; ++v) {
        if (!mg.contracted[v]) candidates.push_back(v);
    }
    std::sort(candidates.begin(), candidates.end(),
              [&](NodeID a, NodeID b) { return priorities[a] < priorities[b]; });

    std::vector<bool> selected(mg.num_nodes, false);
    std::vector<bool> neighbor_selected(mg.num_nodes, false);
    std::vector<NodeID> batch;

    for (NodeID v : candidates) {
        if (batch.size() >= max_size) break;
        if (neighbor_selected[v]) continue;

        selected[v] = true;
        batch.push_back(v);

        // Mark neighbors
        for (const auto& e : mg.fwd[v]) {
            if (!mg.contracted[e.target]) neighbor_selected[e.target] = true;
        }
        for (const auto& e : mg.bwd[v]) {
            if (!mg.contracted[e.target]) neighbor_selected[e.target] = true;
        }
    }

    return batch;
}

// Shortcut info computed during parallel phase
struct ShortcutInfo {
    NodeID from, to;
    Weight weight;
    NodeID mid;
};

// Compute needed shortcuts for a single node (read-only on graph).
std::vector<ShortcutInfo> compute_shortcuts_for_node(
    const MutableGraph& mg, WitnessWorkspace& ws,
    NodeID node, const CHBuildConfig& config) {

    std::vector<ShortcutInfo> shortcuts;
    for (const auto& in : mg.bwd[node]) {
        if (mg.contracted[in.target]) continue;
        for (const auto& out : mg.fwd[node]) {
            if (mg.contracted[out.target]) continue;
            if (in.target == out.target) continue;
            Weight sc = in.weight + out.weight;
            if (!witness_search(mg, ws, in.target, out.target, sc,
                                node, config.max_settle_for_witness)) {
                shortcuts.push_back({in.target, out.target, sc, node});
            }
        }
    }
    return shortcuts;
}

ContractionResult build_ch_parallel(const ArrayGraph& graph, CHBuildConfig config,
                                     std::function<void(int)> progress_cb) {
    NodeID n = graph.node_count();
    MutableGraph mg(graph);

    uint32_t num_threads = std::max(1u, std::thread::hardware_concurrency());
    uint32_t max_batch = config.batch_size > 0 ? config.batch_size :
                         std::max(1u, n / (num_threads * 4));

    // Per-thread witness workspaces
    std::vector<WitnessWorkspace> workspaces(num_threads);
    for (auto& ws : workspaces) ws.init(n);

    // Also need a workspace for priority computation
    WitnessWorkspace pri_ws;
    pri_ws.init(n);

    std::vector<int> deleted_neighbors(n, 0);
    std::vector<Level> levels(n, 0);
    std::vector<NodeID> order;
    order.reserve(n);

    // Compute initial priorities
    std::vector<double> priorities(n);
    for (NodeID v = 0; v < n; ++v) {
        priorities[v] = compute_priority(mg, pri_ws, v, 0, 0, config);
    }

    int contracted_count = 0;

    while (contracted_count < static_cast<int>(n)) {
        // Phase A: Select independent set
        auto batch = select_independent_set(mg, priorities, max_batch);
        if (batch.empty()) break;

        // Phase B: Parallel witness search for each batch node
        std::vector<std::vector<ShortcutInfo>> all_shortcuts(batch.size());

        // Simple thread pool: divide batch among threads
        auto process_range = [&](uint32_t tid, size_t start, size_t end) {
            for (size_t i = start; i < end; ++i) {
                all_shortcuts[i] = compute_shortcuts_for_node(
                    mg, workspaces[tid], batch[i], config);
            }
        };

        if (batch.size() <= num_threads || num_threads == 1) {
            // Small batch or single thread: process sequentially
            for (size_t i = 0; i < batch.size(); ++i) {
                all_shortcuts[i] = compute_shortcuts_for_node(
                    mg, pri_ws, batch[i], config);
            }
        } else {
            std::vector<std::thread> threads;
            size_t chunk = (batch.size() + num_threads - 1) / num_threads;
            for (uint32_t t = 0; t < num_threads; ++t) {
                size_t start = t * chunk;
                size_t end = std::min(start + chunk, batch.size());
                if (start < end) {
                    threads.emplace_back(process_range, t, start, end);
                }
            }
            for (auto& t : threads) t.join();
        }

        // Phase C: Sequential application
        for (size_t i = 0; i < batch.size(); ++i) {
            NodeID v = batch[i];
            mg.contracted[v] = true;
            levels[v] = static_cast<Level>(contracted_count);
            order.push_back(v);
            contracted_count++;

            // Apply shortcuts
            for (const auto& sc : all_shortcuts[i]) {
                mg.add_shortcut(sc.from, sc.to, sc.weight, sc.mid);
            }

            // Update neighbor metadata
            for (const auto& e : mg.fwd[v]) {
                if (!mg.contracted[e.target]) {
                    deleted_neighbors[e.target]++;
                    levels[e.target] = std::max(levels[e.target],
                                                static_cast<Level>(levels[v] + 1));
                }
            }
            for (const auto& e : mg.bwd[v]) {
                if (!mg.contracted[e.target]) {
                    deleted_neighbors[e.target]++;
                    levels[e.target] = std::max(levels[e.target],
                                                static_cast<Level>(levels[v] + 1));
                }
            }
        }

        // Recompute priorities for affected nodes
        std::vector<bool> needs_update(n, false);
        for (NodeID v : batch) {
            for (const auto& e : mg.fwd[v]) {
                if (!mg.contracted[e.target]) needs_update[e.target] = true;
            }
            for (const auto& e : mg.bwd[v]) {
                if (!mg.contracted[e.target]) needs_update[e.target] = true;
            }
        }
        for (NodeID v = 0; v < n; ++v) {
            if (needs_update[v]) {
                priorities[v] = compute_priority(mg, pri_ws, v,
                                                  deleted_neighbors[v], levels[v], config);
            }
        }

        if (progress_cb) {
            progress_cb(contracted_count * 100 / n);
        }
    }

    return build_result(mg, n, levels, order);
}

}  // namespace

ContractionResult build_ch(const ArrayGraph& graph, CHBuildConfig config,
                           std::function<void(int)> progress_cb) {
    if (config.parallel) {
        return build_ch_parallel(graph, config, progress_cb);
    }

    NodeID n = graph.node_count();
    MutableGraph mg(graph);
    WitnessWorkspace ws;
    ws.init(n);

    std::vector<int> deleted_neighbors(n, 0);
    std::vector<Level> levels(n, 0);
    std::vector<NodeID> order;
    order.reserve(n);

    struct PQEntry {
        double priority;
        NodeID node;
        bool operator>(const PQEntry& o) const { return priority > o.priority; }
    };
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<>> pq;

    // Initial priorities with cheap witness search
    for (NodeID v = 0; v < n; ++v) {
        double pri = compute_priority(mg, ws, v, 0, 0, config);
        pq.push({pri, v});
    }

    int contracted_count = 0;
    while (!pq.empty()) {
        auto [old_pri, v] = pq.top();
        pq.pop();
        if (mg.contracted[v]) continue;

        // Lazy update: recompute and check if still minimum
        double new_pri = compute_priority(mg, ws, v, deleted_neighbors[v],
                                          levels[v], config);
        if (!pq.empty() && new_pri > pq.top().priority) {
            pq.push({new_pri, v});
            continue;
        }

        // Contract
        mg.contracted[v] = true;
        levels[v] = static_cast<Level>(contracted_count);
        order.push_back(v);

        // Add shortcuts with full witness search limit
        for (const auto& in : mg.bwd[v]) {
            if (mg.contracted[in.target]) continue;
            for (const auto& out : mg.fwd[v]) {
                if (mg.contracted[out.target]) continue;
                if (in.target == out.target) continue;
                Weight sc = in.weight + out.weight;
                if (!witness_search(mg, ws, in.target, out.target, sc,
                                    v, config.max_settle_for_witness)) {
                    mg.add_shortcut(in.target, out.target, sc, v);
                }
            }
        }

        // Update neighbor metadata
        for (const auto& e : mg.fwd[v]) {
            if (!mg.contracted[e.target]) {
                deleted_neighbors[e.target]++;
                levels[e.target] = std::max(levels[e.target],
                                            static_cast<Level>(levels[v] + 1));
            }
        }
        for (const auto& e : mg.bwd[v]) {
            if (!mg.contracted[e.target]) {
                deleted_neighbors[e.target]++;
                levels[e.target] = std::max(levels[e.target],
                                            static_cast<Level>(levels[v] + 1));
            }
        }

        contracted_count++;
        if (progress_cb && contracted_count % std::max(1u, n / 100) == 0) {
            progress_cb(contracted_count * 100 / n);
        }
    }

    return build_result(mg, n, levels, order);
}

}  // namespace gravel
