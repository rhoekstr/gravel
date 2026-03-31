#include "args.h"
#include "gravel/core/graph_serialization.h"
#include "gravel/ch/ch_serialization.h"
#include "gravel/fragility/route_fragility.h"
#include "gravel/ch/shortcut_index.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>

int cmd_batch_fragility(int argc, char* argv[]) {
    auto args = Args::parse(argc, argv);

    if (!args.has("graph") || !args.has("ch") || !args.has("pairs")) {
        std::cerr << "Usage: gravel batch-fragility --graph <.gravel.meta> --ch <.gravel.ch>"
                  << " --pairs <pairs.csv>\n"
                  << "  pairs.csv: one line per pair, format: source,target\n";
        return 1;
    }

    auto graph = gravel::load_graph(args.get("graph"));
    auto ch = gravel::load_ch(args.get("ch"));
    gravel::ShortcutIndex idx(ch);

    // Read pairs from CSV.
    std::vector<std::pair<gravel::NodeID, gravel::NodeID>> pairs;
    {
        std::ifstream f(args.get("pairs"));
        if (!f) {
            std::cerr << "Cannot open pairs file: " << args.get("pairs") << "\n";
            return 1;
        }
        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream iss(line);
            gravel::NodeID s, t;
            char comma;
            if (iss >> s >> comma >> t)
                pairs.push_back({s, t});
        }
    }

    auto results = gravel::batch_fragility(ch, idx, *graph, pairs);

    nlohmann::json j = nlohmann::json::array();
    for (size_t p = 0; p < results.size(); ++p) {
        nlohmann::json entry;
        entry["from"] = pairs[p].first;
        entry["to"] = pairs[p].second;
        entry["primary_distance"] = results[p].valid()
                                    ? nlohmann::json(results[p].primary_distance)
                                    : nlohmann::json(nullptr);

        if (results[p].valid() && !results[p].edge_fragilities.empty()) {
            size_t bi = results[p].bottleneck_index();
            const auto& bn = results[p].edge_fragilities[bi];
            entry["bottleneck_edge"] = {bn.source, bn.target};
            entry["bottleneck_ratio"] = std::isinf(bn.fragility_ratio)
                                        ? nlohmann::json(nullptr)
                                        : nlohmann::json(bn.fragility_ratio);
            entry["num_path_edges"] = results[p].edge_fragilities.size();
        }

        j.push_back(entry);
    }

    std::cout << j.dump(2) << "\n";
    return 0;
}
