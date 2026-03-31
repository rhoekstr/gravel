#include "gravel/analysis/uncertainty.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace gravel {

UncertaintyResult ensemble_fragility(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const EnsembleConfig& config) {

    UncertaintyResult result;
    result.ensemble.resize(config.num_runs);

    // Run N independent realizations with different seeds
    for (uint32_t i = 0; i < config.num_runs; ++i) {
        auto cfg = config.base_config;
        cfg.seed = config.base_seed + i;
        result.ensemble[i] = county_fragility_index(graph, ch, idx, cfg);
    }

    // Collect composite scores
    std::vector<double> scores;
    scores.reserve(config.num_runs);
    for (const auto& r : result.ensemble) {
        scores.push_back(r.composite_index);
    }

    // Statistics
    std::sort(scores.begin(), scores.end());
    size_t n = scores.size();

    double sum = std::accumulate(scores.begin(), scores.end(), 0.0);
    result.mean_composite = sum / n;

    double sq_sum = 0.0;
    for (double s : scores) sq_sum += (s - result.mean_composite) * (s - result.mean_composite);
    result.std_composite = (n > 1) ? std::sqrt(sq_sum / (n - 1)) : 0.0;

    result.min_composite = scores.front();
    result.max_composite = scores.back();
    result.composite_p25 = scores[n * 25 / 100];
    result.composite_p50 = scores[n / 2];
    result.composite_p75 = scores[n * 75 / 100];

    result.coefficient_of_variation = (result.mean_composite > 0.0) ?
        result.std_composite / result.mean_composite : 0.0;

    return result;
}

WeightSensitivityResult weight_sensitivity(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const WeightSensitivityConfig& config) {

    WeightSensitivityResult result;
    const auto& base = config.base_config.weights;

    // Generate perturbation steps: -range, ..., 0, ..., +range
    std::vector<double> steps(config.grid_points);
    for (uint32_t i = 0; i < config.grid_points; ++i) {
        steps[i] = -config.perturbation_range +
                    2.0 * config.perturbation_range * i / (config.grid_points - 1);
    }

    // Weight names and base values for one-at-a-time perturbation
    struct WeightDim {
        double base_value;
        double* sensitivity;
    };
    double base_values[4] = {base.bridge_weight, base.connectivity_weight,
                              base.accessibility_weight, base.fragility_weight};
    double* sensitivities[4] = {&result.sensitivity_bridge, &result.sensitivity_connectivity,
                                 &result.sensitivity_accessibility, &result.sensitivity_fragility};

    result.composite_min = 1.0;
    result.composite_max = 0.0;

    // For each weight dimension, perturb while holding others at base
    for (int dim = 0; dim < 4; ++dim) {
        double dim_min = 1.0, dim_max = 0.0;

        for (double step : steps) {
            // Create perturbed weights
            CountyFragilityWeights w = base;
            double* weights[4] = {&w.bridge_weight, &w.connectivity_weight,
                                   &w.accessibility_weight, &w.fragility_weight};

            // Perturb this dimension
            *weights[dim] = base_values[dim] * (1.0 + step);
            if (*weights[dim] < 0.0) *weights[dim] = 0.0;

            // Renormalize so weights sum to 1.0
            double total = w.bridge_weight + w.connectivity_weight +
                            w.accessibility_weight + w.fragility_weight;
            if (total > 0.0) {
                w.bridge_weight /= total;
                w.connectivity_weight /= total;
                w.accessibility_weight /= total;
                w.fragility_weight /= total;
            }

            // Run fragility with perturbed weights
            auto cfg = config.base_config;
            cfg.weights = w;
            auto fr = county_fragility_index(graph, ch, idx, cfg);

            result.weight_grid.push_back(w);
            result.composite_values.push_back(fr.composite_index);

            dim_min = std::min(dim_min, fr.composite_index);
            dim_max = std::max(dim_max, fr.composite_index);
            result.composite_min = std::min(result.composite_min, fr.composite_index);
            result.composite_max = std::max(result.composite_max, fr.composite_index);
        }

        // Sensitivity = range / (2 * perturbation_range * base_weight)
        double denom = 2.0 * config.perturbation_range * base_values[dim];
        *sensitivities[dim] = (denom > 0.0) ? (dim_max - dim_min) / denom : 0.0;
    }

    return result;
}

}  // namespace gravel
