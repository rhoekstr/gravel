#include "args.h"
#include "gravel/core/graph_serialization.h"
#include "gravel/ch/ch_serialization.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/analysis/county_fragility.h"
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>

int cmd_county_fragility(int argc, char* argv[]) {
    auto args = Args::parse(argc, argv);

    if (args.wants_help() || !args.has("graph") || !args.has("ch") || !args.has("polygon")) {
        std::cerr << "Compute county-level fragility index within a polygon boundary.\n\n"
                  << "Usage: gravel county-fragility --graph <.gravel.meta> --ch <.gravel.ch>\n"
                  << "                              --polygon <geojson>\n\n"
                  << "Options:\n"
                  << "  --samples              O-D sample count (default: 100)\n"
                  << "  --betweenness-samples  Betweenness source samples, 0=exact (default: 0)\n"
                  << "  --seed                 Random seed (default: 42)\n\n"
                  << "Output: JSON with composite index, bridges, betweenness, algebraic\n"
                  << "        connectivity, Kirchhoff index, accessibility score.\n";
        return args.wants_help() ? 0 : 1;
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
        boundary.vertices.push_back({pt[1].get<double>(), pt[0].get<double>()});  // GeoJSON is [lon, lat]
    }

    gravel::CountyFragilityConfig config;
    config.boundary = boundary;
    if (args.has("samples")) config.od_sample_count = std::stoul(args.get("samples"));
    if (args.has("betweenness-samples")) config.betweenness_samples = std::stoul(args.get("betweenness-samples"));
    if (args.has("seed")) config.seed = std::stoull(args.get("seed"));

    auto result = gravel::county_fragility_index(*graph, ch, idx, config);

    nlohmann::json j;
    j["composite_index"] = result.composite_index;
    j["subgraph_nodes"] = result.subgraph_nodes;
    j["subgraph_edges"] = result.subgraph_edges;
    j["entry_point_count"] = result.entry_point_count;
    j["bridges_count"] = result.bridges.bridges.size();
    j["algebraic_connectivity"] = result.algebraic_connectivity;
    j["kirchhoff_index"] = result.kirchhoff_index_value;
    j["accessibility_score"] = result.accessibility.accessibility_score;
    j["sampled_fragilities_count"] = result.sampled_fragilities.size();

    std::cout << j.dump(2) << "\n";
    return 0;
}
