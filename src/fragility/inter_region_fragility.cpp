#include "gravel/fragility/inter_region_fragility.h"
#include "gravel/core/incremental_sssp.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/ch_query.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <unordered_set>
#include <utility>
#include <vector>
#include <string>

namespace gravel {

namespace {

void compute_level_stats(InterRegionLevel& level) {
    if (level.run_values.empty()) return;

    std::vector<double> finite;
    uint32_t disconnected = 0;
    for (double v : level.run_values) {
        if (std::isinf(v) || v >= IncrementalSSSP::INF) {
            disconnected++;
        } else {
            finite.push_back(v);
        }
    }

    level.disconnected_frac = static_cast<double>(disconnected) / level.run_values.size();

    if (!finite.empty()) {
        double sum = 0.0;
        for (double v : finite) sum += v;
        level.mean_seconds = sum / finite.size();

        double sq = 0.0;
        for (double v : finite) {
            double d = v - level.mean_seconds;
            sq += d * d;
        }
        level.std_seconds = (finite.size() > 1) ? std::sqrt(sq / (finite.size() - 1)) : 0.0;
    }
}

}  // namespace

InterRegionFragilityResult inter_region_fragility(
    const ReducedGraph& reduced,
    const InterRegionFragilityConfig& config) {

    InterRegionFragilityResult result;
    if (!reduced.valid()) return result;

    auto ch = build_ch(*reduced.graph);
    CHQuery q(ch);
    auto local = build_local_graph(*reduced.graph);

    for (const auto& [pair, edges] : reduced.inter_region_edges) {
        (void)pair;  // key is (region_idx_a, region_idx_b) — we look up IDs via edges
        if (edges.empty()) continue;

        NodeID any_src = edges.front().first;
        NodeID any_tgt = edges.front().second;
        const std::string& src_rid = reduced.node_region[any_src];
        const std::string& tgt_rid = reduced.node_region[any_tgt];
        if (src_rid.empty() || tgt_rid.empty()) continue;

        auto it_src = reduced.central_of.find(src_rid);
        auto it_tgt = reduced.central_of.find(tgt_rid);
        if (it_src == reduced.central_of.end() || it_tgt == reduced.central_of.end()) continue;

        NodeID central_src = it_src->second;
        NodeID central_tgt = it_tgt->second;

        InterRegionPairResult pair_result;
        pair_result.source_region = src_rid;
        pair_result.target_region = tgt_rid;
        pair_result.shared_border_edges = static_cast<uint32_t>(edges.size());

        Weight baseline = q.distance(central_src, central_tgt);
        if (baseline >= INF_WEIGHT) continue;
        pair_result.baseline_seconds = baseline;

        uint32_t k_used = std::min(config.k_max, pair_result.shared_border_edges);
        pair_result.k_used = k_used;
        pair_result.curve.resize(k_used + 1);
        for (uint32_t k = 0; k <= k_used; ++k) {
            pair_result.curve[k].k = k_used - k;
            pair_result.curve[k].run_values.reserve(config.monte_carlo_runs);
        }

        std::vector<std::pair<NodeID, NodeID>> candidate_edges = edges;

        for (uint32_t run = 0; run < config.monte_carlo_runs; ++run) {
            std::mt19937_64 rng(config.seed + run);
            auto shuffled = candidate_edges;
            std::shuffle(shuffled.begin(), shuffled.end(), rng);

            std::unordered_set<uint64_t> blocked;
            for (uint32_t i = 0; i < k_used && i < shuffled.size(); ++i) {
                blocked.insert(IncrementalSSSP::edge_key(
                    shuffled[i].first, shuffled[i].second));
            }

            std::vector<uint32_t> sources = {central_src};
            IncrementalSSSP sssp(local, sources, blocked);

            Weight d = sssp.dist(0, central_tgt);
            pair_result.curve[0].run_values.push_back(static_cast<double>(d));

            for (uint32_t i = 0; i < k_used; ++i) {
                uint32_t restore_idx = k_used - 1 - i;
                if (restore_idx >= shuffled.size()) break;

                const auto& e = shuffled[restore_idx];
                auto targets = reduced.graph->outgoing_targets(e.first);
                auto weights = reduced.graph->outgoing_weights(e.first);
                Weight ew = 0;
                for (size_t t = 0; t < targets.size(); ++t) {
                    if (targets[t] == e.second) { ew = weights[t]; break; }
                }
                sssp.restore_edge(e.first, e.second, ew);

                Weight d_restored = sssp.dist(0, central_tgt);
                pair_result.curve[i + 1].run_values.push_back(static_cast<double>(d_restored));
            }
        }

        for (auto& level : pair_result.curve) compute_level_stats(level);

        double auc_infl = 0.0;
        double auc_disc = 0.0;
        for (const auto& level : pair_result.curve) {
            if (baseline > 0 && level.mean_seconds > 0) {
                auc_infl += (level.mean_seconds / baseline - 1.0);
            }
            auc_disc += level.disconnected_frac;
        }
        size_t denom = pair_result.curve.size();
        if (denom > 0) {
            pair_result.auc_inflation = auc_infl / denom;
            pair_result.auc_disconnection = auc_disc / denom;
        }

        result.pairs.push_back(std::move(pair_result));
    }

    return result;
}

}  // namespace gravel
