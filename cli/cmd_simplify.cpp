#include "args.h"
#include "gravel/core/graph_serialization.h"
#include "gravel/ch/ch_serialization.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/simplify/simplify.h"
#include "gravel/core/edge_labels.h"
#include <iostream>
#include <nlohmann/json.hpp>

int cmd_simplify(int argc, char* argv[]) {
    auto args = Args::parse(argc, argv);

    if (args.wants_help() || !args.has("graph")) {
        std::cerr << "Simplify a graph with configurable pruning and degradation estimation.\n\n"
                  << "Usage: gravel simplify --graph <.gravel.meta> [options]\n\n"
                  << "Stages (composable, independently optional):\n"
                  << "  --contract-degree2     Merge degree-2 node chains (lossless, default: on)\n"
                  << "  --no-degree2           Disable degree-2 contraction\n"
                  << "  --ch <.gravel.ch>      Pre-built CH (required for CH pruning + degradation)\n"
                  << "  --ch-keep-fraction F   Keep top F fraction by CH level (e.g., 0.7 = top 70%)\n"
                  << "  --no-preserve-bridges  Allow pruning bridge endpoints\n\n"
                  << "Degradation estimation:\n"
                  << "  --degradation-samples N  O-D pairs to sample (default: 1000)\n"
                  << "  --no-degradation         Skip degradation estimation\n"
                  << "  --seed N                 Random seed (default: 42)\n\n"
                  << "Output:\n"
                  << "  --output <path>        Save simplified graph to .gravel.meta\n\n"
                  << "Output: JSON with size reduction and DegradationReport.\n";
        return args.wants_help() ? 0 : 1;
    }

    auto graph = gravel::load_graph(args.get("graph"));

    gravel::ContractionResult ch;
    std::unique_ptr<gravel::ShortcutIndex> idx;
    bool has_ch = args.has("ch");
    if (has_ch) {
        ch = gravel::load_ch(args.get("ch"));
        idx = std::make_unique<gravel::ShortcutIndex>(ch);
    }

    gravel::SimplificationConfig config;
    config.contract_degree2 = !args.has("no-degree2");
    config.preserve_bridges = !args.has("no-preserve-bridges");
    config.estimate_degradation = !args.has("no-degradation") && has_ch;

    if (args.has("ch-keep-fraction")) {
        config.ch_level_keep_fraction = std::stod(args.get("ch-keep-fraction"));
    }
    if (args.has("degradation-samples")) {
        config.degradation_samples = std::stoul(args.get("degradation-samples"));
    }
    if (args.has("seed")) {
        config.seed = std::stoull(args.get("seed"));
    }

    auto result = gravel::simplify_graph(
        *graph, has_ch ? &ch : nullptr, idx.get(), config);

    // Save simplified graph if output path provided
    if (args.has("output")) {
        gravel::save_graph(*result.graph, args.get("output"));
    }

    // Output JSON report
    nlohmann::json j;
    j["original_nodes"] = result.original_nodes;
    j["original_edges"] = result.original_edges;
    j["simplified_nodes"] = result.simplified_nodes;
    j["simplified_edges"] = result.simplified_edges;
    j["node_reduction_pct"] = 100.0 * (1.0 - (double)result.simplified_nodes / result.original_nodes);
    j["edge_reduction_pct"] = 100.0 * (1.0 - (double)result.simplified_edges / result.original_edges);

    if (config.estimate_degradation) {
        auto& d = result.degradation;
        nlohmann::json deg;
        deg["od_pairs_sampled"] = d.od_pairs_sampled;
        deg["median_stretch"] = d.median_stretch;
        deg["p90_stretch"] = d.p90_stretch;
        deg["p95_stretch"] = d.p95_stretch;
        deg["p99_stretch"] = d.p99_stretch;
        deg["max_stretch"] = d.max_stretch;
        deg["mean_stretch"] = d.mean_stretch;
        deg["pairs_connected_before"] = d.pairs_connected_before;
        deg["pairs_connected_after"] = d.pairs_connected_after;
        deg["pairs_disconnected"] = d.pairs_disconnected;
        deg["connectivity_ratio"] = d.connectivity_ratio;
        deg["original_bridges"] = d.original_bridges;
        deg["preserved_bridges"] = d.preserved_bridges;
        deg["all_bridges_preserved"] = d.all_bridges_preserved;

        nlohmann::json stages = nlohmann::json::array();
        for (const auto& s : d.stages) {
            stages.push_back({
                {"stage", s.stage_name},
                {"nodes_before", s.nodes_before}, {"nodes_after", s.nodes_after},
                {"edges_before", s.edges_before}, {"edges_after", s.edges_after}
            });
        }
        deg["stages"] = stages;
        j["degradation"] = deg;
    }

    std::cout << j.dump(2) << "\n";
    return 0;
}
