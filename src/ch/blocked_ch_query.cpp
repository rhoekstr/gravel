#include "gravel/ch/blocked_ch_query.h"
#include "gravel/core/four_heap.h"
#include <algorithm>
#include <stack>
#include <utility>
#include <unordered_set>
#include <vector>

namespace gravel {

void BlockedCHQuery::Workspace::init(NodeID n) {
    fwd_dist.resize(n);
    bwd_dist.resize(n);
    fwd_parent.resize(n);
    bwd_parent.resize(n);
    fwd_gen.assign(n, 0);
    bwd_gen.assign(n, 0);
    generation = 1;
    initialized = true;
}

void BlockedCHQuery::Workspace::reset() {
    ++generation;
    if (generation == 0) {
        std::fill(fwd_gen.begin(), fwd_gen.end(), 0);
        std::fill(bwd_gen.begin(), bwd_gen.end(), 0);
        generation = 1;
    }
    blocked_overlay_edges.clear();
}

BlockedCHQuery::BlockedCHQuery(const ContractionResult& ch, const ShortcutIndex& idx,
                               const ArrayGraph& graph)
    : ch_(ch), shortcut_idx_(idx), graph_(graph), node_count_(ch.num_nodes) {
    // Build reverse adjacency for backward-search original-edge fallback.
    rev_offsets_.resize(node_count_ + 1, 0);
    // Count incoming edges per node.
    for (NodeID u = 0; u < node_count_; ++u) {
        auto targets = graph_.outgoing_targets(u);
        for (uint32_t i = 0; i < targets.size(); ++i)
            ++rev_offsets_[targets[i] + 1];
    }
    for (NodeID i = 1; i <= node_count_; ++i)
        rev_offsets_[i] += rev_offsets_[i - 1];
    rev_sources_.resize(rev_offsets_[node_count_]);
    rev_weights_.resize(rev_offsets_[node_count_]);
    // Fill reverse edges.
    std::vector<uint32_t> pos(rev_offsets_.begin(), rev_offsets_.end());
    for (NodeID u = 0; u < node_count_; ++u) {
        auto targets = graph_.outgoing_targets(u);
        auto weights = graph_.outgoing_weights(u);
        for (uint32_t i = 0; i < targets.size(); ++i) {
            NodeID v = targets[i];
            uint32_t idx2 = pos[v]++;
            rev_sources_[idx2] = u;
            rev_weights_[idx2] = weights[i];
        }
    }
}

// Check if the expansion of overlay edge (from, to) with given shortcut_mid
// contains any blocked original edge.
static bool expansion_blocked(
        const ContractionResult& ch,
        NodeID from, NodeID to, NodeID mid,
        const std::unordered_set<uint64_t>& blocked_originals) {
    if (mid == INVALID_NODE) {
        return blocked_originals.count(ContractionResult::pack_edge(from, to)) > 0;
    }

    struct Frame { NodeID from, to; };
    std::stack<Frame> stk;
    stk.push({from, mid});
    stk.push({mid, to});

    while (!stk.empty()) {
        auto [u, v] = stk.top();
        stk.pop();

        uint64_t key = ContractionResult::pack_edge(u, v);
        auto it = ch.unpack_map.find(key);

        if (it == ch.unpack_map.end() || it->second == INVALID_NODE) {
            if (blocked_originals.count(key)) return true;
        } else {
            NodeID m = it->second;
            stk.push({u, m});
            stk.push({m, v});
        }
    }
    return false;
}

void BlockedCHQuery::populate_blocked_set(
        const std::vector<std::pair<NodeID, NodeID>>& blocked_edges) const {
    for (auto [u, v] : blocked_edges) {
        ws_.blocked_overlay_edges.insert(ContractionResult::pack_edge(u, v));
    }
}

Weight BlockedCHQuery::distance_blocking(
        NodeID source, NodeID target,
        const std::vector<std::pair<NodeID, NodeID>>& blocked_edges) const {
    if (source == target) return 0.0;
    if (!ws_.initialized) ws_.init(node_count_);
    ws_.reset();
    populate_blocked_set(blocked_edges);
    return bidirectional_search_blocked(source, target);
}

Weight BlockedCHQuery::bidirectional_search_blocked(NodeID source, NodeID target) const {
    FourHeap fwd_pq, bwd_pq;

    ws_.set_fwd(source, 0.0, INVALID_NODE);
    ws_.set_bwd(target, 0.0, INVALID_NODE);
    fwd_pq.push(source, 0.0);
    bwd_pq.push(target, 0.0);

    Weight best = INF_WEIGHT;
    bool fwd_done = false, bwd_done = false;

    while (!fwd_done || !bwd_done) {
        if (!fwd_done) {
            if (fwd_pq.empty()) {
                fwd_done = true;
            } else {
                auto [u, du] = fwd_pq.pop();
                if (du > best) {
                    fwd_done = true;
                } else if (du > ws_.get_fwd_dist(u)) {
                    // stale
                } else {
                    Weight bwd_u = ws_.get_bwd_dist(u);
                    if (bwd_u < INF_WEIGHT) {
                        Weight c = du + bwd_u;
                        if (c < best) best = c;
                    }

                    // Relax CH up-graph edges (shortcuts + originals in CH).
                    uint32_t start = ch_.up_offsets[u];
                    uint32_t end = ch_.up_offsets[u + 1];
                    for (uint32_t i = start; i < end; ++i) {
                        NodeID v = ch_.up_targets[i];
                        NodeID mid = ch_.up_shortcut_mid[i];
                        if (expansion_blocked(ch_, u, v, mid, ws_.blocked_overlay_edges))
                            continue;
                        Weight nd = du + ch_.up_weights[i];
                        if (nd < ws_.get_fwd_dist(v)) {
                            ws_.set_fwd(v, nd, u);
                            fwd_pq.push(v, nd);
                        }
                    }

                    // Also relax original-graph edges (all directions).
                    // This recovers original edges that were replaced by blocked
                    // shortcuts during CH construction. Without level filtering,
                    // the search can reach lower-level nodes — correctness is
                    // maintained because the termination condition (du > best)
                    // is still valid.
                    auto og_targets = graph_.outgoing_targets(u);
                    auto og_weights = graph_.outgoing_weights(u);
                    for (uint32_t j = 0; j < og_targets.size(); ++j) {
                        NodeID v = og_targets[j];
                        uint64_t key = ContractionResult::pack_edge(u, v);
                        if (ws_.blocked_overlay_edges.count(key)) continue;
                        Weight nd = du + og_weights[j];
                        if (nd < ws_.get_fwd_dist(v)) {
                            ws_.set_fwd(v, nd, u);
                            fwd_pq.push(v, nd);
                        }
                    }
                }
            }
        }

        if (!bwd_done) {
            if (bwd_pq.empty()) {
                bwd_done = true;
            } else {
                auto [u, du] = bwd_pq.pop();
                if (du > best) {
                    bwd_done = true;
                } else if (du > ws_.get_bwd_dist(u)) {
                    // stale
                } else {
                    Weight fwd_u = ws_.get_fwd_dist(u);
                    if (fwd_u < INF_WEIGHT) {
                        Weight c = fwd_u + du;
                        if (c < best) best = c;
                    }

                    // Relax CH down-graph edges.
                    uint32_t start = ch_.down_offsets[u];
                    uint32_t end = ch_.down_offsets[u + 1];
                    for (uint32_t i = start; i < end; ++i) {
                        NodeID v = ch_.down_targets[i];
                        NodeID mid = ch_.down_shortcut_mid[i];
                        if (expansion_blocked(ch_, v, u, mid, ws_.blocked_overlay_edges))
                            continue;
                        Weight nd = du + ch_.down_weights[i];
                        if (nd < ws_.get_bwd_dist(v)) {
                            ws_.set_bwd(v, nd, u);
                            bwd_pq.push(v, nd);
                        }
                    }

                    // Also relax original-graph reversed edges (all directions).
                    // For backward search: incoming edges of u (i.e., edges v→u).
                    uint32_t rstart = rev_offsets_[u];
                    uint32_t rend = rev_offsets_[u + 1];
                    for (uint32_t j = rstart; j < rend; ++j) {
                        NodeID v = rev_sources_[j];  // v→u is a forward edge
                        uint64_t key = ContractionResult::pack_edge(v, u);
                        if (ws_.blocked_overlay_edges.count(key)) continue;
                        Weight nd = du + rev_weights_[j];
                        if (nd < ws_.get_bwd_dist(v)) {
                            ws_.set_bwd(v, nd, u);
                            bwd_pq.push(v, nd);
                        }
                    }
                }
            }
        }
    }

    return best;
}

}  // namespace gravel
