#include "gravel/ch/ch_query.h"
#include "gravel/core/four_heap.h"
#include <algorithm>
#include <cassert>
#include <vector>

namespace gravel {

void CHQuery::Workspace::init(NodeID n) {
    fwd_dist.resize(n);
    bwd_dist.resize(n);
    fwd_parent.resize(n);
    bwd_parent.resize(n);
    fwd_gen.assign(n, 0);
    bwd_gen.assign(n, 0);
    generation = 1;  // start at 1 so gen==0 means "never touched"
    initialized = true;
}

void CHQuery::Workspace::reset() {
    ++generation;
    if (generation == 0) {
        // Overflow: full reset (happens every ~4 billion queries)
        std::fill(fwd_gen.begin(), fwd_gen.end(), 0);
        std::fill(bwd_gen.begin(), bwd_gen.end(), 0);
        generation = 1;
    }
}

CHQuery::CHQuery(const ContractionResult& ch)
    : ch_(ch), node_count_(ch.num_nodes) {}

Weight CHQuery::distance(NodeID source, NodeID target) const {
    NodeID meeting;
    return bidirectional_search(source, target, meeting);
}

RouteResult CHQuery::route(NodeID source, NodeID target) const {
    NodeID meeting;
    Weight dist = bidirectional_search(source, target, meeting);
    RouteResult result;
    result.distance = dist;
    if (dist < INF_WEIGHT) {
        result.path = unpack_path(source, meeting, target);
    }
    return result;
}

std::vector<Weight> CHQuery::distance_matrix(
    const std::vector<NodeID>& origins,
    const std::vector<NodeID>& destinations) const {
    size_t n_orig = origins.size();
    size_t n_dest = destinations.size();
    std::vector<Weight> result(n_orig * n_dest, INF_WEIGHT);

    #pragma omp parallel
    {
        CHQuery local_query(ch_);

        #pragma omp for schedule(dynamic)
        for (size_t i = 0; i < n_orig; ++i) {
            for (size_t j = 0; j < n_dest; ++j) {
                result[i * n_dest + j] = local_query.distance(origins[i], destinations[j]);
            }
        }
    }

    return result;
}

Weight CHQuery::distance(const SnapResult& from, const SnapResult& to) const {
    if (!from.valid() || !to.valid()) return INF_WEIGHT;
    NodeID meeting;
    return bidirectional_search_phantom(from, to, meeting);
}

RouteResult CHQuery::route(const SnapResult& from, const SnapResult& to) const {
    if (!from.valid() || !to.valid()) return {};
    NodeID meeting;
    Weight dist = bidirectional_search_phantom(from, to, meeting);
    RouteResult result;
    result.distance = dist;
    // Path unpacking for phantom routes would require storing which endpoint
    // was used — for now, return distance only. Full path support deferred.
    return result;
}

Weight CHQuery::bidirectional_search_phantom(const SnapResult& from, const SnapResult& to,
                                              NodeID& meeting) const {
    // Dual-seeded bidirectional search:
    // Forward seeds from both endpoints of the source snap edge
    // Backward seeds from both endpoints of the destination snap edge
    if (!ws_.initialized) ws_.init(node_count_);
    ws_.reset();

    FourHeap fwd_pq, bwd_pq;

    // Seed forward from source edge endpoints
    NodeID fs = from.edge_source;
    NodeID ft = from.edge_target;
    Weight d_fs = from.dist_to_source;  // cost from phantom to edge_source
    Weight d_ft = from.dist_to_target;  // cost from phantom to edge_target

    ws_.set_fwd(fs, d_fs, INVALID_NODE);
    fwd_pq.push(fs, d_fs);
    if (d_ft < ws_.get_fwd_dist(ft)) {
        ws_.set_fwd(ft, d_ft, INVALID_NODE);
        fwd_pq.push(ft, d_ft);
    }

    // Seed backward from destination edge endpoints
    NodeID ts = to.edge_source;
    NodeID tt = to.edge_target;
    Weight d_ts = to.dist_to_source;
    Weight d_tt = to.dist_to_target;

    ws_.set_bwd(ts, d_ts, INVALID_NODE);
    bwd_pq.push(ts, d_ts);
    if (d_tt < ws_.get_bwd_dist(tt)) {
        ws_.set_bwd(tt, d_tt, INVALID_NODE);
        bwd_pq.push(tt, d_tt);
    }

    Weight best = INF_WEIGHT;
    meeting = INVALID_NODE;
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
                        if (c < best) { best = c; meeting = u; }
                    }
                    uint32_t start = ch_.up_offsets[u];
                    uint32_t end = ch_.up_offsets[u + 1];
                    for (uint32_t i = start; i < end; ++i) {
                        NodeID v = ch_.up_targets[i];
                        Weight nd = du + ch_.up_weights[i];
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
                        if (c < best) { best = c; meeting = u; }
                    }
                    uint32_t start = ch_.down_offsets[u];
                    uint32_t end = ch_.down_offsets[u + 1];
                    for (uint32_t i = start; i < end; ++i) {
                        NodeID v = ch_.down_targets[i];
                        Weight nd = du + ch_.down_weights[i];
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

Weight CHQuery::bidirectional_search(NodeID source, NodeID target,
                                     NodeID& meeting) const {
    if (source == target) {
        meeting = source;
        return 0.0;
    }

    if (!ws_.initialized) ws_.init(node_count_);
    ws_.reset();

    FourHeap fwd_pq, bwd_pq;

    ws_.set_fwd(source, 0.0, INVALID_NODE);
    ws_.set_bwd(target, 0.0, INVALID_NODE);
    fwd_pq.push(source, 0.0);
    bwd_pq.push(target, 0.0);

    Weight best = INF_WEIGHT;
    meeting = INVALID_NODE;

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
                    // Stale entry from lazy deletion — skip
                } else {
                    Weight bwd_u = ws_.get_bwd_dist(u);
                    if (bwd_u < INF_WEIGHT) {
                        Weight c = du + bwd_u;
                        if (c < best) { best = c; meeting = u; }
                    }
                    uint32_t start = ch_.up_offsets[u];
                    uint32_t end = ch_.up_offsets[u + 1];
                    for (uint32_t i = start; i < end; ++i) {
                        NodeID v = ch_.up_targets[i];
                        Weight nd = du + ch_.up_weights[i];
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
                    // Stale entry from lazy deletion — skip
                } else {
                    Weight fwd_u = ws_.get_fwd_dist(u);
                    if (fwd_u < INF_WEIGHT) {
                        Weight c = fwd_u + du;
                        if (c < best) { best = c; meeting = u; }
                    }
                    uint32_t start = ch_.down_offsets[u];
                    uint32_t end = ch_.down_offsets[u + 1];
                    for (uint32_t i = start; i < end; ++i) {
                        NodeID v = ch_.down_targets[i];
                        Weight nd = du + ch_.down_weights[i];
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

void CHQuery::unpack_edge(NodeID from, NodeID to, std::vector<NodeID>& path) const {
    auto it = ch_.unpack_map.find(ContractionResult::pack_edge(from, to));
    if (it == ch_.unpack_map.end() || it->second == INVALID_NODE) {
        // Original edge — no shortcut to expand
        path.push_back(to);
        return;
    }
    NodeID mid = it->second;
    unpack_edge(from, mid, path);
    unpack_edge(mid, to, path);
}

std::vector<NodeID> CHQuery::unpack_path(NodeID source, NodeID meeting,
                                          NodeID target) const {
    if (meeting == INVALID_NODE) return {};
    if (source == target) return {source};

    std::vector<NodeID> path;
    path.push_back(source);

    // Forward part: source → meeting via up-graph parent chain
    {
        std::vector<NodeID> fwd_chain;
        NodeID cur = meeting;
        while (cur != source) {
            fwd_chain.push_back(cur);
            cur = ws_.get_fwd_parent(cur);
            if (cur == INVALID_NODE) break;
        }
        std::reverse(fwd_chain.begin(), fwd_chain.end());

        NodeID prev = source;
        for (NodeID node : fwd_chain) {
            unpack_edge(prev, node, path);
            prev = node;
        }
    }

    // Backward part: meeting → target in original graph.
    if (meeting != target) {
        std::vector<NodeID> bwd_chain;
        NodeID cur = meeting;
        while (cur != target) {
            NodeID next = ws_.get_bwd_parent(cur);
            if (next == INVALID_NODE) break;
            bwd_chain.push_back(next);
            cur = next;
        }

        NodeID prev = meeting;
        for (NodeID node : bwd_chain) {
            unpack_edge(prev, node, path);
            prev = node;
        }
    }

    return path;
}

}  // namespace gravel
