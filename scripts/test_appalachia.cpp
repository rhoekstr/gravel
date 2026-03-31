/// Appalachia simplification test.
/// Loads a multi-state OSM extract, builds CH, runs simplification pipeline,
/// and reports size reduction + degradation metrics.
///
/// Build: g++ -std=c++20 -O2 -I include -I build/_deps/*/include \
///        -DGRAVEL_HAS_OSMIUM scripts/test_appalachia.cpp \
///        -L build -lgravel -lz -llz4 -o build/test_appalachia
/// Run:   build/test_appalachia <path.osm.pbf>

#include "gravel/geo/osm_graph.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/ch_query.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/simplify/simplify.h"
#include "gravel/core/dijkstra.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <random>

using namespace gravel;
using clk = std::chrono::steady_clock;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <appalachia.osm.pbf>\n";
        return 1;
    }

    auto t0 = clk::now();

    // Load
    std::cerr << "Loading " << argv[1] << "...\n";
    OSMConfig cfg;
    cfg.pbf_path = argv[1];
    cfg.speed_profile = SpeedProfile::car();
    cfg.bidirectional = true;
    auto graph = load_osm_graph(cfg);
    auto t1 = clk::now();
    std::cerr << "  Loaded: " << graph->node_count() << " nodes, "
              << graph->edge_count() << " edges ("
              << std::chrono::duration<double>(t1 - t0).count() << "s)\n";

    // Build CH
    std::cerr << "Building CH...\n";
    auto ch = build_ch(*graph, {}, [](int pct) {
        if (pct % 10 == 0) std::cerr << "  " << pct << "%\n";
    });
    auto t2 = clk::now();
    std::cerr << "  CH built (" << std::chrono::duration<double>(t2 - t1).count() << "s)\n";
    std::cerr << "  Up edges: " << ch.up_targets.size()
              << ", Down edges: " << ch.down_targets.size() << "\n";

    // Validate CH on 20 random pairs
    std::cerr << "Validating CH on 20 pairs...\n";
    {
        CHQuery q(ch);
        std::mt19937_64 rng(42);
        std::uniform_int_distribution<NodeID> nd(0, graph->node_count() - 1);
        int match = 0, tested = 0;
        for (int i = 0; i < 100 && tested < 20; ++i) {
            NodeID s = nd(rng), t = nd(rng);
            if (s == t) continue;
            auto d_ch = q.distance(s, t);
            auto dijk = dijkstra(*graph, s);
            auto d_ref = dijk.distances[t];
            if (d_ch >= INF_WEIGHT && d_ref >= INF_WEIGHT) continue;
            tested++;
            if (std::abs(d_ch - d_ref) < 1e-3) match++;
        }
        std::cerr << "  CH validation: " << match << "/" << tested << " exact\n";
    }

    ShortcutIndex idx(ch);

    // Simplify: degree-2 only
    std::cerr << "\n=== Degree-2 Contraction Only ===\n";
    {
        SimplificationConfig scfg;
        scfg.contract_degree2 = true;
        scfg.ch_level_keep_fraction = 1.0;
        scfg.estimate_degradation = true;
        scfg.degradation_samples = 200;

        auto t3 = clk::now();
        auto result = simplify_graph(*graph, &ch, &idx, scfg);
        auto t4 = clk::now();

        std::cerr << "  Nodes: " << result.original_nodes << " -> "
                  << result.simplified_nodes << " ("
                  << std::fixed << std::setprecision(1)
                  << (100.0 * (1.0 - (double)result.simplified_nodes / result.original_nodes))
                  << "% reduction)\n";
        std::cerr << "  Edges: " << result.original_edges << " -> "
                  << result.simplified_edges << " ("
                  << (100.0 * (1.0 - (double)result.simplified_edges / result.original_edges))
                  << "% reduction)\n";
        std::cerr << "  Time: " << std::chrono::duration<double>(t4 - t3).count() << "s\n";

        auto& d = result.degradation;
        std::cerr << "  Degradation (" << d.od_pairs_sampled << " pairs):\n"
                  << "    Median stretch: " << std::setprecision(4) << d.median_stretch << "\n"
                  << "    P90 stretch:    " << d.p90_stretch << "\n"
                  << "    P95 stretch:    " << d.p95_stretch << "\n"
                  << "    Max stretch:    " << d.max_stretch << "\n"
                  << "    Connectivity:   " << d.pairs_connected_after << "/"
                  << d.pairs_connected_before << " (" << std::setprecision(1)
                  << (d.connectivity_ratio * 100) << "%)\n"
                  << "    Bridges: " << d.preserved_bridges << "/" << d.original_bridges
                  << (d.all_bridges_preserved ? " (all preserved)" : " (SOME LOST)") << "\n";
    }

    // Simplify: degree-2 + CH-level 70%
    std::cerr << "\n=== Degree-2 + CH Top 70% ===\n";
    {
        SimplificationConfig scfg;
        scfg.contract_degree2 = true;
        scfg.ch_level_keep_fraction = 0.7;
        scfg.estimate_degradation = true;
        scfg.degradation_samples = 200;

        auto t3 = clk::now();
        auto result = simplify_graph(*graph, &ch, &idx, scfg);
        auto t4 = clk::now();

        std::cerr << "  Nodes: " << result.original_nodes << " -> "
                  << result.simplified_nodes << " ("
                  << std::fixed << std::setprecision(1)
                  << (100.0 * (1.0 - (double)result.simplified_nodes / result.original_nodes))
                  << "% reduction)\n";
        std::cerr << "  Edges: " << result.original_edges << " -> "
                  << result.simplified_edges << " ("
                  << (100.0 * (1.0 - (double)result.simplified_edges / result.original_edges))
                  << "% reduction)\n";
        std::cerr << "  Time: " << std::chrono::duration<double>(t4 - t3).count() << "s\n";

        auto& d = result.degradation;
        std::cerr << "  Degradation (" << d.od_pairs_sampled << " pairs):\n"
                  << "    Median stretch: " << std::setprecision(4) << d.median_stretch << "\n"
                  << "    P90 stretch:    " << d.p90_stretch << "\n"
                  << "    P95 stretch:    " << d.p95_stretch << "\n"
                  << "    Max stretch:    " << d.max_stretch << "\n"
                  << "    Connectivity:   " << d.pairs_connected_after << "/"
                  << d.pairs_connected_before << " (" << std::setprecision(1)
                  << (d.connectivity_ratio * 100) << "%)\n"
                  << "    Bridges: " << d.preserved_bridges << "/" << d.original_bridges
                  << (d.all_bridges_preserved ? " (all preserved)" : " (SOME LOST)") << "\n";

        for (const auto& s : d.stages) {
            std::cerr << "    Stage '" << s.stage_name << "': "
                      << s.nodes_before << " -> " << s.nodes_after << " nodes, "
                      << s.edges_before << " -> " << s.edges_after << " edges\n";
        }
    }

    auto tend = clk::now();
    std::cerr << "\nTotal time: " << std::chrono::duration<double>(tend - t0).count() << "s\n";

    return 0;
}
