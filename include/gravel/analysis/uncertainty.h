/// @file uncertainty.h
/// @brief Uncertainty quantification for composite fragility indices.
///
/// Composite indices like county_fragility_index combine multiple sub-metrics
/// with configurable weights and Monte Carlo sampling. Two sources of uncertainty
/// must be quantified for research validity:
///
/// 1. **Sampling variance**: Different random seeds produce different O-D samples,
///    yielding different composite scores. The ensemble approach runs N independent
///    realizations and reports mean ± SD. This is the confidence interval on the score.
///
/// 2. **Weight sensitivity**: The composite weights (bridge=0.30, connectivity=0.25,
///    accessibility=0.25, fragility=0.20) are researcher choices, not physical constants.
///    A county that ranks 10th under default weights but 3rd under slight perturbations
///    has an unstable ranking. Weight sensitivity analysis perturbs each weight and
///    measures how much the composite changes.
///
/// Both are standard robustness checks for composite indices per the OECD Handbook
/// on Constructing Composite Indicators (2008). Any dissertation committee familiar
/// with index methodology will expect them.
///
/// @section usage Example
/// @code
/// EnsembleConfig ecfg;
/// ecfg.base_config = county_config;
/// ecfg.num_runs = 20;
/// auto ens = ensemble_fragility(graph, ch, idx, ecfg);
/// // ens.mean_composite ± ens.std_composite → confidence interval
///
/// WeightSensitivityConfig wcfg;
/// wcfg.base_config = county_config;
/// auto ws = weight_sensitivity(graph, ch, idx, wcfg);
/// // ws.sensitivity_bridge → how much the score changes per unit bridge weight change
/// @endcode

#pragma once
#include "gravel/analysis/county_fragility.h"
#include <vector>

namespace gravel {

/// Configuration for ensemble (multi-seed) fragility estimation.
struct EnsembleConfig {
    CountyFragilityConfig base_config;
    uint32_t num_runs = 20;       ///< Number of independent realizations
    uint64_t base_seed = 42;      ///< Seeds are base_seed, base_seed+1, ..., base_seed+num_runs-1
};

/// Result of ensemble fragility: statistics across N independent runs.
struct UncertaintyResult {
    /// All N individual results (for detailed inspection or custom analysis).
    std::vector<CountyFragilityResult> ensemble;

    /// Summary statistics on composite_index across the ensemble.
    double mean_composite = 0.0;
    double std_composite = 0.0;
    double min_composite = 0.0;
    double max_composite = 0.0;

    /// Quartiles of composite_index across runs.
    double composite_p25 = 0.0;
    double composite_p50 = 0.0;   ///< Median
    double composite_p75 = 0.0;

    /// Coefficient of variation: std / mean. Values > 0.1 suggest the sample
    /// count is too low for stable rankings.
    double coefficient_of_variation = 0.0;
};

/// Run county_fragility_index N times with different random seeds.
/// Measures sampling variance in the composite score.
///
/// Why: The O-D sampling in county_fragility_index uses a finite number of
/// pairs (default 100). Different random seeds select different pairs, producing
/// different scores. This quantifies how much the score varies due to sampling
/// alone, separate from any real difference between counties.
UncertaintyResult ensemble_fragility(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const EnsembleConfig& config);

/// Configuration for weight sensitivity analysis.
struct WeightSensitivityConfig {
    CountyFragilityConfig base_config;

    /// Perturbation range: each weight is varied by ±this fraction.
    /// Default 0.2 means bridge_weight varies from 0.24 to 0.36 (±20% of 0.30).
    double perturbation_range = 0.2;

    /// Number of evaluation points per weight dimension.
    /// 5 points means: -20%, -10%, 0%, +10%, +20%.
    uint32_t grid_points = 5;
};

/// Result of weight sensitivity analysis.
struct WeightSensitivityResult {
    /// Sensitivity of composite_index to each weight (approximate partial derivative).
    /// Computed as (composite_max - composite_min) / (2 * perturbation_range * base_weight).
    /// Higher value = score is more sensitive to this weight choice.
    double sensitivity_bridge = 0.0;
    double sensitivity_connectivity = 0.0;
    double sensitivity_accessibility = 0.0;
    double sensitivity_fragility = 0.0;

    /// Range of composite_index observed across all weight perturbations.
    double composite_min = 0.0;
    double composite_max = 0.0;

    /// All evaluated weight configurations and their scores.
    std::vector<CountyFragilityWeights> weight_grid;
    std::vector<double> composite_values;  ///< Parallel to weight_grid
};

/// One-at-a-time weight perturbation analysis.
/// For each of the 4 composite weights, varies it across a grid while holding
/// others at their base values (renormalized to sum to 1.0).
///
/// Why: If the county ranking changes substantially when bridge_weight moves
/// from 0.30 to 0.25, the ranking is not robust to researcher weight choices.
/// Reporting sensitivity is a standard methodological requirement for any
/// composite index used in comparative analysis.
WeightSensitivityResult weight_sensitivity(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const WeightSensitivityConfig& config);

}  // namespace gravel
