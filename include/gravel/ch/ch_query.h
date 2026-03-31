#pragma once
#include "gravel/ch/contraction.h"
#include "gravel/snap/snapper.h"
#include <vector>

namespace gravel {

struct RouteResult {
    Weight distance = INF_WEIGHT;
    std::vector<NodeID> path;  // unpacked node sequence; empty if no path
};

class CHQuery {
public:
    explicit CHQuery(const ContractionResult& ch);

    // Single-pair query with path unpacking
    RouteResult route(NodeID source, NodeID target) const;

    // Distance-only query (no path unpacking — faster)
    Weight distance(NodeID source, NodeID target) const;

    // Snap-aware distance query: phantom-to-phantom via dual-seeded CH search.
    Weight distance(const SnapResult& from, const SnapResult& to) const;

    // Snap-aware route query: phantom-to-phantom with path unpacking.
    RouteResult route(const SnapResult& from, const SnapResult& to) const;

    // Distance matrix: origins x destinations.
    // Returns flat vector in row-major order (size = origins.size() * destinations.size()).
    // Uses OpenMP for parallelism when available.
    std::vector<Weight> distance_matrix(
        const std::vector<NodeID>& origins,
        const std::vector<NodeID>& destinations) const;

private:
    const ContractionResult& ch_;
    NodeID node_count_;

    struct Workspace {
        std::vector<Weight> fwd_dist;
        std::vector<Weight> bwd_dist;
        std::vector<NodeID> fwd_parent;
        std::vector<NodeID> bwd_parent;
        std::vector<uint32_t> fwd_gen;
        std::vector<uint32_t> bwd_gen;
        uint32_t generation = 0;
        bool initialized = false;

        void init(NodeID n);
        void reset();

        Weight get_fwd_dist(NodeID v) const {
            return fwd_gen[v] == generation ? fwd_dist[v] : INF_WEIGHT;
        }
        Weight get_bwd_dist(NodeID v) const {
            return bwd_gen[v] == generation ? bwd_dist[v] : INF_WEIGHT;
        }
        void set_fwd(NodeID v, Weight d, NodeID parent) {
            fwd_dist[v] = d;
            fwd_parent[v] = parent;
            fwd_gen[v] = generation;
        }
        void set_bwd(NodeID v, Weight d, NodeID parent) {
            bwd_dist[v] = d;
            bwd_parent[v] = parent;
            bwd_gen[v] = generation;
        }
        NodeID get_fwd_parent(NodeID v) const {
            return fwd_gen[v] == generation ? fwd_parent[v] : INVALID_NODE;
        }
        NodeID get_bwd_parent(NodeID v) const {
            return bwd_gen[v] == generation ? bwd_parent[v] : INVALID_NODE;
        }
    };

    mutable Workspace ws_;

    Weight bidirectional_search(NodeID source, NodeID target, NodeID& meeting) const;
    Weight bidirectional_search_phantom(const SnapResult& from, const SnapResult& to,
                                         NodeID& meeting) const;
    std::vector<NodeID> unpack_path(NodeID source, NodeID meeting, NodeID target) const;

    // Recursively unpack edge from→to using the unpack_map.
    // Appends nodes to path (does NOT include `from`, DOES include `to`).
    void unpack_edge(NodeID from, NodeID to, std::vector<NodeID>& path) const;
};

}  // namespace gravel
