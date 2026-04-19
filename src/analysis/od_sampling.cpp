#include "gravel/analysis/od_sampling.h"
#include "gravel/ch/ch_query.h"
#include <random>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

namespace gravel {

std::vector<std::pair<NodeID, NodeID>> stratified_sample(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    SamplingConfig config) {

    NodeID n = graph.node_count();
    if (n < 2) return {};

    std::mt19937_64 rng(config.seed);
    CHQuery chq(ch);

    // Build node sampler: uniform or weighted
    std::function<NodeID(std::mt19937_64&)> sample_node;
    if (!config.node_weights.empty() && config.node_weights.size() == n) {
        std::discrete_distribution<NodeID> weighted_dist(
            config.node_weights.begin(), config.node_weights.end());
        sample_node = [weighted_dist](std::mt19937_64& rng) mutable {
            return weighted_dist(rng);
        };
    } else {
        std::uniform_int_distribution<NodeID> uniform_dist(0, n - 1);
        sample_node = [uniform_dist](std::mt19937_64& rng) mutable {
            return uniform_dist(rng);
        };
    }

    // Step 1: Generate pilot sample to estimate distance distribution
    uint32_t pilot_size = std::min(static_cast<uint32_t>(500), n * (n - 1) / 2);

    std::vector<std::pair<std::pair<NodeID, NodeID>, Weight>> pilot_pairs;
    for (uint32_t i = 0; i < pilot_size; ++i) {
        NodeID s = sample_node(rng);
        NodeID t = sample_node(rng);
        if (s == t) continue;
        Weight d = chq.distance(s, t);
        if (d < INF_WEIGHT) {
            pilot_pairs.push_back({{s, t}, d});
        }
    }

    if (pilot_pairs.empty()) return {};

    // Step 2: Compute distance strata boundaries
    std::vector<Weight> pilot_dists;
    for (const auto& [pair, d] : pilot_pairs) {
        pilot_dists.push_back(d);
    }
    std::sort(pilot_dists.begin(), pilot_dists.end());

    uint32_t num_strata = std::min(config.distance_strata,
                                    static_cast<uint32_t>(pilot_dists.size()));
    std::vector<Weight> boundaries(num_strata + 1);
    boundaries[0] = 0.0;
    for (uint32_t s = 1; s < num_strata; ++s) {
        size_t idx = s * pilot_dists.size() / num_strata;
        boundaries[s] = pilot_dists[idx];
    }
    boundaries[num_strata] = INF_WEIGHT;

    // Auto-detect long distance threshold
    double ld_threshold = config.long_distance_threshold;
    if (ld_threshold == 0.0 && pilot_dists.size() >= 10) {
        // Top 30% of distances
        size_t p70 = pilot_dists.size() * 7 / 10;
        ld_threshold = pilot_dists[p70];
    }

    // Step 3: Compute samples per stratum (weighted by long-distance factor)
    std::vector<double> stratum_weight(num_strata, 1.0);
    for (uint32_t s = 0; s < num_strata; ++s) {
        Weight mid = (boundaries[s] + boundaries[s + 1]) / 2.0;
        if (mid > ld_threshold && ld_threshold > 0) {
            stratum_weight[s] = config.long_distance_weight;
        }
    }

    double total_weight = std::accumulate(stratum_weight.begin(), stratum_weight.end(), 0.0);
    std::vector<uint32_t> samples_per_stratum(num_strata);
    uint32_t assigned = 0;
    for (uint32_t s = 0; s < num_strata; ++s) {
        samples_per_stratum[s] = static_cast<uint32_t>(
            config.total_samples * stratum_weight[s] / total_weight);
        assigned += samples_per_stratum[s];
    }
    // Distribute remainder
    for (uint32_t s = 0; assigned < config.total_samples && s < num_strata; ++s) {
        samples_per_stratum[s]++;
        assigned++;
    }

    // Step 4: Generate stratified samples
    std::vector<std::pair<NodeID, NodeID>> result;
    result.reserve(config.total_samples);

    for (uint32_t s = 0; s < num_strata; ++s) {
        uint32_t needed = samples_per_stratum[s];
        uint32_t attempts = 0;
        uint32_t max_attempts = needed * 20;

        while (needed > 0 && attempts < max_attempts) {
            NodeID src = sample_node(rng);
            NodeID tgt = sample_node(rng);
            if (src == tgt) { attempts++; continue; }

            Weight d = chq.distance(src, tgt);
            if (d >= boundaries[s] && d < boundaries[s + 1]) {
                result.push_back({src, tgt});
                needed--;
            }
            attempts++;
        }
    }

    return result;
}

}  // namespace gravel
