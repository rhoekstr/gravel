#pragma once
#include "gravel/ch/contraction.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/core/array_graph.h"
#include "gravel/core/types.h"

#include <cstdint>
#include <vector>

namespace gravel {

/// Options for edge_fragility(). Both measures are on by default; turning one off skips its cost
/// (``compute_stranded=false`` avoids the bridge tree; ``compute_ratio=false`` avoids all CH queries).
struct EdgeFragilityConfig {
    bool compute_ratio = true;     ///< path-inflation ratio per non-bridge edge (blocked CH queries)
    bool compute_stranded = true;  ///< nodes stranded per bridge (bridge tree over 2-edge-connected comps)
};

/// Whole-graph per-edge fragility, aligned with CSR edge order (matches ``Graph::to_coo()``).
/// One entry per directed edge; the two directed halves of an undirected edge carry the same value.
struct EdgeFragilityResult {
    std::vector<double> fragility_ratio;         ///< replacement / primary (>= 1; INF if the edge is a bridge)
    std::vector<Weight> replacement_distance;    ///< shortest endpoint distance with the edge removed (INF if bridge)
    std::vector<uint32_t> stranded_count;        ///< nodes disconnected if the edge fails (0 if not a bridge)
    std::vector<uint8_t> is_bridge;              ///< 1 if removing the edge disconnects its endpoints
};

/// Per-edge fragility over the whole graph — the generalization of ``route_fragility`` from a
/// single s-t path to every edge. For each edge it reports how much the shortest path between its
/// endpoints inflates when the edge is removed (the *path-inflation ratio*, INF for a bridge), and,
/// for bridges, how many nodes it strands.
///
/// Bridges are found once with ``find_bridges`` (Tarjan, O(V+E)); non-bridge ratios use
/// ``BlockedCHQuery`` (parallelized with OpenMP over edges); cut sizes come from a bridge tree over
/// the 2-edge-connected components. On meshed networks the ratio is the informative signal; on
/// near-radial networks (where most edges are bridges) the stranded count is.
EdgeFragilityResult edge_fragility(const ContractionResult& ch,
                                   const ShortcutIndex& shortcut_idx,
                                   const ArrayGraph& graph,
                                   EdgeFragilityConfig config = {});

}  // namespace gravel
