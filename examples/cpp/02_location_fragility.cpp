// Location fragility example — measure isolation risk for a geographic point.
//
// Requires: libosmium
//
// Build: g++ -std=c++20 -I../../include 02_location_fragility.cpp \
//            -L../../build -lgravel -losmium -lz -lbz2 -lexpat \
//            -o location_fragility

#include <gravel/geo/osm_graph.h>
#include <gravel/ch/contraction.h>
#include <gravel/analysis/location_fragility.h>

#include <iostream>

int main(int argc, char* argv[]) {
    using namespace gravel;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <county.osm.pbf>\n";
        return 1;
    }

    // Load OSM PBF
    std::cout << "Loading " << argv[1] << "...\n";
    OSMConfig osm_cfg;
    osm_cfg.pbf_path = argv[1];
    osm_cfg.speed_profile = SpeedProfile::car();
    auto graph = load_osm_graph(osm_cfg);
    std::cout << "  " << graph->node_count() << " nodes, "
              << graph->edge_count() << " edges\n\n";

    // Build CH
    std::cout << "Building contraction hierarchy...\n";
    auto ch = build_ch(*graph);
    std::cout << "  Done\n\n";

    // Configure location fragility
    LocationFragilityConfig cfg;
    cfg.center = {35.4312, -83.4496};  // Bryson City, NC
    cfg.radius_meters = 30000;
    cfg.removal_fraction = 0.10f;
    cfg.sample_count = 200;
    cfg.strategy = SelectionStrategy::MONTE_CARLO;
    cfg.monte_carlo_runs = 20;
    cfg.seed = 42;

    std::cout << "Computing location fragility...\n";
    auto result = location_fragility(*graph, ch, cfg);

    std::cout << "\n=== RESULTS ===\n";
    std::cout << "Isolation risk:        " << result.isolation_risk << "\n";
    std::cout << "AUC normalized:        " << result.auc_normalized << "\n";
    std::cout << "Reachable nodes:       " << result.reachable_nodes << "\n";
    std::cout << "SP edges total:        " << result.sp_edges_total << "\n";
    std::cout << "SP edges removed:      " << result.sp_edges_removed << "\n";
    std::cout << "Simplified subgraph:   " << result.subgraph_nodes << " nodes\n";
    std::cout << "Directional coverage:  " << result.directional_coverage << "\n";
    std::cout << "Directional asymmetry: " << result.directional_asymmetry << "\n";

    return 0;
}
