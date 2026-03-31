#include "args.h"
#include "gravel/core/graph_serialization.h"
#include "gravel/ch/ch_serialization.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/analysis/progressive_fragility.h"
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>

int cmd_progressive_fragility(int argc, char* argv[]) {
    auto args = Args::parse(argc, argv);

    if (args.wants_help() || !args.has("graph") || !args.has("ch") || !args.has("polygon")) {
        std::cerr << "Progressive elimination fragility: degradation curve under sequential edge removal.\n\n"
                  << "Usage: gravel progressive-fragility --graph <.gravel.meta> --ch <.gravel.ch>\n"
                  << "                                   --polygon <geojson>\n"
                  << "       --strategy <monte-carlo|greedy-betweenness|greedy-fragility>\n\n"
                  << "Options:\n"
                  << "  --k-max <n>              Max edges to remove (default: 5)\n"
                  << "  --mc-runs <n>            Monte Carlo runs (default: 50)\n"
                  << "  --seed <n>               Base random seed (default: 42)\n"
                  << "  --recompute-mode <full|fast>  Greedy fragility mode (default: full)\n"
                  << "  --samples <n>            OD pairs per evaluation (default: 100)\n"
                  << "  --critical-threshold <f> Critical_k threshold (default: 0.7)\n"
                  << "  --jump-delta <f>         Jump detection threshold (default: 0.15)\n"
                  << "  --output <path>          Save JSON to file\n";
        return args.wants_help() ? 0 : 1;
    }

    auto graph = gravel::load_graph(args.get("graph"));
    auto ch = gravel::load_ch(args.get("ch"));
    gravel::ShortcutIndex idx(ch);

    // Load polygon
    std::ifstream pf(args.get("polygon"));
    auto geojson = nlohmann::json::parse(pf);
    gravel::Polygon boundary;
    auto& coords = geojson["geometry"]["coordinates"][0];
    for (auto& pt : coords) {
        boundary.vertices.push_back({pt[1].get<double>(), pt[0].get<double>()});
    }

    gravel::ProgressiveFragilityConfig cfg;
    cfg.base_config.boundary = boundary;

    // Parse strategy
    std::string strategy = args.get("strategy", "monte-carlo");
    if (strategy == "monte-carlo") cfg.selection_strategy = gravel::SelectionStrategy::MONTE_CARLO;
    else if (strategy == "greedy-betweenness") cfg.selection_strategy = gravel::SelectionStrategy::GREEDY_BETWEENNESS;
    else if (strategy == "greedy-fragility") cfg.selection_strategy = gravel::SelectionStrategy::GREEDY_FRAGILITY;

    if (args.has("k-max")) cfg.k_max = std::stoul(args.get("k-max"));
    if (args.has("mc-runs")) cfg.monte_carlo_runs = std::stoul(args.get("mc-runs"));
    if (args.has("seed")) cfg.base_seed = std::stoull(args.get("seed"));
    if (args.has("samples")) cfg.base_config.od_sample_count = std::stoul(args.get("samples"));
    if (args.has("critical-threshold")) cfg.critical_k_threshold = std::stod(args.get("critical-threshold"));
    if (args.has("jump-delta")) cfg.jump_detection_delta = std::stod(args.get("jump-delta"));

    if (args.has("recompute-mode") && args.get("recompute-mode") == "fast")
        cfg.recompute_mode = gravel::RecomputeMode::FAST_APPROXIMATE;

    auto result = gravel::progressive_fragility(*graph, ch, idx, cfg);

    // Build JSON output
    nlohmann::json j;
    j["strategy"] = strategy;
    j["k_max"] = result.k_max_used;
    j["subgraph_nodes"] = result.subgraph_nodes;
    j["subgraph_edges"] = result.subgraph_edges;
    j["auc_raw"] = result.auc_raw;
    j["auc_normalized"] = result.auc_normalized;
    j["auc_excess"] = result.auc_excess;
    j["critical_k"] = result.critical_k;
    j["jump_detected"] = result.jump_detected;
    j["jump_at_k"] = result.jump_at_k;
    j["jump_magnitude"] = result.jump_magnitude;

    nlohmann::json curve_json = nlohmann::json::array();
    for (const auto& level : result.curve) {
        nlohmann::json lj;
        lj["k"] = level.k;
        lj["mean_composite"] = level.mean_composite;
        if (cfg.selection_strategy == gravel::SelectionStrategy::MONTE_CARLO) {
            lj["std_composite"] = level.std_composite;
            lj["p25"] = level.p25;
            lj["p50"] = level.p50;
            lj["p75"] = level.p75;
            lj["p90"] = level.p90;
            lj["iqr"] = level.iqr;
            lj["fraction_disconnected"] = level.fraction_disconnected;
        } else {
            if (level.removed_edge.first != gravel::INVALID_NODE) {
                lj["removed_edge"] = {level.removed_edge.first, level.removed_edge.second};
            }
        }
        curve_json.push_back(lj);
    }
    j["curve"] = curve_json;

    nlohmann::json seq = nlohmann::json::array();
    for (auto [u, v] : result.removal_sequence) {
        seq.push_back({u, v});
    }
    j["removal_sequence"] = seq;

    std::string output = j.dump(2);
    if (args.has("output")) {
        std::ofstream of(args.get("output"));
        of << output << "\n";
    }
    std::cout << output << "\n";
    return 0;
}
