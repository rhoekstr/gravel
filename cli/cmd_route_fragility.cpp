#include "args.h"
#include "gravel/core/graph_serialization.h"
#include "gravel/ch/ch_serialization.h"
#include "gravel/fragility/route_fragility.h"
#include "gravel/ch/shortcut_index.h"
#include <iostream>
#include <nlohmann/json.hpp>

int cmd_route_fragility(int argc, char* argv[]) {
    auto args = Args::parse(argc, argv);

    if (args.wants_help() || !args.has("graph") || !args.has("ch") || !args.has("from") || !args.has("to")) {
        std::cerr << "Compute edge fragility for every edge on the shortest s-t path.\n\n"
                  << "Usage: gravel route-fragility --graph <.gravel.meta> --ch <.gravel.ch>\n"
                  << "                             --from <node_id> --to <node_id>\n\n"
                  << "Output: JSON with primary distance, path, and per-edge fragility ratios.\n"
                  << "        Bottleneck edge (highest fragility ratio) is highlighted.\n";
        return args.wants_help() ? 0 : 1;
    }

    auto graph = gravel::load_graph(args.get("graph"));
    auto ch = gravel::load_ch(args.get("ch"));
    gravel::ShortcutIndex idx(ch);

    gravel::NodeID from = static_cast<gravel::NodeID>(std::stoul(args.get("from")));
    gravel::NodeID to = static_cast<gravel::NodeID>(std::stoul(args.get("to")));

    auto result = gravel::route_fragility(ch, idx, *graph, from, to);

    nlohmann::json j;
    j["from"] = from;
    j["to"] = to;

    if (result.valid()) {
        j["primary_distance"] = result.primary_distance;
        j["primary_path"] = result.primary_path;

        nlohmann::json frags = nlohmann::json::array();
        for (const auto& ef : result.edge_fragilities) {
            nlohmann::json e;
            e["source"] = ef.source;
            e["target"] = ef.target;
            e["replacement_distance"] = ef.replacement_distance < gravel::INF_WEIGHT
                                        ? nlohmann::json(ef.replacement_distance)
                                        : nlohmann::json(nullptr);
            e["fragility_ratio"] = std::isinf(ef.fragility_ratio)
                                   ? nlohmann::json(nullptr)
                                   : nlohmann::json(ef.fragility_ratio);
            frags.push_back(e);
        }
        j["edge_fragilities"] = frags;

        size_t bi = result.bottleneck_index();
        j["bottleneck"] = {
            {"edge_index", bi},
            {"source", result.edge_fragilities[bi].source},
            {"target", result.edge_fragilities[bi].target},
            {"fragility_ratio", std::isinf(result.edge_fragilities[bi].fragility_ratio)
                                ? nlohmann::json(nullptr)
                                : nlohmann::json(result.edge_fragilities[bi].fragility_ratio)}
        };
    } else {
        j["error"] = "no path found";
    }

    std::cout << j.dump(2) << "\n";
    return 0;
}
