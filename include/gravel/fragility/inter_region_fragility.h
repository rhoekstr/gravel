/// @file inter_region_fragility.h
/// @brief Progressive fragility analysis between adjacent regions on a reduced graph.
///
/// Operates on any `ReducedGraph` (from gravel-simplify). For each adjacent pair
/// of regions (those sharing ≥1 inter-region edge), measures how travel time
/// between their central nodes degrades as inter-region border edges are
/// progressively blocked.
///
/// Algorithm per pair (A, B):
///   1. Baseline: CH distance central_A → central_B on intact reduced graph
///   2. For each of monte_carlo_runs trials:
///      a. Pick k_max random inter-region edges from shared(A, B)
///      b. Block all k_max, run IncrementalSSSP from central_A
///      c. Record dist[central_B] at k = k_max (all blocked)
///      d. Restore edges one at a time, record at each k
///   3. Aggregate into a degradation curve + AUC metrics.

#pragma once

#include "gravel/simplify/reduced_graph.h"
#include <cstdint>
#include <string>
#include <vector>

namespace gravel {

struct InterRegionFragilityConfig {
    /// Maximum inter-region edges to block per pair (default 20).
    /// Capped at the actual number of shared edges.
    uint32_t k_max = 20;

    /// Monte Carlo trials per pair.
    uint32_t monte_carlo_runs = 10;

    uint64_t seed = 42;
};

/// Per-k-level statistics across MC trials.
struct InterRegionLevel {
    uint32_t k = 0;                         ///< Edges still blocked (k_max..0)
    double mean_seconds = 0.0;
    double std_seconds = 0.0;
    double disconnected_frac = 0.0;         ///< Fraction of MC trials that returned ∞
    std::vector<double> run_values;         ///< Per-trial values
};

/// Fragility result for one adjacent region pair.
struct InterRegionPairResult {
    std::string source_region;              ///< Region ID (e.g., "37173" FIPS)
    std::string target_region;

    Weight baseline_seconds = 0;             ///< Intact network travel time

    /// Degradation curve. curve[0] = k_max blocked (worst), curve[k_max] = fully restored.
    std::vector<InterRegionLevel> curve;

    /// AUC of (mean_seconds / baseline - 1), normalized by curve length.
    double auc_inflation = 0.0;

    /// AUC of disconnected_frac, normalized by curve length. [0, 1].
    double auc_disconnection = 0.0;

    uint32_t shared_border_edges = 0;       ///< Total inter-region edges between pair
    uint32_t k_used = 0;                    ///< min(k_max, shared_border_edges)
};

struct InterRegionFragilityResult {
    std::vector<InterRegionPairResult> pairs;  ///< Only adjacent pairs (natural adjacency).
};

/// Compute progressive inter-region fragility on a reduced graph.
InterRegionFragilityResult inter_region_fragility(
    const ReducedGraph& reduced,
    const InterRegionFragilityConfig& config = {});

}  // namespace gravel
