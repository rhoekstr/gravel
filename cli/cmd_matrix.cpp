#include "args.h"
#include "gravel/ch/ch_serialization.h"
#include "gravel/ch/ch_query.h"
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>

static std::vector<gravel::NodeID> parse_node_list(const std::string& s) {
    std::vector<gravel::NodeID> result;
    std::stringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ',')) {
        result.push_back(static_cast<gravel::NodeID>(std::stoul(token)));
    }
    return result;
}

int cmd_matrix(int argc, char* argv[]) {
    auto args = Args::parse(argc, argv);

    if (!args.has("ch") || !args.has("origins") || !args.has("destinations")) {
        std::cerr << "Usage: gravel matrix --ch <.gravel.ch>"
                  << " --origins <id,id,...> --destinations <id,id,...>\n";
        return 1;
    }

    auto ch = gravel::load_ch(args.get("ch"));
    gravel::CHQuery query(ch);

    auto origins = parse_node_list(args.get("origins"));
    auto dests = parse_node_list(args.get("destinations"));

    nlohmann::json j;
    j["origins"] = origins;
    j["destinations"] = dests;

    std::vector<std::vector<double>> matrix;
    for (auto src : origins) {
        std::vector<double> row;
        for (auto dst : dests) {
            auto d = query.distance(src, dst);
            row.push_back(d < gravel::INF_WEIGHT ? d : -1.0);
        }
        matrix.push_back(row);
    }
    j["distances"] = matrix;

    std::cout << j.dump(2) << "\n";
    return 0;
}
