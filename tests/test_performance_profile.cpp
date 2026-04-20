#include <catch2/catch_test_macros.hpp>
#include "gravel/geo/osm_graph.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/ch_query.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/ch/blocked_ch_query.h"
#include "gravel/fragility/route_fragility.h"
#include "gravel/simplify/bridges.h"
#include "gravel/analysis/betweenness.h"
#include "gravel/analysis/algebraic_connectivity.h"
#include "gravel/analysis/kirchhoff.h"
#include "gravel/analysis/accessibility.h"
#include "gravel/analysis/county_fragility.h"
#include "gravel/analysis/location_fragility.h"
#include "gravel/analysis/analysis_context.h"
#include "gravel/core/subgraph.h"
#include "gravel/simplify/simplify.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <filesystem>
#include <random>
#include <utility>
#include <vector>
#include <string>

using namespace gravel;
using Clock = std::chrono::steady_clock;

namespace {

struct Timer {
    Clock::time_point start = Clock::now();
    double lap() {
        auto now = Clock::now();
        double dt = std::chrono::duration<double>(now - start).count();
        start = now;
        return dt;
    }
};

bool has_swain_data() {
    return std::filesystem::exists(std::string(TEST_DATA_DIR) + "/swain_county.osm.pbf");
}

Polygon swain_boundary() {
    Polygon p;
    p.vertices = {
        {35.25, -83.95}, {35.25, -83.20},
        {35.55, -83.20}, {35.55, -83.95},
        {35.25, -83.95}
    };
    return p;
}

}  // namespace

TEST_CASE("Performance profile: stage-by-stage Swain County analysis", "[.perf]") {
    if (!has_swain_data()) SKIP("No Swain County data");

    std::cerr << "\n========== PERFORMANCE PROFILE: Swain County ==========\n\n";
    std::cerr << std::fixed << std::setprecision(3);

    Timer t;

    // Stage 1: OSM loading
    OSMConfig osm_cfg;
    osm_cfg.pbf_path = std::string(TEST_DATA_DIR) + "/swain_county.osm.pbf";
    osm_cfg.speed_profile = SpeedProfile::car();
    auto graph = load_osm_graph(osm_cfg);
    double t_load = t.lap();
    std::cerr << "[LOAD]     " << t_load << "s - "
              << graph->node_count() << " nodes, " << graph->edge_count() << " edges\n";

    // Stage 2: CH build
    auto ch = build_ch(*graph);
    double t_ch = t.lap();
    std::cerr << "[CH]       " << t_ch << "s - " << ch.up_targets.size() << " up edges\n";

    ShortcutIndex idx(ch);
    double t_idx = t.lap();
    std::cerr << "[IDX]      " << t_idx << "s\n";

    auto boundary = swain_boundary();

    // Stage 3: AnalysisContext (subgraph + simplify + bridges + entry points)
    AnalysisContextConfig ctx_cfg;
    ctx_cfg.boundary = boundary;
    ctx_cfg.simplify = true;
    auto ctx = build_analysis_context(*graph, ch, idx, ctx_cfg);
    double t_ctx = t.lap();
    std::cerr << "[CONTEXT]  " << t_ctx << "s - "
              << ctx.stats.original_subgraph_nodes << " → "
              << ctx.stats.analysis_nodes << " nodes ("
              << (100.0 * (1.0 - ctx.stats.simplification_ratio)) << "% reduction)\n";
    std::cerr << "           extract=" << ctx.stats.subgraph_extract_seconds
              << "s simplify=" << ctx.stats.simplification_seconds
              << "s bridges=" << ctx.stats.bridge_detection_seconds
              << "s entry=" << ctx.stats.entry_point_seconds << "s\n";

    const auto& sg = *ctx.analysis_graph;
    std::cerr << "           analysis graph: " << sg.node_count() << " nodes, "
              << sg.edge_count() << " edges, " << ctx.bridges.bridges.size() << " bridges\n";

    // Stage 4: Betweenness (sampled) on simplified graph
    BetweennessConfig bc_100;
    bc_100.sample_sources = 100;
    bc_100.seed = 42;
    auto bet_100 = edge_betweenness(sg, bc_100);
    double t_bet100 = t.lap();
    std::cerr << "[BETW-100] " << t_bet100 << "s - 100-source approx on "
              << sg.node_count() << " nodes\n";

    BetweennessConfig bc_500;
    bc_500.sample_sources = 500;
    bc_500.seed = 42;
    auto bet_500 = edge_betweenness(sg, bc_500);
    double t_bet500 = t.lap();
    std::cerr << "[BETW-500] " << t_bet500 << "s - 500-source approx\n";

    // Stage 5: Spectral metrics on simplified graph
    double ac = algebraic_connectivity(sg);
    double t_ac = t.lap();
    std::cerr << "[ALG-CONN] " << t_ac << "s - λ₂ = " << ac << "\n";

    KirchhoffConfig kc;
    kc.seed = 42;
    double ki = kirchhoff_index(sg, kc);
    double t_ki = t.lap();
    std::cerr << "[KIRCHHOFF] " << t_ki << "s - Kf = " << ki << "\n";

    // Stage 6: Route fragility on FULL graph
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<NodeID> dist(0, graph->node_count() - 1);
    NodeID s = dist(rng), tar = dist(rng);
    while (tar == s) tar = dist(rng);

    auto fr = route_fragility(ch, idx, *graph, s, tar);
    double t_rf1 = t.lap();
    std::cerr << "[ROUTE-FRAG-1]  " << t_rf1 << "s - single pair ("
              << fr.edge_fragilities.size() << " edges analyzed)\n";

    // Stage 7: Batch route fragility (20 pairs)
    std::vector<std::pair<NodeID, NodeID>> od_pairs;
    for (int i = 0; i < 20; ++i) {
        NodeID a = dist(rng), b = dist(rng);
        while (b == a) b = dist(rng);
        od_pairs.push_back({a, b});
    }
    auto batch = batch_fragility(ch, idx, *graph, od_pairs);
    double t_rf20 = t.lap();
    std::cerr << "[ROUTE-FRAG-20] " << t_rf20 << "s - 20 pairs ("
              << (t_rf20 / 20) << "s per pair avg)\n";

    // Stage 8: BlockedCHQuery
    std::vector<std::pair<NodeID, NodeID>> blocked_5;
    for (int i = 0; i < 5; ++i) {
        NodeID a = dist(rng), b = dist(rng);
        blocked_5.push_back({a, b});
    }
    BlockedCHQuery bcq(ch, idx, *graph);
    bcq.distance_blocking(s, tar, blocked_5);
    double t_bcq1 = t.lap();
    std::cerr << "[BLOCKED-CH-1]  " << t_bcq1 << "s - 1 query, 5 blocked\n";

    for (const auto& [a, b] : od_pairs) {
        bcq.distance_blocking(a, b, blocked_5);
    }
    double t_bcq20 = t.lap();
    std::cerr << "[BLOCKED-CH-20] " << t_bcq20 << "s - 20 queries, 5 blocked ("
              << (t_bcq20 / 20) << "s per query)\n";

    // Stage 9: county_fragility_index variants
    CountyFragilityConfig cfg;
    cfg.boundary = boundary;
    cfg.od_sample_count = 20;
    cfg.skip_spectral = true;
    cfg.seed = 42;
    auto cfr = county_fragility_index(*graph, ch, idx, cfg, &ctx);
    double t_cf_fast = t.lap();
    std::cerr << "[COUNTY-FAST]   " << t_cf_fast << "s - 20 OD, skip_spectral, with context\n";

    CountyFragilityConfig cfg_full;
    cfg_full.boundary = boundary;
    cfg_full.od_sample_count = 20;
    cfg_full.seed = 42;
    auto cfr_full = county_fragility_index(*graph, ch, idx, cfg_full, &ctx);
    double t_cf_full = t.lap();
    std::cerr << "[COUNTY-FULL]   " << t_cf_full << "s - 20 OD, full spectral, with context\n";

    CountyFragilityConfig cfg_noctx;
    cfg_noctx.boundary = boundary;
    cfg_noctx.od_sample_count = 20;
    cfg_noctx.skip_spectral = true;
    cfg_noctx.seed = 42;
    auto cfr_noctx = county_fragility_index(*graph, ch, idx, cfg_noctx);
    double t_cf_noctx = t.lap();
    std::cerr << "[COUNTY-NOCTX]  " << t_cf_noctx << "s - 20 OD, skip_spectral, NO context\n";

    // Stage 10: Location fragility (new Dijkstra+IncrementalSSSP)
    LocationFragilityConfig loc_cfg;
    loc_cfg.center = {35.4312, -83.4496};
    loc_cfg.radius_meters = 16000;
    loc_cfg.monte_carlo_runs = 10;
    loc_cfg.seed = 42;
    auto loc = location_fragility(*graph, ch, loc_cfg);
    double t_loc = t.lap();
    std::cerr << "[LOC-FRAG-MC10] " << t_loc << "s - 10 MC trials, "
              << loc.reachable_nodes << " reachable, "
              << loc.sp_edges_removed << "/" << loc.sp_edges_total << " SP edges removed\n";

    // Print bottleneck analysis
    std::cerr << "\n========== BOTTLENECK ANALYSIS ==========\n";
    std::cerr << "Fast stages (<0.1s):\n";
    if (t_ctx < 0.1)    std::cerr << "  Context build:  " << t_ctx << "s\n";
    if (t_bcq1 < 0.1)   std::cerr << "  Blocked CH x1:  " << t_bcq1 << "s\n";

    std::cerr << "\nModerate stages (0.1s - 1s):\n";
    auto maybe = [](const char* name, double t) {
        if (t >= 0.1 && t < 1.0) std::cerr << "  " << name << ": " << t << "s\n";
    };
    maybe("Load", t_load);
    maybe("CH build", t_ch);
    maybe("ShortcutIndex", t_idx);
    maybe("Context", t_ctx);
    maybe("Betw-100", t_bet100);
    maybe("Alg conn", t_ac);
    maybe("Kirchhoff", t_ki);

    std::cerr << "\nSlow stages (>1s) - optimization targets:\n";
    auto slow = [](const char* name, double t) {
        if (t >= 1.0) std::cerr << "  *** " << name << ": " << t << "s\n";
    };
    slow("Load", t_load);
    slow("CH build", t_ch);
    slow("ShortcutIndex", t_idx);
    slow("Betw-100", t_bet100);
    slow("Betw-500", t_bet500);
    slow("Alg conn", t_ac);
    slow("Kirchhoff", t_ki);
    slow("Route frag x1", t_rf1);
    slow("Route frag x20", t_rf20);
    slow("Blocked CH x20", t_bcq20);
    slow("County fast", t_cf_fast);
    slow("County full", t_cf_full);
    slow("County noctx", t_cf_noctx);
    slow("Loc frag x5", t_loc);

    // Progressive fragility cost estimate
    std::cerr << "\n========== PROGRESSIVE FRAGILITY ESTIMATES ==========\n";
    double county_per_call = t_cf_fast;
    std::cerr << "county_fragility per call (fast): " << county_per_call << "s\n";
    std::cerr << "  MC k=20, runs=30: " << (county_per_call * 20 * 30) << "s ("
              << (county_per_call * 20 * 30 / 60) << " min)\n";
    std::cerr << "  Greedy k=50: " << (county_per_call * 50) << "s ("
              << (county_per_call * 50 / 60) << " min)\n";
    std::cerr << "=================================\n";

    REQUIRE(graph->node_count() > 100000);
}
