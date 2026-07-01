#include "gravel/analysis/stochastic_fragility.h"

#include "gravel/ch/ch_query.h"
#include "gravel/ch/blocked_ch_query.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <stdexcept>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace gravel {

namespace {

// Build probe O-D pairs and their intact-network baseline distances. Only pairs
// reachable in the intact graph are kept (infinite-baseline pairs are dropped).
void build_probes(const ArrayGraph& graph, const ContractionResult& ch,
                  const StochasticFragilityConfig& config,
                  std::vector<std::pair<NodeID, NodeID>>& pairs,
                  std::vector<Weight>& baseline) {
    CHQuery query(ch);
    const NodeID n = graph.node_count();
    if (n == 0) return;

    auto add_pair = [&](NodeID s, NodeID t) {
        if (s == t || s >= n || t >= n) return;
        Weight d = query.distance(s, t);
        if (d < INF_WEIGHT && d > 0.0) {
            pairs.push_back({s, t});
            baseline.push_back(d);
        }
    };

    if (config.target == StochasticTarget::INTER_REGION) {
        for (const auto& [s, t] : config.od_pairs) add_pair(s, t);
        return;
    }

    std::mt19937_64 rng(config.seed);
    std::uniform_int_distribution<NodeID> node_dist(0, n - 1);
    const std::size_t want = config.od_sample_count;
    const int max_attempts = static_cast<int>(want) * 40 + 100;

    if (config.target == StochasticTarget::LOCATION_ISOLATION) {
        NodeID center = (config.center != INVALID_NODE && config.center < n)
                            ? config.center : 0;
        for (int a = 0; a < max_attempts && pairs.size() < want; ++a)
            add_pair(center, node_dist(rng));
    } else {  // OD_DISTANCE_INFLATION
        for (int a = 0; a < max_attempts && pairs.size() < want; ++a)
            add_pair(node_dist(rng), node_dist(rng));
    }
}

}  // namespace

StochasticFragilityResult stochastic_fragility(
    const ArrayGraph& graph, const ContractionResult& ch, const ShortcutIndex& idx,
    const std::vector<double>& edge_probabilities,
    const StochasticFragilityConfig& config) {

    const EdgeID m = graph.edge_count();
    if (edge_probabilities.size() != static_cast<std::size_t>(m)) {
        throw std::invalid_argument(
            "stochastic_fragility: edge_probabilities length must equal edge_count");
    }

    StochasticFragilityResult result;

    // Per-edge source node, for turning a failed edge index into a blocked (u,v) edge.
    const auto& offsets = graph.raw_offsets();
    const auto& targets = graph.raw_targets();
    std::vector<NodeID> src_of_edge(m);
    for (NodeID u = 0; u + 1 < offsets.size(); ++u)
        for (uint32_t e = offsets[u]; e < offsets[u + 1]; ++e)
            src_of_edge[e] = u;

    std::vector<std::pair<NodeID, NodeID>> pairs;
    std::vector<Weight> baseline;
    build_probes(graph, ch, config, pairs, baseline);
    result.probe_pairs = static_cast<uint32_t>(pairs.size());
    if (pairs.empty()) return result;

    const int runs = static_cast<int>(config.monte_carlo_runs);
    if (runs <= 0) return result;
    result.run_values.assign(runs, 0.0);
    result.run_disconnected.assign(runs, 0.0);

    std::vector<double> prob(edge_probabilities);
    for (auto& p : prob) p = std::clamp(p, 0.0, 1.0);

    // Per-edge failure counts, summed across runs. Integer addition is associative, so the
    // thread-local → shared reduction below is thread-count invariant.
    std::vector<uint64_t> total_fail(m, 0);

    #pragma omp parallel if(runs > 2)
    {
        BlockedCHQuery blocked(ch, idx, graph);
        std::vector<std::pair<NodeID, NodeID>> failed;
        std::vector<uint64_t> local_fail(m, 0);

        #pragma omp for schedule(dynamic)
        for (int r = 0; r < runs; ++r) {
            // run r uses a distinct seed → per-run reproducibility, order-independent.
            std::mt19937_64 rng(config.seed + static_cast<uint64_t>(r) + 1);
            std::uniform_real_distribution<double> unif(0.0, 1.0);
            failed.clear();
            for (EdgeID e = 0; e < m; ++e)
                if (prob[e] > 0.0 && unif(rng) < prob[e]) {
                    failed.push_back({src_of_edge[e], targets[e]});
                    ++local_fail[e];
                }

            double sum_infl = 0.0;
            int connected = 0, disconnected = 0;
            for (std::size_t i = 0; i < pairs.size(); ++i) {
                Weight d = blocked.distance_blocking(pairs[i].first, pairs[i].second, failed);
                if (d < INF_WEIGHT) {
                    sum_infl += d / baseline[i];
                    ++connected;
                } else {
                    ++disconnected;
                }
            }
            result.run_values[r] = connected > 0 ? sum_infl / connected : 0.0;
            result.run_disconnected[r] =
                static_cast<double>(disconnected) / static_cast<double>(pairs.size());
        }

        #pragma omp critical
        for (EdgeID e = 0; e < m; ++e) total_fail[e] += local_fail[e];
    }

    // Per-edge empirical failure probability (CSR order) — for visualization / mapping.
    result.edge_failure_frequency.assign(m, 0.0);
    for (EdgeID e = 0; e < m; ++e)
        result.edge_failure_frequency[e] =
            static_cast<double>(total_fail[e]) / static_cast<double>(runs);

    // Aggregate on a sorted copy → statistics are thread-count invariant.
    result.runs = static_cast<uint32_t>(runs);
    std::vector<double> sorted = result.run_values;
    std::sort(sorted.begin(), sorted.end());
    const std::size_t nr = sorted.size();

    const double sum = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    result.mean = sum / static_cast<double>(nr);
    double sq = 0.0;
    for (double v : sorted) sq += (v - result.mean) * (v - result.mean);
    result.std_dev = nr > 1 ? std::sqrt(sq / static_cast<double>(nr - 1)) : 0.0;
    result.p50 = sorted[nr / 2];
    result.p90 = sorted[std::min(nr - 1, nr * 9 / 10)];
    result.p99 = sorted[std::min(nr - 1, nr * 99 / 100)];

    const double disc_sum = std::accumulate(result.run_disconnected.begin(),
                                            result.run_disconnected.end(), 0.0);
    result.mean_disconnected_fraction = disc_sum / static_cast<double>(nr);

    result.exceedance.reserve(config.exceedance_thresholds.size());
    for (double thr : config.exceedance_thresholds) {
        int cnt = 0;
        for (double v : result.run_values) if (v > thr) ++cnt;
        result.exceedance.push_back(static_cast<double>(cnt) / static_cast<double>(nr));
    }

    return result;
}

}  // namespace gravel
