#include "args.h"
#include "gravel/core/graph_serialization.h"
#include "gravel/ch/ch_serialization.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/analysis/county_fragility.h"
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>

int cmd_county_index(int argc, char* argv[]) {
    auto args = Args::parse(argc, argv);

    if (!args.has("graph") || !args.has("ch") || !args.has("polygon")) {
        std::cerr << "Usage: gravel county-index --graph <.gravel.meta> --ch <.gravel.ch>"
                  << " --polygon <geojson>\n";
        return 1;
    }

    auto graph = gravel::load_graph(args.get("graph"));
    auto ch = gravel::load_ch(args.get("ch"));
    gravel::ShortcutIndex idx(ch);

    // Load polygon from GeoJSON
    std::ifstream pf(args.get("polygon"));
    if (!pf) {
        std::cerr << "Cannot open polygon file: " << args.get("polygon") << "\n";
        return 1;
    }
    auto geojson = nlohmann::json::parse(pf);

    gravel::Polygon boundary;
    auto& coords = geojson["geometry"]["coordinates"][0];
    for (auto& pt : coords) {
        boundary.vertices.push_back({pt[1].get<double>(), pt[0].get<double>()});
    }

    gravel::CountyFragilityConfig config;
    config.boundary = boundary;
    if (args.has("samples")) config.od_sample_count = std::stoul(args.get("samples"));
    if (args.has("seed")) config.seed = std::stoull(args.get("seed"));

    auto result = gravel::county_fragility_index(*graph, ch, idx, config);

    // Output just the composite index — suitable for piping to other tools
    std::cout << result.composite_index << "\n";
    return 0;
}
