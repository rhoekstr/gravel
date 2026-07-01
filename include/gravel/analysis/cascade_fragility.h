/// @file cascade_fragility.h
/// @brief Motter–Lai cascading edge failure (experimental).
///
/// Structural fragility asks "what breaks when edge X is removed?" Cascading failure
/// asks "does removing X trigger a chain reaction?" — load redistributes onto other
/// edges, any pushed past capacity fails too, and the process iterates to a fixed point.
///
/// This is the complex-networks Motter–Lai model, adapted to edges: load = edge
/// betweenness, capacity = (1+α)·initial_load (a tolerance α above normal load). It is
/// deliberately NOT a traffic-assignment model — no origin-destination demand matrix,
/// no equilibrium solve — which keeps it defensible as a covariate. Load is recomputed
/// on the degraded graph each round by masking failed edges with infinite weight, so
/// edge indexing is preserved (no subgraph re-indexing).
///
/// EXPERIMENTAL: recomputing betweenness per round is expensive; use sampled betweenness
/// on large graphs, and set BetweennessConfig.deterministic for reproducible cascades
/// (tiny FP differences near the capacity threshold can otherwise flip a marginal edge).
/// Report cascade size as a function of α, not a single α.

#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/analysis/betweenness.h"
#include <cstdint>
#include <utility>
#include <vector>

namespace gravel {

enum class CascadeCapacity {
    BETWEENNESS_TOLERANCE,  ///< capacity = (1+α)·initial_load (classic Motter–Lai).
    PCE_WEIGHTED,           ///< capacity = (1 + α·pce/mean_pce)·initial_load — high-throughput roads more robust.
};

struct CascadeFragilityConfig {
    double alpha = 0.2;  ///< Tolerance: capacity headroom above initial load.

    /// Initial failures (original node IDs). Empty ⇒ trigger the highest-initial-load edge.
    std::vector<std::pair<NodeID, NodeID>> trigger_edges;

    CascadeCapacity capacity_source = CascadeCapacity::BETWEENNESS_TOLERANCE;

    /// Per-edge PCE capacity (CSR order); required for PCE_WEIGHTED.
    std::vector<double> edge_pce;

    /// Load model. Prefer sample_sources > 0 on large graphs; deterministic = true for
    /// reproducible cascades.
    BetweennessConfig betweenness_config;

    uint32_t max_iterations = 50;  ///< Safety cap on cascade rounds.
};

struct CascadeFragilityResult {
    uint32_t cascade_size = 0;      ///< Total failed edges (including the trigger).
    double cascade_fraction = 0.0;  ///< cascade_size / edge_count.
    uint32_t iterations = 0;        ///< Rounds until the cascade stabilized.
    uint32_t trigger_size = 0;      ///< Edges in the initial trigger.
    std::vector<std::pair<NodeID, NodeID>> failed_edges;  ///< All failed edges (orig IDs).
};

struct CascadeAlphaPoint {
    double alpha = 0.0;
    double cascade_fraction = 0.0;
    uint32_t iterations = 0;
};

/// Run one cascade at config.alpha. Edges with positive initial load whose redistributed
/// load exceeds capacity fail; iterate to a fixed point. Zero-initial-load edges are not
/// subject to overload failure (they carry no load in normal operation).
CascadeFragilityResult cascade_fragility(const ArrayGraph& graph,
                                          const CascadeFragilityConfig& config);

/// Sweep α — robustness is a curve, not a point. Returns cascade fraction + iteration
/// count at each tolerance value.
std::vector<CascadeAlphaPoint> cascade_vs_alpha(const ArrayGraph& graph,
                                                 CascadeFragilityConfig config,
                                                 const std::vector<double>& alphas);

}  // namespace gravel
