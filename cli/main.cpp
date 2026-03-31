#include "args.h"
#include <iostream>
#include <string>
#include <vector>

// Forward declarations for subcommands
int cmd_build_graph(int argc, char* argv[]);
int cmd_build_ch(int argc, char* argv[]);
int cmd_route(int argc, char* argv[]);
int cmd_matrix(int argc, char* argv[]);
int cmd_validate(int argc, char* argv[]);
int cmd_route_fragility(int argc, char* argv[]);
int cmd_batch_fragility(int argc, char* argv[]);
int cmd_county_fragility(int argc, char* argv[]);
int cmd_county_index(int argc, char* argv[]);
int cmd_location_fragility(int argc, char* argv[]);
int cmd_snap(int argc, char* argv[]);
int cmd_simplify(int argc, char* argv[]);
int cmd_progressive_fragility(int argc, char* argv[]);

static void print_usage() {
    std::cerr << "gravel v1.0 — Graph Routing and Vulnerability Analysis Library\n\n"
              << "Usage: gravel <command> [options]\n\n"
              << "Preprocessing:\n"
              << "  build-graph         Convert CSV/OSM to .gravel.meta\n"
              << "  build-ch            Build contraction hierarchy (.gravel.ch)\n\n"
              << "Routing:\n"
              << "  route               Query single shortest path\n"
              << "  matrix              Compute distance matrix\n"
              << "  snap                Snap coordinate to nearest road edge\n\n"
              << "Fragility:\n"
              << "  route-fragility     Edge fragility for a single route\n"
              << "  batch-fragility     Batch fragility for multiple O-D pairs\n"
              << "  county-fragility    County-level fragility index (full JSON)\n"
              << "  county-index        County composite index score\n"
              << "  location-fragility  Isolation risk for a specific location\n"
              << "  progressive-fragility  Multi-edge failure degradation curve\n\n"
              << "Simplification:\n"
              << "  simplify            Reduce graph size with degradation estimation\n\n"
              << "Validation:\n"
              << "  validate            Validate CH against reference Dijkstra\n\n"
              << "Run 'gravel <command> --help' for command-specific options.\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string cmd = argv[1];

    if (cmd == "build-graph")  return cmd_build_graph(argc - 1, argv + 1);
    if (cmd == "build-ch")     return cmd_build_ch(argc - 1, argv + 1);
    if (cmd == "route")        return cmd_route(argc - 1, argv + 1);
    if (cmd == "matrix")       return cmd_matrix(argc - 1, argv + 1);
    if (cmd == "validate")     return cmd_validate(argc - 1, argv + 1);
    if (cmd == "route-fragility")  return cmd_route_fragility(argc - 1, argv + 1);
    if (cmd == "batch-fragility")  return cmd_batch_fragility(argc - 1, argv + 1);
    if (cmd == "county-fragility") return cmd_county_fragility(argc - 1, argv + 1);
    if (cmd == "county-index")     return cmd_county_index(argc - 1, argv + 1);
    if (cmd == "location-fragility") return cmd_location_fragility(argc - 1, argv + 1);
    if (cmd == "snap")               return cmd_snap(argc - 1, argv + 1);
    if (cmd == "simplify")           return cmd_simplify(argc - 1, argv + 1);
    if (cmd == "progressive-fragility") return cmd_progressive_fragility(argc - 1, argv + 1);

    if (cmd == "--help" || cmd == "-h") {
        print_usage();
        return 0;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    print_usage();
    return 1;
}
