/// @file location_fragility.h
/// @brief Location-based isolation fragility via Dijkstra + incremental SSSP.
///
/// Answers "how isolated does this location become as roads fail?"
///
/// The algorithm:
/// 1. CH query to find all nodes within radius and their baseline distances
/// 2. Identify shortest-path edges via DAG criterion (O(E_local), no path unpacking)
/// 3. Remove a fraction of SP edges, run one Dijkstra on reduced graph
/// 4. Incrementally restore edges with bounded propagation
/// 5. Score each k-level: disconnected fraction + distance inflation + coverage gap
///
/// Three selection strategies for which edges to remove:
/// - MONTE_CARLO: random shuffle of SP edges, N independent trials
/// - GREEDY_BETWEENNESS: remove highest-betweenness SP edges first
/// - GREEDY_FRAGILITY: remove edge causing worst isolation per step
///
/// Performance: ~5s per MC trial on 200K-node real-world graphs.

#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/ch/contraction.h"
#include "gravel/analysis/progressive_fragility.h"  // SelectionStrategy enum
#include <cstdint>
#include <vector>

namespace gravel {

struct LocationFragilityConfig {
    Coord center;
    double radius_meters = 80467.0;        ///< 50 miles default
    uint32_t angular_bins = 8;              ///< Compass sectors for directional coverage

    /// Fraction of shortest-path edges to remove (default 10%).
    float removal_fraction = 0.10f;

    /// Target nodes to sample for scoring (default 200).
    /// Larger = more accurate but slower. The subgraph is degree-2 simplified
    /// before sampling, so this operates on a compact graph.
    uint32_t sample_count = 200;

    /// Edge selection strategy.
    SelectionStrategy strategy = SelectionStrategy::MONTE_CARLO;

    /// Monte Carlo trials (MONTE_CARLO strategy only).
    uint32_t monte_carlo_runs = 20;

    /// Betweenness sample sources (GREEDY_BETWEENNESS only). 0 = exact.
    uint32_t betweenness_samples = 100;

    uint64_t seed = 42;
};

/// Per-k-level statistics on the isolation degradation curve.
struct LocationKLevel {
    uint32_t k = 0;                         ///< Edges still removed at this level

    double mean_isolation_risk = 0.0;
    double std_isolation_risk = 0.0;
    double p50 = 0.0;
    double p90 = 0.0;

    double mean_disconnected_frac = 0.0;
    double mean_distance_inflation = 0.0;

    std::vector<double> run_values;         ///< All MC trial values at this k
};

/// Full result of location isolation fragility analysis.
struct LocationFragilityResult {
    /// Peak isolation risk (at maximum removal).
    double isolation_risk = 0.0;

    /// Degradation curve: curve[0] = all edges removed, curve[k_max] = fully restored.
    /// Length = sp_edges_removed + 1.
    std::vector<LocationKLevel> curve;

    /// AUC of mean isolation risk, normalized by curve length.
    double auc_normalized = 0.0;

    /// Baseline isolation risk (no edges removed, should be ~0).
    double baseline_isolation_risk = 0.0;

    // --- Directional analysis (at maximum removal) ---
    double directional_coverage = 0.0;      ///< Fraction of sectors reachable [0, 1]
    std::vector<double> directional_fragility; ///< Per-bin mean distance inflation
    double directional_asymmetry = 0.0;     ///< HHI of directional fragility

    // --- Greedy strategies: ordered removal sequence ---
    std::vector<std::pair<NodeID, NodeID>> removal_sequence;

    // --- Metadata ---
    SelectionStrategy strategy_used = SelectionStrategy::MONTE_CARLO;
    uint32_t reachable_nodes = 0;           ///< Nodes reachable at baseline
    uint32_t sp_edges_total = 0;            ///< Total shortest-path edges found
    uint32_t sp_edges_removed = 0;          ///< Edges removed (k_max)
    uint32_t subgraph_nodes = 0;
    uint32_t subgraph_edges = 0;
};

/// Compute location-based isolation fragility.
///
/// Extracts a local subgraph within radius, identifies shortest-path edges,
/// removes a fraction of them, then incrementally restores to build the
/// degradation curve.
LocationFragilityResult location_fragility(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const LocationFragilityConfig& config);

}  // namespace gravel
