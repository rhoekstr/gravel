/// @file stochastic_fragility.h
/// @brief Fragility as a distribution under stochastic per-edge failures.
///
/// Point-estimate fragility answers "what if exactly these edges fail?" Stochastic
/// fragility answers "given each edge fails with probability p_e, what is the
/// *distribution* of network degradation?" — yielding a covariate with uncertainty
/// bounds rather than a single number.
///
/// Each Monte Carlo realization samples a failure set (edge e fails with probability
/// edge_probabilities[e], CSR edge order), then measures distance inflation and
/// disconnection across a set of probe O-D pairs. The three targets differ only in how
/// the probe pairs are chosen; the measurement and aggregation are shared.
///
/// The per-edge probabilities are an input array — the derivation (hazard footprint,
/// bridge/flood exposure, floodplain intersection) lives in geo/Python, keeping the
/// DAG intact. Uses BlockedCHQuery (no CH rebuild); parallel over runs; seeded with an
/// ordered reduction so reported statistics are thread-count invariant.

#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/shortcut_index.h"
#include <cstdint>
#include <utility>
#include <vector>

namespace gravel {

/// How the probe O-D pairs are chosen (the only thing that differs between targets).
enum class StochasticTarget {
    OD_DISTANCE_INFLATION,  ///< Random reachable O-D pairs (default; general covariate).
    LOCATION_ISOLATION,     ///< Pairs from `center` to sampled targets (a place's isolation).
    INTER_REGION,           ///< Use the explicit `od_pairs` (e.g. region-centroid pairs).
};

struct StochasticFragilityConfig {
    uint32_t monte_carlo_runs = 100;  ///< Realizations; run i uses seed + i.
    uint64_t seed = 42;
    StochasticTarget target = StochasticTarget::OD_DISTANCE_INFLATION;

    uint32_t od_sample_count = 50;    ///< Probe-pair count for OD / LOCATION targets.
    NodeID center = INVALID_NODE;     ///< LOCATION_ISOLATION source node.
    std::vector<std::pair<NodeID, NodeID>> od_pairs;  ///< INTER_REGION / explicit pairs.

    /// Inflation-ratio thresholds for the exceedance curve (P(mean inflation > t)).
    std::vector<double> exceedance_thresholds = {1.5, 2.0, 3.0};
};

struct StochasticFragilityResult {
    /// Distribution of per-run mean distance inflation over *connected* probe pairs.
    double mean = 0.0;
    double std_dev = 0.0;
    double p50 = 0.0, p90 = 0.0, p99 = 0.0;

    /// Mean over runs of the fraction of probe pairs left disconnected by the failures.
    double mean_disconnected_fraction = 0.0;

    /// Aligned with config.exceedance_thresholds: fraction of runs whose mean inflation
    /// exceeded each threshold.
    std::vector<double> exceedance;

    /// Per-run mean inflation (run order — deterministic) and per-run disconnected frac.
    std::vector<double> run_values;
    std::vector<double> run_disconnected;

    uint32_t runs = 0;
    uint32_t probe_pairs = 0;
};

/// Monte Carlo fragility under independent per-edge failures. `edge_probabilities` is
/// per-edge (CSR edge order, length == edge_count); values outside [0,1] are clamped.
StochasticFragilityResult stochastic_fragility(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const std::vector<double>& edge_probabilities,
    const StochasticFragilityConfig& config = {});

}  // namespace gravel
