#include <catch2/catch_test_macros.hpp>
#include "gravel/geo/osm_graph.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/fragility/route_fragility.h"
#include "gravel/simplify/bridges.h"
#include "gravel/analysis/betweenness.h"
#include "gravel/analysis/algebraic_connectivity.h"
#include "gravel/analysis/kirchhoff.h"
#include "gravel/analysis/accessibility.h"
#include "gravel/analysis/county_fragility.h"
#include "gravel/analysis/analysis_context.h"
#include "gravel/core/subgraph.h"
#include "gravel/simplify/simplify.h"
#include <chrono>
#include <iostream>
#include <filesystem>
#include <random>

TEST_CASE("Swain county timing with AnalysisContext + simplification", "[.timing]") {
    using namespace gravel;
    using Clock = std::chrono::steady_clock;

    std::string pbf = std::string(TEST_DATA_DIR) + "/swain_county.osm.pbf";
    if (!std::filesystem::exists(pbf)) SKIP("No Swain County data");

    OSMConfig osm_cfg;
    osm_cfg.pbf_path = pbf;
    osm_cfg.speed_profile = SpeedProfile::car();
    auto graph = load_osm_graph(osm_cfg);
    std::cerr << "Graph: " << graph->node_count() << " nodes, " << graph->edge_count() << " edges\n";

    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    Polygon boundary;
    boundary.vertices = {
        {35.25, -83.95}, {35.25, -83.20},
        {35.55, -83.20}, {35.55, -83.95},
        {35.25, -83.95}
    };

    // Build analysis context WITH simplification (the optimization under test)
    auto t_ctx = Clock::now();
    AnalysisContextConfig ctx_cfg;
    ctx_cfg.boundary = boundary;
    ctx_cfg.simplify = true;  // degree-2 contraction
    auto ctx = build_analysis_context(*graph, ch, idx, ctx_cfg);
    auto t_ctx_done = Clock::now();

    std::cerr << "\n--- AnalysisContext Build ---\n";
    std::cerr << "Original subgraph: " << ctx.stats.original_subgraph_nodes << " nodes, "
              << ctx.stats.original_subgraph_edges << " edges\n";
    std::cerr << "After simplification: " << ctx.stats.analysis_nodes << " nodes, "
              << ctx.stats.analysis_edges << " edges\n";
    std::cerr << "Reduction: " << (1.0 - ctx.stats.simplification_ratio) * 100 << "%\n";
    std::cerr << "Bridges: " << ctx.bridges.bridges.size() << "\n";
    std::cerr << "Entry points: " << ctx.entry_points.size() << "\n";
    std::cerr << "Context build time: "
              << std::chrono::duration<double>(t_ctx_done - t_ctx).count() << "s\n";
    std::cerr << "  Extract: " << ctx.stats.subgraph_extract_seconds << "s\n";
    std::cerr << "  Simplify: " << ctx.stats.simplification_seconds << "s\n";
    std::cerr << "  Bridges: " << ctx.stats.bridge_detection_seconds << "s\n";
    std::cerr << "  Entry pts: " << ctx.stats.entry_point_seconds << "s\n";

    // Now run county_fragility_index WITH context (skip spectral for speed)
    CountyFragilityConfig cfg;
    cfg.boundary = boundary;
    cfg.od_sample_count = 20;  // reduced for timing
    cfg.skip_spectral = true;
    cfg.seed = 42;

    auto t_frag = Clock::now();
    auto result = county_fragility_index(*graph, ch, idx, cfg, &ctx);
    auto t_frag_done = Clock::now();

    double frag_sec = std::chrono::duration<double>(t_frag_done - t_frag).count();
    std::cerr << "\n--- County Fragility (with context, skip_spectral, 20 OD) ---\n";
    std::cerr << "Time: " << frag_sec << "s\n";
    std::cerr << "Composite: " << result.composite_index << "\n";
    std::cerr << "Subgraph: " << result.subgraph_nodes << " nodes\n";

    // Also time a full county fragility call WITHOUT context for comparison
    CountyFragilityConfig cfg_no_ctx;
    cfg_no_ctx.boundary = boundary;
    cfg_no_ctx.od_sample_count = 20;
    cfg_no_ctx.skip_spectral = true;
    cfg_no_ctx.seed = 42;

    auto t_no_ctx = Clock::now();
    auto result_no_ctx = county_fragility_index(*graph, ch, idx, cfg_no_ctx);
    auto t_no_ctx_done = Clock::now();

    double no_ctx_sec = std::chrono::duration<double>(t_no_ctx_done - t_no_ctx).count();
    std::cerr << "\n--- County Fragility (NO context, skip_spectral, 20 OD) ---\n";
    std::cerr << "Time: " << no_ctx_sec << "s\n";
    std::cerr << "Composite: " << result_no_ctx.composite_index << "\n";
    std::cerr << "Subgraph: " << result_no_ctx.subgraph_nodes << " nodes\n";

    // Progressive estimate
    std::cerr << "\n--- Progressive Estimates (k=20, runs=30) ---\n";
    std::cerr << "With context:    " << frag_sec * 600 / 60 << " min (" << frag_sec * 600 << "s)\n";
    std::cerr << "Without context: " << no_ctx_sec * 600 / 60 << " min (" << no_ctx_sec * 600 << "s)\n";
}
