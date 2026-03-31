#include "args.h"
#include "gravel/core/csv_graph.h"
#include "gravel/core/graph_serialization.h"
#include <iostream>

int cmd_build_graph(int argc, char* argv[]) {
    auto args = Args::parse(argc, argv);

    if (!args.has("csv")) {
        std::cerr << "Usage: gravel build-graph --csv <file.csv> --output <file.gravel.meta>\n"
                  << "  [--source-col <name>] [--target-col <name>] [--weight-col <name>]\n"
                  << "  [--delimiter <char>] [--bidirectional]\n";
        return 1;
    }

    gravel::CSVConfig cfg;
    cfg.path = args.get("csv");
    cfg.source_col = args.get("source-col", "source");
    cfg.target_col = args.get("target-col", "target");
    cfg.weight_col = args.get("weight-col", "weight");
    cfg.bidirectional = args.has("bidirectional");
    if (args.has("delimiter")) {
        cfg.delimiter = args.get("delimiter")[0];
    }

    std::string output = args.get("output", "graph.gravel.meta");

    std::cerr << "Loading CSV: " << cfg.path << "\n";
    auto graph = gravel::load_csv_graph(cfg);
    std::cerr << "Nodes: " << graph->node_count()
              << "  Edges: " << graph->edge_count() << "\n";

    gravel::save_graph(*graph, output);
    std::cerr << "Saved: " << output << "\n";
    return 0;
}
