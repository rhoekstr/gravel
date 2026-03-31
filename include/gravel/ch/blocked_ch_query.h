#pragma once
#include "gravel/ch/contraction.h"
#include "gravel/core/array_graph.h"
#include "gravel/ch/shortcut_index.h"
#include <unordered_set>
#include <utility>
#include <vector>

namespace gravel {

// CH query that can block specific original edges.
// Blocking an original edge (u,v) also blocks all shortcuts that expand through it.
// When a shortcut is blocked but the original edge exists and is unblocked,
// the original edge weight is used as a fallback (since CH deduplication may
// have replaced the original edge with a lighter shortcut).
// Thread-safe: workspace is per-instance (create one per thread for parallel use).
class BlockedCHQuery {
public:
    BlockedCHQuery(const ContractionResult& ch, const ShortcutIndex& idx,
                   const ArrayGraph& graph);

    // Distance query with blocked edges.
    Weight distance_blocking(NodeID source, NodeID target,
                              const std::vector<std::pair<NodeID, NodeID>>& blocked_edges) const;

private:
    const ContractionResult& ch_;
    const ShortcutIndex& shortcut_idx_;
    const ArrayGraph& graph_;
    NodeID node_count_;

    // Reverse adjacency for backward-search original-edge fallback.
    std::vector<uint32_t> rev_offsets_;
    std::vector<NodeID> rev_sources_;
    std::vector<Weight> rev_weights_;

    struct Workspace {
        std::vector<Weight> fwd_dist, bwd_dist;
        std::vector<NodeID> fwd_parent, bwd_parent;
        std::vector<uint32_t> fwd_gen, bwd_gen;
        uint32_t generation = 0;
        bool initialized = false;
        std::unordered_set<uint64_t> blocked_overlay_edges;

        void init(NodeID n);
        void reset();

        Weight get_fwd_dist(NodeID v) const {
            return fwd_gen[v] == generation ? fwd_dist[v] : INF_WEIGHT;
        }
        Weight get_bwd_dist(NodeID v) const {
            return bwd_gen[v] == generation ? bwd_dist[v] : INF_WEIGHT;
        }
        void set_fwd(NodeID v, Weight d, NodeID parent) {
            fwd_dist[v] = d; fwd_parent[v] = parent; fwd_gen[v] = generation;
        }
        void set_bwd(NodeID v, Weight d, NodeID parent) {
            bwd_dist[v] = d; bwd_parent[v] = parent; bwd_gen[v] = generation;
        }
    };

    mutable Workspace ws_;

    void populate_blocked_set(const std::vector<std::pair<NodeID, NodeID>>& blocked_edges) const;
    Weight bidirectional_search_blocked(NodeID source, NodeID target) const;
};

}  // namespace gravel
