#include "args.h"
#include "gravel/core/graph_serialization.h"
#include "gravel/ch/ch_serialization.h"
#include "gravel/ch/ch_query.h"
#include <iostream>
#include <nlohmann/json.hpp>

int cmd_route(int argc, char* argv[]) {
    auto args = Args::parse(argc, argv);

    if (!args.has("graph") || !args.has("ch") || !args.has("from") || !args.has("to")) {
        std::cerr << "Usage: gravel route --graph <.gravel.meta> --ch <.gravel.ch>"
                  << " --from <node_id> --to <node_id>\n";
        return 1;
    }

    auto graph = gravel::load_graph(args.get("graph"));
    auto ch = gravel::load_ch(args.get("ch"));
    gravel::CHQuery query(ch);

    gravel::NodeID from = static_cast<gravel::NodeID>(std::stoul(args.get("from")));
    gravel::NodeID to = static_cast<gravel::NodeID>(std::stoul(args.get("to")));

    auto result = query.route(from, to);

    nlohmann::json j;
    j["from"] = from;
    j["to"] = to;

    if (result.distance < gravel::INF_WEIGHT) {
        j["distance"] = result.distance;
        j["path"] = result.path;
    } else {
        j["distance"] = nullptr;
        j["path"] = nullptr;
        j["error"] = "no path found";
    }

    std::cout << j.dump(2) << "\n";
    return 0;
}
