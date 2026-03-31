#include <benchmark/benchmark.h>
#include "gravel/core/array_graph.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/ch_query.h"
#include "gravel/core/dijkstra.h"
#include "gravel/validation/synthetic_graphs.h"
#include "gravel/simplify/bridges.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/fragility/route_fragility.h"
#include "gravel/ch/blocked_ch_query.h"
#include "gravel/fragility/hershberger_suri.h"
#include "gravel/fragility/bernstein_approx.h"
#include "gravel/fragility/via_path.h"
#include "gravel/validation/fragility_validator.h"
#include "gravel/analysis/algebraic_connectivity.h"
#include "gravel/analysis/betweenness.h"
#include "gravel/ch/landmarks.h"
#include "gravel/fragility/fragility_filter.h"
#include <random>
#include <memory>

using namespace gravel;

// ---------------------------------------------------------------------------
// Fixtures: grid graphs contract efficiently (like road networks).
// Random graphs have pathological CH behavior and are not representative.
//
// Scale tiers (proxy graphs — CH construction is not yet parallelized):
//   County:  10K nodes  → 100x100 grid (CH build ~1.5s)
//   State:   50K nodes  → 224x224 grid (CH build ~19s)
// Full-scale benchmarks (S2-1.1 targets) require parallel CH (v0.4).
// ---------------------------------------------------------------------------

namespace {

struct ScaleFixture {
    std::shared_ptr<ArrayGraph> graph;
    ContractionResult ch;
    ShortcutIndex* idx = nullptr;
    std::vector<std::pair<NodeID, NodeID>> pairs;

    ~ScaleFixture() { delete idx; }
};

ScaleFixture& county_fixture() {
    static ScaleFixture f;
    static bool init = false;
    if (!init) {
        f.graph = make_grid_graph(100, 100);  // 10,000 nodes
        f.ch = build_ch(*f.graph);
        f.idx = new ShortcutIndex(f.ch);

        std::mt19937 rng(123);
        std::uniform_int_distribution<NodeID> dist(0, f.graph->node_count() - 1);
        f.pairs.resize(1000);
        for (auto& [s, t] : f.pairs) { s = dist(rng); t = dist(rng); }
        init = true;
    }
    return f;
}

ScaleFixture& state_fixture() {
    static ScaleFixture f;
    static bool init = false;
    if (!init) {
        f.graph = make_grid_graph(224, 224);  // 50,176 nodes
        f.ch = build_ch(*f.graph);
        f.idx = new ShortcutIndex(f.ch);

        std::mt19937 rng(456);
        std::uniform_int_distribution<NodeID> dist(0, f.graph->node_count() - 1);
        f.pairs.resize(1000);
        for (auto& [s, t] : f.pairs) { s = dist(rng); t = dist(rng); }
        init = true;
    }
    return f;
}

// Tree-with-bridges for validation (has bridges, contracts fast unlike random graphs)
ScaleFixture& validation_fixture() {
    static ScaleFixture f;
    static bool init = false;
    if (!init) {
        f.graph = make_tree_with_bridges(500, 200, 42);
        f.ch = build_ch(*f.graph);
        f.idx = new ShortcutIndex(f.ch);

        std::mt19937 rng(789);
        std::uniform_int_distribution<NodeID> dist(0, f.graph->node_count() - 1);
        f.pairs.resize(1000);
        for (auto& [s, t] : f.pairs) { s = dist(rng); t = dist(rng); }
        init = true;
    }
    return f;
}

}  // namespace

// ===========================================================================
// S2-10.2 Benchmark 1: Single CH route query
// Target: < 1 ms p99 at state scale
// ===========================================================================

static void BM_CHQuery_County(benchmark::State& state) {
    auto& f = county_fixture();
    CHQuery query(f.ch);
    size_t i = 0;
    for (auto _ : state) {
        auto [s, t] = f.pairs[i % f.pairs.size()];
        benchmark::DoNotOptimize(query.distance(s, t));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CHQuery_County)->Unit(benchmark::kMicrosecond);

static void BM_CHQuery_State(benchmark::State& state) {
    auto& f = state_fixture();
    CHQuery query(f.ch);
    size_t i = 0;
    for (auto _ : state) {
        auto [s, t] = f.pairs[i % f.pairs.size()];
        benchmark::DoNotOptimize(query.distance(s, t));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CHQuery_State)->Unit(benchmark::kMicrosecond);

static void BM_CHRoute_State(benchmark::State& state) {
    auto& f = state_fixture();
    CHQuery query(f.ch);
    size_t i = 0;
    for (auto _ : state) {
        auto [s, t] = f.pairs[i % f.pairs.size()];
        benchmark::DoNotOptimize(query.route(s, t));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CHRoute_State)->Unit(benchmark::kMicrosecond);

// ===========================================================================
// S2-10.2 Benchmark 2: Route fragility (county)
// Target: < 50 ms p99 per pair
// ===========================================================================

static void BM_RouteFragility_County(benchmark::State& state) {
    auto& f = county_fixture();
    size_t i = 0;
    for (auto _ : state) {
        auto [s, t] = f.pairs[i % f.pairs.size()];
        benchmark::DoNotOptimize(route_fragility(f.ch, *f.idx, *f.graph, s, t));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RouteFragility_County)->Unit(benchmark::kMillisecond)->Iterations(50);

// ===========================================================================
// S2-10.2 Benchmark 3: Route fragility (state)
// Target: < 200 ms p99 per pair
// ===========================================================================

static void BM_RouteFragility_State(benchmark::State& state) {
    auto& f = state_fixture();
    size_t i = 0;
    for (auto _ : state) {
        auto [s, t] = f.pairs[i % f.pairs.size()];
        benchmark::DoNotOptimize(route_fragility(f.ch, *f.idx, *f.graph, s, t));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RouteFragility_State)->Unit(benchmark::kMillisecond)->Iterations(20);

// ===========================================================================
// S2-10.2 Benchmark 4: Distance matrix
// Target: < 60s for 1000x1000 at state
// ===========================================================================

static void BM_DistanceMatrix_County_100x100(benchmark::State& state) {
    auto& f = county_fixture();
    CHQuery query(f.ch);
    std::vector<NodeID> origins(100), destinations(100);
    for (size_t i = 0; i < 100; ++i) {
        origins[i] = f.pairs[i].first;
        destinations[i] = f.pairs[i].second;
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(query.distance_matrix(origins, destinations));
    }
    state.SetItemsProcessed(state.iterations() * 10000);
}
BENCHMARK(BM_DistanceMatrix_County_100x100)->Unit(benchmark::kMillisecond);

static void BM_DistanceMatrix_State_100x100(benchmark::State& state) {
    auto& f = state_fixture();
    CHQuery query(f.ch);
    std::vector<NodeID> origins(100), destinations(100);
    for (size_t i = 0; i < 100; ++i) {
        origins[i] = f.pairs[i].first;
        destinations[i] = f.pairs[i].second;
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(query.distance_matrix(origins, destinations));
    }
    state.SetItemsProcessed(state.iterations() * 10000);
}
BENCHMARK(BM_DistanceMatrix_State_100x100)->Unit(benchmark::kMillisecond);

// ===========================================================================
// S2-10.2 Benchmark 5: batch_fragility
// Target: < 10 minutes for 10K pairs at state
// ===========================================================================

static void BM_BatchFragility_County_50(benchmark::State& state) {
    auto& f = county_fixture();
    std::vector<std::pair<NodeID, NodeID>> pairs(f.pairs.begin(), f.pairs.begin() + 50);
    for (auto _ : state) {
        benchmark::DoNotOptimize(batch_fragility(f.ch, *f.idx, *f.graph, pairs));
    }
    state.SetItemsProcessed(state.iterations() * 50);
}
BENCHMARK(BM_BatchFragility_County_50)->Unit(benchmark::kMillisecond)->Iterations(3);

static void BM_BatchFragility_State_20(benchmark::State& state) {
    auto& f = state_fixture();
    std::vector<std::pair<NodeID, NodeID>> pairs(f.pairs.begin(), f.pairs.begin() + 20);
    for (auto _ : state) {
        benchmark::DoNotOptimize(batch_fragility(f.ch, *f.idx, *f.graph, pairs));
    }
    state.SetItemsProcessed(state.iterations() * 20);
}
BENCHMARK(BM_BatchFragility_State_20)->Unit(benchmark::kMillisecond)->Iterations(3);

// ===========================================================================
// S2-10.2 Benchmark 6: Bridge detection
// Target: < 10 seconds at state
// ===========================================================================

static void BM_BridgeDetection_County(benchmark::State& state) {
    auto& f = county_fixture();
    for (auto _ : state) {
        benchmark::DoNotOptimize(find_bridges(*f.graph));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BridgeDetection_County)->Unit(benchmark::kMillisecond);

static void BM_BridgeDetection_State(benchmark::State& state) {
    auto& f = state_fixture();
    for (auto _ : state) {
        benchmark::DoNotOptimize(find_bridges(*f.graph));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BridgeDetection_State)->Unit(benchmark::kMillisecond);

// ===========================================================================
// Hershberger-Suri (county-scale exact replacement paths)
// S2-2.1.1: < 1 ms per O-D pair at county
// ===========================================================================

static void BM_HershbergerSuri_County(benchmark::State& state) {
    auto& f = county_fixture();
    size_t i = 0;
    for (auto _ : state) {
        auto [s, t] = f.pairs[i % f.pairs.size()];
        benchmark::DoNotOptimize(hershberger_suri(*f.graph, s, t));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HershbergerSuri_County)->Unit(benchmark::kMillisecond)->Iterations(50);

// ===========================================================================
// Bernstein approximate replacement paths
// S2-2.2: should be cheaper than exact for batch
// ===========================================================================

static void BM_BernsteinApprox_County(benchmark::State& state) {
    auto& f = county_fixture();
    BernsteinConfig cfg;
    cfg.epsilon = 0.1;
    size_t i = 0;
    for (auto _ : state) {
        auto [s, t] = f.pairs[i % f.pairs.size()];
        benchmark::DoNotOptimize(bernstein_approx(*f.graph, s, t, cfg));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BernsteinApprox_County)->Unit(benchmark::kMillisecond)->Iterations(50);

// ===========================================================================
// Via-path alternative routes
// S2-2.3: ~5x single CH query cost (~2.5 ms at state)
// ===========================================================================

static void BM_ViaPath_County(benchmark::State& state) {
    auto& f = county_fixture();
    ViaPathConfig cfg;
    size_t i = 0;
    for (auto _ : state) {
        auto [s, t] = f.pairs[i % f.pairs.size()];
        benchmark::DoNotOptimize(find_alternative_routes(f.ch, s, t, cfg));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ViaPath_County)->Unit(benchmark::kMicrosecond)->Iterations(500);

static void BM_ViaPath_State(benchmark::State& state) {
    auto& f = state_fixture();
    ViaPathConfig cfg;
    size_t i = 0;
    for (auto _ : state) {
        auto [s, t] = f.pairs[i % f.pairs.size()];
        benchmark::DoNotOptimize(find_alternative_routes(f.ch, s, t, cfg));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ViaPath_State)->Unit(benchmark::kMicrosecond)->Iterations(200);

// ===========================================================================
// BlockedCHQuery: core inner loop of route_fragility
// ===========================================================================

static void BM_BlockedCHQuery_County(benchmark::State& state) {
    auto& f = county_fixture();
    BlockedCHQuery bq(f.ch, *f.idx, *f.graph);
    CHQuery query(f.ch);

    // Get a real path edge to block
    RouteResult rr = query.route(f.pairs[0].first, f.pairs[0].second);
    std::vector<std::pair<NodeID, NodeID>> block_edges;
    if (rr.path.size() >= 2) {
        block_edges.push_back({rr.path[0], rr.path[1]});
    }

    size_t i = 0;
    for (auto _ : state) {
        auto [s, t] = f.pairs[i % f.pairs.size()];
        benchmark::DoNotOptimize(bq.distance_blocking(s, t, block_edges));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BlockedCHQuery_County)->Unit(benchmark::kMicrosecond)->Iterations(500);

// ===========================================================================
// ShortcutIndex construction time
// ===========================================================================

static void BM_ShortcutIndex_Build_County(benchmark::State& state) {
    auto& f = county_fixture();
    for (auto _ : state) {
        benchmark::DoNotOptimize(ShortcutIndex(f.ch));
    }
}
BENCHMARK(BM_ShortcutIndex_Build_County)->Unit(benchmark::kMillisecond);

static void BM_ShortcutIndex_Build_State(benchmark::State& state) {
    auto& f = state_fixture();
    for (auto _ : state) {
        benchmark::DoNotOptimize(ShortcutIndex(f.ch));
    }
}
BENCHMARK(BM_ShortcutIndex_Build_State)->Unit(benchmark::kMillisecond);

// ===========================================================================
// CH construction cost
// ===========================================================================

static void BM_CHBuild_County(benchmark::State& state) {
    auto& f = county_fixture();
    for (auto _ : state) {
        benchmark::DoNotOptimize(build_ch(*f.graph));
    }
}
BENCHMARK(BM_CHBuild_County)->Unit(benchmark::kMillisecond)->Iterations(3);

// ===========================================================================
// Fragility validation (correctness check)
// ===========================================================================

static void BM_ValidateFragility_County(benchmark::State& state) {
    auto& f = validation_fixture();
    FragilityValidationConfig cfg;
    cfg.sample_count = 10;
    for (auto _ : state) {
        benchmark::DoNotOptimize(validate_fragility(f.ch, *f.idx, *f.graph, cfg));
    }
}
BENCHMARK(BM_ValidateFragility_County)->Unit(benchmark::kMillisecond)->Iterations(3);

// ===========================================================================
// v0.5-v0.9: Spectral metrics, betweenness, landmarks, filter pipeline
// ===========================================================================

static void BM_AlgebraicConnectivity_County(benchmark::State& state) {
    auto& f = county_fixture();
    for (auto _ : state) {
        benchmark::DoNotOptimize(algebraic_connectivity(*f.graph));
    }
}
BENCHMARK(BM_AlgebraicConnectivity_County)->Unit(benchmark::kMillisecond)->Iterations(3);

static void BM_EdgeBetweenness_County_Sampled100(benchmark::State& state) {
    auto& f = county_fixture();
    BetweennessConfig cfg;
    cfg.sample_sources = 100;
    cfg.seed = 42;
    for (auto _ : state) {
        benchmark::DoNotOptimize(edge_betweenness(*f.graph, cfg));
    }
}
BENCHMARK(BM_EdgeBetweenness_County_Sampled100)->Unit(benchmark::kMillisecond)->Iterations(3);

static void BM_Landmarks_County_8(benchmark::State& state) {
    auto& f = county_fixture();
    for (auto _ : state) {
        benchmark::DoNotOptimize(precompute_landmarks(*f.graph, 8, 42));
    }
}
BENCHMARK(BM_Landmarks_County_8)->Unit(benchmark::kMillisecond)->Iterations(3);

static void BM_FilteredFragility_County(benchmark::State& state) {
    auto& f = county_fixture();
    static LandmarkData lm = precompute_landmarks(*f.graph, 8, 42);
    size_t i = 0;
    for (auto _ : state) {
        auto [s, t] = f.pairs[i % f.pairs.size()];
        benchmark::DoNotOptimize(filtered_route_fragility(f.ch, *f.idx, *f.graph, lm, s, t));
        ++i;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_FilteredFragility_County)->Unit(benchmark::kMillisecond)->Iterations(20);

static void BM_CHBuild_County_Parallel(benchmark::State& state) {
    auto& f = county_fixture();
    CHBuildConfig cfg;
    cfg.parallel = true;
    for (auto _ : state) {
        benchmark::DoNotOptimize(build_ch(*f.graph, cfg));
    }
}
BENCHMARK(BM_CHBuild_County_Parallel)->Unit(benchmark::kMillisecond)->Iterations(3);

// ===========================================================================
BENCHMARK_MAIN();
