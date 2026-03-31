#include "args.h"
#include "gravel/core/graph_serialization.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/ch_serialization.h"
#include <iostream>

int cmd_build_ch(int argc, char* argv[]) {
    auto args = Args::parse(argc, argv);

    if (!args.has("input")) {
        std::cerr << "Usage: gravel build-ch --input <file.gravel.meta> --output <file.gravel.ch>\n";
        return 1;
    }

    std::string input = args.get("input");
    std::string output = args.get("output", "graph.gravel.ch");

    std::cerr << "Loading graph: " << input << "\n";
    auto graph = gravel::load_graph(input);
    std::cerr << "Nodes: " << graph->node_count()
              << "  Edges: " << graph->edge_count() << "\n";

    std::cerr << "Building contraction hierarchy...\n";
    auto ch = gravel::build_ch(*graph, {}, [](int pct) {
        std::cerr << "\r  Progress: " << pct << "%" << std::flush;
    });
    std::cerr << "\r  Progress: 100%\n";

    gravel::save_ch(ch, output);
    std::cerr << "Saved: " << output << "\n";
    return 0;
}
