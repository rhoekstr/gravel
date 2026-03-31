#include "args.h"
#include "gravel/core/graph_serialization.h"
#include "gravel/ch/ch_serialization.h"
#include "gravel/analysis/location_fragility.h"
#include <iostream>
#include <cmath>
#include <nlohmann/json.hpp>

int cmd_location_fragility(int argc, char* argv[]) {
    auto args = Args::parse(argc, argv);

    if (args.wants_help() || !args.has("graph") || !args.has("ch") || !args.has("lat") || !args.has("lon")) {
        std::cerr << "Compute isolation risk for a specific location.\n\n"
                  << "Extracts local subgraph, identifies shortest-path edges, removes\n"
                  << "a fraction, and measures degradation via incremental SSSP.\n\n"
                  << "Usage: gravel location-fragility --graph <.gravel.meta> --ch <.gravel.ch>\n"
                  << "                                --lat <latitude> --lon <longitude>\n\n"
                  << "Options:\n"
                  << "  --radius-miles    Search radius in miles (default: 50)\n"
                  << "  --removal-frac    Fraction of SP edges to remove (default: 0.10)\n"
                  << "  --strategy        monte-carlo|greedy-betweenness|greedy-fragility\n"
                  << "  --mc-runs         Monte Carlo trials (default: 20)\n"
                  << "  --seed            Random seed (default: 42)\n\n"
                  << "Output: JSON with isolation_risk, degradation curve, directional coverage.\n";
        return args.wants_help() ? 0 : 1;
    }

    auto graph = gravel::load_graph(args.get("graph"));
    auto ch = gravel::load_ch(args.get("ch"));

    gravel::LocationFragilityConfig config;
    config.center.lat = std::stod(args.get("lat"));
    config.center.lon = std::stod(args.get("lon"));

    if (args.has("radius-miles"))
        config.radius_meters = std::stod(args.get("radius-miles")) * 1609.34;
    if (args.has("removal-frac"))
        config.removal_fraction = std::stof(args.get("removal-frac"));
    if (args.has("mc-runs"))
        config.monte_carlo_runs = std::stoul(args.get("mc-runs"));
    if (args.has("seed"))
        config.seed = std::stoull(args.get("seed"));
    if (args.has("strategy")) {
        auto s = args.get("strategy");
        if (s == "greedy-betweenness") config.strategy = gravel::SelectionStrategy::GREEDY_BETWEENNESS;
        else if (s == "greedy-fragility") config.strategy = gravel::SelectionStrategy::GREEDY_FRAGILITY;
        else config.strategy = gravel::SelectionStrategy::MONTE_CARLO;
    }

    auto result = gravel::location_fragility(*graph, ch, config);

    nlohmann::json j;
    j["isolation_risk"] = result.isolation_risk;
    j["baseline_isolation_risk"] = result.baseline_isolation_risk;
    j["auc_normalized"] = result.auc_normalized;
    j["directional_coverage"] = result.directional_coverage;
    j["directional_asymmetry"] = result.directional_asymmetry;
    j["reachable_nodes"] = result.reachable_nodes;
    j["sp_edges_total"] = result.sp_edges_total;
    j["sp_edges_removed"] = result.sp_edges_removed;
    j["subgraph_nodes"] = result.subgraph_nodes;
    j["subgraph_edges"] = result.subgraph_edges;

    nlohmann::json curve_json = nlohmann::json::array();
    for (const auto& kl : result.curve) {
        curve_json.push_back({
            {"k", kl.k},
            {"mean_isolation_risk", kl.mean_isolation_risk},
            {"std", kl.std_isolation_risk}
        });
    }
    j["curve"] = curve_json;

    std::cout << j.dump(2) << "\n";
    return 0;
}
