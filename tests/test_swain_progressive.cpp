#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/geo/osm_graph.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/analysis/progressive_fragility.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <chrono>

using namespace gravel;

// Swain County, NC approximate bounding polygon
// County seat: Bryson City (~35.43, -83.45)
// The county spans roughly from 35.25 to 35.55 lat, -83.1 to -83.75 lon
static Polygon swain_county_polygon() {
    Polygon p;
    p.vertices = {
        {35.25, -83.75}, {35.25, -83.10},
        {35.55, -83.10}, {35.55, -83.75},
        {35.25, -83.75}
    };
    return p;
}

TEST_CASE("Swain County progressive fragility — Monte Carlo k=20", "[real_progressive][.]") {
    // Load Swain County OSM data
    std::string pbf_path = std::string(TEST_DATA_DIR) + "/swain_county.osm.pbf";
    OSMConfig osm_cfg;
    osm_cfg.pbf_path = pbf_path;
    osm_cfg.speed_profile = SpeedProfile::car();

    std::cerr << "Loading " << pbf_path << "...\n";
    auto graph = load_osm_graph(osm_cfg);
    REQUIRE(graph);
    std::cerr << "  Nodes: " << graph->node_count()
              << ", Edges: " << graph->edge_count() << "\n";

    // Build CH
    std::cerr << "Building CH...\n";
    CHBuildConfig ch_cfg;
    auto ch = build_ch(*graph, ch_cfg, [](int pct) {
        if (pct % 20 == 0) std::cerr << "  " << pct << "%\n";
    });
    ShortcutIndex idx(ch);
    std::cerr << "  CH built. Up edges: " << ch.up_targets.size() << "\n";

    // Configure progressive fragility
    ProgressiveFragilityConfig prog_cfg;
    prog_cfg.base_config.boundary = swain_county_polygon();
    prog_cfg.base_config.od_sample_count = 50;
    prog_cfg.base_config.betweenness_samples = 100;
    prog_cfg.base_config.seed = 42;

    prog_cfg.selection_strategy = SelectionStrategy::MONTE_CARLO;
    prog_cfg.k_max = 20;
    prog_cfg.monte_carlo_runs = 30;
    prog_cfg.base_seed = 42;

    std::cerr << "\nRunning progressive fragility (MC, k_max=20, runs=30)...\n";

    auto result = progressive_fragility(*graph, ch, idx, prog_cfg);

    // Print the degradation curve
    std::cerr << "\n=== Swain County Degradation Curve ===\n";
    std::cerr << std::fixed << std::setprecision(4);
    std::cerr << "  Subgraph: " << result.subgraph_nodes << " nodes, "
              << result.subgraph_edges << " edges\n";
    std::cerr << "  Strategy: Monte Carlo (" << prog_cfg.monte_carlo_runs << " runs)\n\n";

    std::cerr << std::setw(4) << "k"
              << std::setw(10) << "mean"
              << std::setw(10) << "std"
              << std::setw(10) << "p25"
              << std::setw(10) << "p50"
              << std::setw(10) << "p75"
              << std::setw(10) << "p90"
              << std::setw(10) << "disconn%"
              << "\n";
    std::cerr << std::string(74, '-') << "\n";

    for (const auto& kl : result.curve) {
        std::cerr << std::setw(4) << kl.k
                  << std::setw(10) << kl.mean_composite
                  << std::setw(10) << kl.std_composite
                  << std::setw(10) << kl.p25
                  << std::setw(10) << kl.p50
                  << std::setw(10) << kl.p75
                  << std::setw(10) << kl.p90
                  << std::setw(10) << (kl.fraction_disconnected * 100.0)
                  << "\n";
    }

    std::cerr << "\n=== Summary Metrics ===\n";
    std::cerr << "  AUC raw:        " << result.auc_raw << "\n";
    std::cerr << "  AUC normalized: " << result.auc_normalized << "\n";
    std::cerr << "  AUC excess:     " << result.auc_excess << "\n";
    std::cerr << "  Critical k:     " << result.critical_k << "\n";
    std::cerr << "  Jump detected:  " << (result.jump_detected ? "YES" : "no") << "\n";
    if (result.jump_detected) {
        std::cerr << "  Jump at k:      " << result.jump_at_k << "\n";
        std::cerr << "  Jump magnitude: " << result.jump_magnitude << "\n";
    }

    // Structural assertions
    REQUIRE(result.curve.size() == 21);  // k=0..20
    REQUIRE(result.subgraph_nodes > 0);
    REQUIRE(result.subgraph_edges > 0);

    // Baseline should be a valid fragility score
    REQUIRE(result.curve[0].mean_composite >= 0.0);
    REQUIRE(result.curve[0].mean_composite <= 1.0);

    // Curve should be non-decreasing in mean (on average, removing edges makes things worse)
    // Allow some noise from MC sampling
    double max_decrease = 0.0;
    for (size_t i = 1; i < result.curve.size(); ++i) {
        double diff = result.curve[i].mean_composite - result.curve[i-1].mean_composite;
        if (diff < 0) max_decrease = std::min(max_decrease, diff);
    }
    // Allow up to 0.05 decrease from MC noise
    REQUIRE(max_decrease > -0.05);

    // AUC should be positive
    REQUIRE(result.auc_raw > 0.0);
    REQUIRE(result.auc_normalized > 0.0);

    // At k=20, fragility should be higher than baseline (removing 20 edges hurts)
    REQUIRE(result.curve[20].mean_composite >= result.curve[0].mean_composite - 0.01);

    std::cerr << "\n=== PASS ===\n";
}

TEST_CASE("Swain County progressive fragility — Greedy Betweenness k=50", "[real_progressive][.]") {
    std::string pbf_path = std::string(TEST_DATA_DIR) + "/swain_county.osm.pbf";
    OSMConfig osm_cfg;
    osm_cfg.pbf_path = pbf_path;
    osm_cfg.speed_profile = SpeedProfile::car();

    std::cerr << "Loading " << pbf_path << "...\n";
    auto graph = load_osm_graph(osm_cfg);
    REQUIRE(graph);
    std::cerr << "  Nodes: " << graph->node_count()
              << ", Edges: " << graph->edge_count() << "\n";

    std::cerr << "Building CH...\n";
    CHBuildConfig ch_cfg;
    auto ch = build_ch(*graph, ch_cfg, [](int pct) {
        if (pct % 20 == 0) std::cerr << "  " << pct << "%\n";
    });
    ShortcutIndex idx(ch);
    std::cerr << "  CH built. Up edges: " << ch.up_targets.size() << "\n";

    ProgressiveFragilityConfig prog_cfg;
    prog_cfg.base_config.boundary = swain_county_polygon();
    prog_cfg.base_config.od_sample_count = 50;
    prog_cfg.base_config.betweenness_samples = 200;
    prog_cfg.base_config.seed = 42;

    prog_cfg.selection_strategy = SelectionStrategy::GREEDY_BETWEENNESS;
    prog_cfg.k_max = 50;
    prog_cfg.monte_carlo_runs = 1;   // greedy is deterministic — one run
    prog_cfg.base_seed = 42;

    std::cerr << "\nRunning progressive fragility (GREEDY_BETWEENNESS, k_max=50)...\n";

    auto t0 = std::chrono::high_resolution_clock::now();
    auto result = progressive_fragility(*graph, ch, idx, prog_cfg);
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    std::cerr << "\n=== Swain County Greedy-Betweenness Degradation Curve (k=50) ===\n";
    std::cerr << std::fixed << std::setprecision(4);
    std::cerr << "  Subgraph: " << result.subgraph_nodes << " nodes, "
              << result.subgraph_edges << " edges\n";
    std::cerr << "  Total time: " << elapsed << "s\n\n";

    std::cerr << std::setw(4)  << "k"
              << std::setw(10) << "mean"
              << std::setw(10) << "p50"
              << std::setw(10) << "disconn%"
              << "\n";
    std::cerr << std::string(34, '-') << "\n";

    // Print every 5th level plus k=0 and k=50
    for (const auto& kl : result.curve) {
        if (kl.k == 0 || kl.k % 5 == 0) {
            std::cerr << std::setw(4)  << kl.k
                      << std::setw(10) << kl.mean_composite
                      << std::setw(10) << kl.p50
                      << std::setw(10) << (kl.fraction_disconnected * 100.0)
                      << "\n";
        }
    }

    std::cerr << "\n=== Summary ===\n";
    std::cerr << "  AUC normalized: " << result.auc_normalized << "\n";
    std::cerr << "  AUC excess:     " << result.auc_excess << "\n";
    std::cerr << "  Critical k:     " << result.critical_k << "\n";
    if (result.jump_detected) {
        std::cerr << "  Jump at k=" << result.jump_at_k
                  << "  magnitude=" << result.jump_magnitude << "\n";
    }

    if (!result.removal_sequence.empty()) {
        std::cerr << "\n  First 10 removed edges (original node pairs):\n";
        for (size_t i = 0; i < std::min<size_t>(10, result.removal_sequence.size()); ++i) {
            std::cerr << "    [" << i+1 << "] (" << result.removal_sequence[i].first
                      << ", " << result.removal_sequence[i].second << ")\n";
        }
    }

    REQUIRE(result.curve.size() == 51);
    REQUIRE(result.subgraph_nodes > 0);
    REQUIRE(result.auc_raw > 0.0);

    std::cerr << "\n=== PASS ===\n";
}

TEST_CASE("Swain County progressive fragility — Greedy Betweenness k_max_fraction=0.001", "[real_progressive][.]") {
    std::string pbf_path = std::string(TEST_DATA_DIR) + "/swain_county.osm.pbf";
    OSMConfig osm_cfg;
    osm_cfg.pbf_path = pbf_path;
    osm_cfg.speed_profile = SpeedProfile::car();

    std::cerr << "Loading " << pbf_path << "...\n";
    auto graph = load_osm_graph(osm_cfg);
    REQUIRE(graph);
    std::cerr << "  Nodes: " << graph->node_count()
              << ", Edges: " << graph->edge_count() << "\n";

    std::cerr << "Building CH...\n";
    CHBuildConfig ch_cfg;
    auto ch = build_ch(*graph, ch_cfg, [](int pct) {
        if (pct % 20 == 0) std::cerr << "  " << pct << "%\n";
    });
    ShortcutIndex idx(ch);
    std::cerr << "  CH built.\n";

    ProgressiveFragilityConfig prog_cfg;
    prog_cfg.base_config.boundary    = swain_county_polygon();
    prog_cfg.base_config.od_sample_count  = 50;
    prog_cfg.base_config.betweenness_samples = 200;
    prog_cfg.base_config.seed        = 42;

    prog_cfg.selection_strategy = SelectionStrategy::GREEDY_BETWEENNESS;
    prog_cfg.k_max_fraction     = 0.001f;   // ~0.1% of subgraph edges
    prog_cfg.base_seed          = 42;

    std::cerr << "\nRunning progressive fragility (GREEDY_BETWEENNESS, k_max_fraction=0.001)...\n";

    auto t0 = std::chrono::high_resolution_clock::now();
    auto result = progressive_fragility(*graph, ch, idx, prog_cfg);
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed = std::chrono::duration<double>(t1 - t0).count();

    uint32_t k_max = result.k_max_used;
    std::cerr << "\n=== Swain County Greedy-Betweenness (k_max_fraction=0.001) ===\n";
    std::cerr << std::fixed << std::setprecision(4);
    std::cerr << "  Subgraph: " << result.subgraph_nodes << " nodes, "
              << result.subgraph_edges << " edges\n";
    std::cerr << "  k_max resolved to: " << k_max << " edges ("
              << (100.0 * k_max / result.subgraph_edges) << "% of subgraph)\n";
    std::cerr << "  Total time: " << elapsed << "s\n\n";

    std::cerr << std::setw(6)  << "k"
              << std::setw(8)  << "(%)"
              << std::setw(10) << "composite"
              << "\n";
    std::cerr << std::string(24, '-') << "\n";

    // Print ~11 evenly spaced points across the curve
    uint32_t step = std::max(1u, k_max / 10);
    for (const auto& kl : result.curve) {
        if (kl.k == 0 || kl.k % step == 0 || kl.k == k_max) {
            double pct = result.subgraph_edges > 0
                         ? 100.0 * kl.k / result.subgraph_edges : 0.0;
            std::cerr << std::setw(6)  << kl.k
                      << std::setw(7)  << std::setprecision(3) << pct << "%"
                      << std::setw(10) << std::setprecision(4) << kl.mean_composite
                      << "\n";
        }
    }

    std::cerr << "\n=== Summary ===\n";
    std::cerr << "  AUC normalized: " << result.auc_normalized << "\n";
    std::cerr << "  AUC excess:     " << result.auc_excess     << "\n";
    std::cerr << "  Critical k:     " << result.critical_k     << "\n";
    if (result.jump_detected)
        std::cerr << "  Jump at k=" << result.jump_at_k
                  << "  magnitude=" << result.jump_magnitude    << "\n";

    if (!result.removal_sequence.empty()) {
        std::cerr << "\n  First 10 removed edges:\n";
        for (size_t i = 0; i < std::min<size_t>(10, result.removal_sequence.size()); ++i)
            std::cerr << "    [" << (i+1) << "] ("
                      << result.removal_sequence[i].first << ", "
                      << result.removal_sequence[i].second << ")\n";
    }

    REQUIRE(result.k_max_used > 0);
    REQUIRE(result.subgraph_nodes > 0);
    REQUIRE(result.curve.size() == static_cast<size_t>(k_max + 1));
    REQUIRE(result.auc_raw >= 0.0);

    std::cerr << "\n=== PASS ===\n";
}
