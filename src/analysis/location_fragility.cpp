#include "gravel/analysis/location_fragility.h"
#include "gravel/ch/ch_query.h"
#include "gravel/core/incremental_sssp.h"
#include "gravel/core/subgraph.h"
#include "gravel/core/geo_math.h"
#include "gravel/simplify/simplify.h"
#include "gravel/analysis/betweenness.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <numeric>
#include <random>
#include <unordered_set>

namespace gravel {

namespace {

NodeID find_nearest_node(const ArrayGraph& graph, Coord center) {
    NodeID best = INVALID_NODE;
    double best_dist = std::numeric_limits<double>::infinity();
    for (NodeID v = 0; v < graph.node_count(); ++v) {
        auto coord = graph.node_coordinate(v);
        if (!coord) continue;
        double d = haversine_meters(center, *coord);
        if (d < best_dist) { best_dist = d; best = v; }
    }
    return best;
}

double bearing(Coord from, Coord to) {
    double dlon = (to.lon - from.lon) * M_PI / 180.0;
    double lat1 = from.lat * M_PI / 180.0;
    double lat2 = to.lat * M_PI / 180.0;
    double y = std::sin(dlon) * std::cos(lat2);
    double x = std::cos(lat1) * std::sin(lat2) -
               std::sin(lat1) * std::cos(lat2) * std::cos(dlon);
    double b = std::atan2(y, x);
    if (b < 0) b += 2.0 * M_PI;
    return b;
}

Polygon circle_polygon(Coord center, double radius_meters) {
    Polygon p;
    for (int i = 0; i < 8; ++i) {
        double angle = 2.0 * M_PI * i / 8.0;
        double dlat = (radius_meters / 111320.0) * std::cos(angle);
        double dlon = (radius_meters / (111320.0 * std::cos(center.lat * M_PI / 180.0))) * std::sin(angle);
        p.vertices.push_back({center.lat + dlat, center.lon + dlon});
    }
    p.vertices.push_back(p.vertices.front());
    return p;
}

struct SPEdge {
    uint32_t local_u, local_v;
    Weight weight;
    NodeID orig_u, orig_v;
};

std::vector<SPEdge> find_sp_edges(
    const LocalGraph& local,
    const std::vector<Weight>& ch_dist_local) {

    std::vector<SPEdge> sp_edges;
    for (uint32_t u = 0; u < local.n_nodes; ++u) {
        Weight du = ch_dist_local[u];
        if (du >= IncrementalSSSP::INF) continue;
        for (const auto& e : local.adj[u]) {
            Weight dv = ch_dist_local[e.to];
            if (dv >= IncrementalSSSP::INF) continue;
            Weight expected = du + e.weight;
            double eps = 1e-6 * std::max(1.0, static_cast<double>(std::max(du, dv)));
            if (std::abs(static_cast<double>(expected) - static_cast<double>(dv)) < eps) {
                sp_edges.push_back({u, e.to, e.weight,
                                    local.local_to_original[u],
                                    local.local_to_original[e.to]});
            }
        }
    }
    return sp_edges;
}

// Compute isolation risk from distances to sampled target nodes only.
struct IsolationScore {
    double isolation_risk;
    double disconnected_frac;
    double distance_inflation;
    double coverage_gap;
    std::vector<double> per_bin_inflation;
};

IsolationScore compute_isolation(
    const std::vector<Weight>& dist_blocked,
    const std::vector<Weight>& dist_full,
    const std::vector<uint32_t>& sample_nodes,
    const std::vector<uint32_t>& sample_bins,
    uint32_t angular_bins) {

    IsolationScore score{};
    uint32_t reachable_baseline = 0;
    uint32_t disconnected = 0;
    double total_inflation = 0.0;
    uint32_t inflation_count = 0;

    std::vector<double> bin_inflation(angular_bins, 0.0);
    std::vector<uint32_t> bin_count(angular_bins, 0);
    std::vector<bool> bin_reachable(angular_bins, false);

    for (size_t i = 0; i < sample_nodes.size(); ++i) {
        uint32_t v = sample_nodes[i];
        if (dist_full[v] >= IncrementalSSSP::INF) continue;
        reachable_baseline++;
        uint32_t bin = sample_bins[i];

        if (dist_blocked[v] >= IncrementalSSSP::INF) {
            disconnected++;
        } else {
            double ratio = static_cast<double>(dist_blocked[v]) /
                           std::max(1.0, static_cast<double>(dist_full[v]));
            ratio = std::min(ratio, 6.0);
            total_inflation += ratio;
            inflation_count++;
            bin_inflation[bin] += ratio;
            bin_count[bin]++;
            bin_reachable[bin] = true;
        }
    }

    if (reachable_baseline == 0) {
        score.per_bin_inflation.resize(angular_bins, 0.0);
        return score;
    }

    score.disconnected_frac = static_cast<double>(disconnected) / reachable_baseline;
    double avg_inflation = (inflation_count > 0) ? total_inflation / inflation_count : 1.0;
    score.distance_inflation = std::min(1.0, (avg_inflation - 1.0) / 5.0);

    uint32_t sectors_reachable = 0;
    for (uint32_t b = 0; b < angular_bins; ++b) {
        if (bin_reachable[b]) sectors_reachable++;
    }
    score.coverage_gap = 1.0 - static_cast<double>(sectors_reachable) / angular_bins;

    score.isolation_risk = 0.5 * score.disconnected_frac +
                           0.3 * score.distance_inflation +
                           0.2 * score.coverage_gap;
    score.isolation_risk = std::clamp(score.isolation_risk, 0.0, 1.0);

    score.per_bin_inflation.resize(angular_bins, 0.0);
    for (uint32_t b = 0; b < angular_bins; ++b) {
        if (bin_count[b] > 0)
            score.per_bin_inflation[b] = (bin_inflation[b] / bin_count[b]) - 1.0;
    }

    return score;
}

void compute_level_stats(LocationKLevel& level) {
    if (level.run_values.empty()) return;
    auto sorted = level.run_values;
    std::sort(sorted.begin(), sorted.end());
    size_t n = sorted.size();

    double sum = 0.0;
    for (double v : sorted) sum += v;
    level.mean_isolation_risk = sum / n;

    double sq = 0.0;
    for (double v : sorted) sq += (v - level.mean_isolation_risk) * (v - level.mean_isolation_risk);
    level.std_isolation_risk = (n > 1) ? std::sqrt(sq / (n - 1)) : 0.0;

    level.p50 = sorted[n / 2];
    level.p90 = sorted[std::min(n - 1, n * 9 / 10)];
}

std::vector<double> run_single_trial(
    const LocalGraph& local,
    uint32_t source_local,
    const std::vector<SPEdge>& removal_order,
    uint32_t k_edges,
    const std::vector<uint32_t>& sample_nodes,
    const std::vector<uint32_t>& sample_bins,
    uint32_t angular_bins,
    const std::vector<std::vector<Weight>>* dist_full_precomputed = nullptr) {

    std::unordered_set<uint64_t> blocked;
    for (uint32_t i = 0; i < k_edges; ++i)
        blocked.insert(IncrementalSSSP::edge_key(removal_order[i].local_u, removal_order[i].local_v));

    std::vector<uint32_t> sources = {source_local};
    std::unique_ptr<IncrementalSSSP> sssp_ptr;
    if (dist_full_precomputed)
        sssp_ptr = std::make_unique<IncrementalSSSP>(local, sources, blocked, *dist_full_precomputed);
    else
        sssp_ptr = std::make_unique<IncrementalSSSP>(local, sources, blocked);
    auto& sssp = *sssp_ptr;

    std::vector<double> curve(k_edges + 1);

    auto score = compute_isolation(sssp.dist_matrix()[0], sssp.dist_full_matrix()[0],
                                   sample_nodes, sample_bins, angular_bins);
    curve[0] = score.isolation_risk;

    for (uint32_t i = 0; i < k_edges; ++i) {
        uint32_t restore_idx = k_edges - 1 - i;
        const auto& e = removal_order[restore_idx];
        sssp.restore_edge(e.local_u, e.local_v, e.weight);

        auto sc = compute_isolation(sssp.dist_matrix()[0], sssp.dist_full_matrix()[0],
                                    sample_nodes, sample_bins, angular_bins);
        curve[i + 1] = sc.isolation_risk;
    }

    return curve;
}

}  // namespace

LocationFragilityResult location_fragility(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const LocationFragilityConfig& config) {

    LocationFragilityResult result;
    result.strategy_used = config.strategy;

    // Step 1: Find source node
    NodeID source = find_nearest_node(graph, config.center);
    if (source == INVALID_NODE) return result;

    // Step 2: Extract and simplify local subgraph
    auto polygon = circle_polygon(config.center, config.radius_meters * 1.1);
    auto sub = extract_subgraph(graph, polygon);
    if (!sub.graph || sub.graph->node_count() < 3) return result;

    // Degree-2 simplification for speed (92% reduction on real data).
    // Skip on small graphs where contraction would remove too much structure.
    SimplificationResult simplified;
    bool use_simplified = (sub.graph->node_count() > 500);
    if (use_simplified) {
        simplified = contract_degree2(*sub.graph);
        if (!simplified.graph || simplified.graph->node_count() < 10)
            use_simplified = false;
    }
    const ArrayGraph& analysis_graph = use_simplified ? *simplified.graph : *sub.graph;

    result.subgraph_nodes = analysis_graph.node_count();
    result.subgraph_edges = analysis_graph.edge_count();

    // Build local graph from simplified subgraph
    auto local = build_local_graph(analysis_graph);

    // Map: local node → original graph node (compose simplified → raw_sub → original)
    for (uint32_t i = 0; i < local.n_nodes; ++i) {
        NodeID id = local.local_to_original[i];  // = i (identity from build_local_graph)
        NodeID raw_sub_id = use_simplified ? simplified.new_to_original[id] : id;
        local.local_to_original[i] = sub.new_to_original[raw_sub_id];
    }

    // Find source in local coordinates
    uint32_t source_local = UINT32_MAX;
    for (uint32_t v = 0; v < local.n_nodes; ++v) {
        if (local.local_to_original[v] == source) { source_local = v; break; }
    }
    if (source_local == UINT32_MAX) {
        // Source was contracted away — find nearest surviving node
        double best = std::numeric_limits<double>::infinity();
        auto src_coord = graph.node_coordinate(source);
        if (!src_coord) return result;
        for (uint32_t v = 0; v < local.n_nodes; ++v) {
            auto c = graph.node_coordinate(local.local_to_original[v]);
            if (!c) continue;
            double d = haversine_meters(*src_coord, *c);
            if (d < best) { best = d; source_local = v; }
        }
    }
    if (source_local == UINT32_MAX) return result;

    // Step 3: CH distances from source to all local nodes
    CHQuery chq(ch);
    std::vector<Weight> ch_dist_local(local.n_nodes, IncrementalSSSP::INF);
    uint32_t reachable = 0;
    for (uint32_t v = 0; v < local.n_nodes; ++v) {
        Weight d = chq.distance(source, local.local_to_original[v]);
        ch_dist_local[v] = d;
        if (d < IncrementalSSSP::INF) reachable++;
    }
    result.reachable_nodes = reachable;
    if (reachable < 2) return result;

    // Step 4: Sample target nodes for scoring
    std::vector<uint32_t> reachable_nodes;
    for (uint32_t v = 0; v < local.n_nodes; ++v) {
        if (ch_dist_local[v] < IncrementalSSSP::INF && v != source_local)
            reachable_nodes.push_back(v);
    }

    std::mt19937_64 sample_rng(config.seed);
    uint32_t sample_count = std::min(config.sample_count,
                                      static_cast<uint32_t>(reachable_nodes.size()));
    std::shuffle(reachable_nodes.begin(), reachable_nodes.end(), sample_rng);
    std::vector<uint32_t> sample_nodes(reachable_nodes.begin(),
                                        reachable_nodes.begin() + sample_count);

    // Assign angular bins to sampled nodes
    double bin_width = 2.0 * M_PI / config.angular_bins;
    std::vector<uint32_t> sample_bins(sample_count, 0);
    for (uint32_t i = 0; i < sample_count; ++i) {
        auto coord = graph.node_coordinate(local.local_to_original[sample_nodes[i]]);
        if (coord) {
            double b = bearing(config.center, *coord);
            sample_bins[i] = static_cast<uint32_t>(b / bin_width) % config.angular_bins;
        }
    }

    // Step 5: Identify shortest-path edges
    auto sp_edges = find_sp_edges(local, ch_dist_local);
    result.sp_edges_total = static_cast<uint32_t>(sp_edges.size());
    if (sp_edges.empty()) return result;

    // Step 6: Determine k_edges
    uint32_t k_edges = static_cast<uint32_t>(
        std::ceil(config.removal_fraction * sp_edges.size()));
    k_edges = std::min(k_edges, static_cast<uint32_t>(sp_edges.size()));
    if (k_edges == 0) k_edges = 1;
    result.sp_edges_removed = k_edges;

    // Step 7: Precompute unblocked distances once (shared by all strategies)
    std::vector<uint32_t> sources_vec = {source_local};
    std::unordered_set<uint64_t> empty_blocked;
    IncrementalSSSP baseline_sssp(local, sources_vec, empty_blocked);
    auto dist_full_shared = baseline_sssp.dist_full_matrix();

    // Step 8: Strategy dispatch
    if (config.strategy == SelectionStrategy::MONTE_CARLO) {
        uint32_t mc_runs = config.monte_carlo_runs;
        result.curve.resize(k_edges + 1);
        for (uint32_t k = 0; k <= k_edges; ++k) {
            result.curve[k].k = k_edges - k;
            result.curve[k].run_values.reserve(mc_runs);
        }

        #pragma omp parallel for schedule(dynamic) if(mc_runs > 2)
        for (int run = 0; run < static_cast<int>(mc_runs); ++run) {
            std::mt19937_64 rng(config.seed + run);
            auto shuffled = sp_edges;
            std::shuffle(shuffled.begin(), shuffled.end(), rng);

            auto trial_curve = run_single_trial(
                local, source_local, shuffled, k_edges,
                sample_nodes, sample_bins, config.angular_bins,
                &dist_full_shared);

            #pragma omp critical
            {
                for (uint32_t i = 0; i <= k_edges; ++i)
                    result.curve[i].run_values.push_back(trial_curve[i]);
            }
        }

        for (auto& level : result.curve) compute_level_stats(level);

    } else if (config.strategy == SelectionStrategy::GREEDY_BETWEENNESS) {
        BetweennessConfig bc;
        bc.sample_sources = config.betweenness_samples;
        bc.seed = config.seed;
        auto bet = edge_betweenness(analysis_graph, bc);

        struct ScoredEdge { SPEdge edge; double betweenness; };
        std::vector<ScoredEdge> scored;
        for (const auto& spe : sp_edges) {
            double b = 0.0;
            auto targets = analysis_graph.outgoing_targets(spe.local_u);
            for (size_t i = 0; i < targets.size(); ++i) {
                if (targets[i] == spe.local_v) {
                    uint32_t edge_idx = analysis_graph.raw_offsets()[spe.local_u] + i;
                    if (edge_idx < bet.edge_scores.size()) b = bet.edge_scores[edge_idx];
                    break;
                }
            }
            scored.push_back({spe, b});
        }

        std::sort(scored.begin(), scored.end(),
                  [](const ScoredEdge& a, const ScoredEdge& b) { return a.betweenness > b.betweenness; });

        std::vector<SPEdge> removal_order;
        for (uint32_t i = 0; i < k_edges && i < scored.size(); ++i) {
            removal_order.push_back(scored[i].edge);
            result.removal_sequence.push_back({scored[i].edge.orig_u, scored[i].edge.orig_v});
        }

        auto trial_curve = run_single_trial(
            local, source_local, removal_order, k_edges,
            sample_nodes, sample_bins, config.angular_bins,
            &dist_full_shared);

        result.curve.resize(k_edges + 1);
        for (uint32_t i = 0; i <= k_edges; ++i) {
            result.curve[i].k = k_edges - i;
            result.curve[i].mean_isolation_risk = trial_curve[i];
            result.curve[i].run_values = {trial_curve[i]};
        }

    } else {
        // Greedy Fragility
        std::unordered_set<uint64_t> blocked;
        std::vector<SPEdge> remaining = sp_edges;

        result.curve.resize(k_edges + 1);
        result.curve[0].k = 0;
        result.curve[0].mean_isolation_risk = 0.0;

        for (uint32_t k = 1; k <= k_edges; ++k) {
            double best_risk = -1.0;
            size_t best_idx = 0;

            #pragma omp parallel for schedule(dynamic) if(remaining.size() > 8)
            for (int i = 0; i < static_cast<int>(remaining.size()); ++i) {
                auto trial_blocked = blocked;
                trial_blocked.insert(IncrementalSSSP::edge_key(
                    remaining[i].local_u, remaining[i].local_v));

                IncrementalSSSP sssp(local, sources_vec, trial_blocked, dist_full_shared);

                auto score = compute_isolation(sssp.dist_matrix()[0], sssp.dist_full_matrix()[0],
                                               sample_nodes, sample_bins, config.angular_bins);

                #pragma omp critical
                {
                    if (score.isolation_risk > best_risk) {
                        best_risk = score.isolation_risk;
                        best_idx = i;
                    }
                }
            }

            blocked.insert(IncrementalSSSP::edge_key(
                remaining[best_idx].local_u, remaining[best_idx].local_v));
            result.removal_sequence.push_back({remaining[best_idx].orig_u, remaining[best_idx].orig_v});

            result.curve[k].k = k;
            result.curve[k].mean_isolation_risk = best_risk;
            result.curve[k].run_values = {best_risk};

            remaining.erase(remaining.begin() + best_idx);
        }
    }

    // Step 9: Summary metrics
    if (!result.curve.empty()) {
        result.isolation_risk = result.curve[0].mean_isolation_risk;
        result.baseline_isolation_risk = result.curve.back().mean_isolation_risk;

        double auc = 0.0;
        for (const auto& level : result.curve) auc += level.mean_isolation_risk;
        result.auc_normalized = auc / result.curve.size();
    }

    // Step 10: Directional analysis at maximum removal
    {
        std::unordered_set<uint64_t> max_blocked;
        if (config.strategy == SelectionStrategy::MONTE_CARLO) {
            std::mt19937_64 rng(config.seed);
            auto shuffled = sp_edges;
            std::shuffle(shuffled.begin(), shuffled.end(), rng);
            for (uint32_t i = 0; i < k_edges; ++i)
                max_blocked.insert(IncrementalSSSP::edge_key(shuffled[i].local_u, shuffled[i].local_v));
        } else {
            for (const auto& spe : sp_edges) {
                for (const auto& [ru, rv] : result.removal_sequence) {
                    if (spe.orig_u == ru && spe.orig_v == rv) {
                        max_blocked.insert(IncrementalSSSP::edge_key(spe.local_u, spe.local_v));
                        break;
                    }
                }
            }
        }

        IncrementalSSSP sssp(local, sources_vec, max_blocked, dist_full_shared);
        auto score = compute_isolation(sssp.dist_matrix()[0], sssp.dist_full_matrix()[0],
                                       sample_nodes, sample_bins, config.angular_bins);

        result.directional_coverage = 1.0 - score.coverage_gap;
        result.directional_fragility = score.per_bin_inflation;

        double total_frag = 0.0;
        for (double f : result.directional_fragility) total_frag += f;
        if (total_frag > 0.0) {
            double hhi = 0.0;
            for (double f : result.directional_fragility) {
                double share = f / total_frag;
                hhi += share * share;
            }
            result.directional_asymmetry = hhi;
        }
    }

    return result;
}

}  // namespace gravel
