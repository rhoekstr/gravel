#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gravel/analysis/stochastic_fragility.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/validation/synthetic_graphs.h"

#include <stdexcept>
#include <vector>

using namespace gravel;
using Catch::Matchers::WithinAbs;

TEST_CASE("Stochastic fragility: zero probability => no failures => inflation ~1", "[stochastic]") {
    auto graph = make_grid_graph(6, 6);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    std::vector<double> p(graph->edge_count(), 0.0);  // nothing fails
    StochasticFragilityConfig cfg;
    cfg.monte_carlo_runs = 10;
    cfg.od_sample_count = 15;

    auto res = stochastic_fragility(*graph, ch, idx, p, cfg);
    REQUIRE(res.probe_pairs > 0);
    REQUIRE(res.runs == 10);
    CHECK_THAT(res.mean, WithinAbs(1.0, 1e-6));  // no failures -> no inflation
    CHECK_THAT(res.mean_disconnected_fraction, WithinAbs(0.0, 1e-12));
}

TEST_CASE("Stochastic fragility: positive probability => inflation >= 1", "[stochastic]") {
    auto graph = make_grid_graph(8, 8);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    std::vector<double> p(graph->edge_count(), 0.15);
    StochasticFragilityConfig cfg;
    cfg.monte_carlo_runs = 20;
    cfg.od_sample_count = 20;
    cfg.seed = 7;

    auto res = stochastic_fragility(*graph, ch, idx, p, cfg);
    REQUIRE(res.runs == 20);
    CHECK(res.mean >= 1.0 - 1e-6);           // inflation never below baseline
    CHECK(res.p90 >= res.p50 - 1e-9);        // percentiles ordered
    CHECK(res.exceedance.size() == cfg.exceedance_thresholds.size());
}

TEST_CASE("Stochastic fragility: reproducible for a fixed seed", "[stochastic]") {
    auto graph = make_grid_graph(7, 7);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    std::vector<double> p(graph->edge_count(), 0.1);
    StochasticFragilityConfig cfg;
    cfg.monte_carlo_runs = 12;
    cfg.od_sample_count = 12;
    cfg.seed = 99;

    auto a = stochastic_fragility(*graph, ch, idx, p, cfg);
    auto b = stochastic_fragility(*graph, ch, idx, p, cfg);
    REQUIRE(a.run_values.size() == b.run_values.size());
    for (std::size_t i = 0; i < a.run_values.size(); ++i)
        CHECK_THAT(a.run_values[i], WithinAbs(b.run_values[i], 1e-12));
}

TEST_CASE("Stochastic fragility: location-isolation target runs", "[stochastic]") {
    auto graph = make_grid_graph(6, 6);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    std::vector<double> p(graph->edge_count(), 0.1);
    StochasticFragilityConfig cfg;
    cfg.target = StochasticTarget::LOCATION_ISOLATION;
    cfg.center = 0;
    cfg.monte_carlo_runs = 8;
    cfg.od_sample_count = 12;

    auto res = stochastic_fragility(*graph, ch, idx, p, cfg);
    CHECK(res.probe_pairs > 0);
    CHECK(res.runs == 8);
}

TEST_CASE("Stochastic fragility: rejects mismatched probability length", "[stochastic]") {
    auto graph = make_grid_graph(4, 4);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);

    std::vector<double> p(3, 0.1);  // wrong length
    CHECK_THROWS_AS(stochastic_fragility(*graph, ch, idx, p), std::invalid_argument);
}

TEST_CASE("Stochastic fragility: per-edge failure frequency bounds & determinism", "[stochastic]") {
    auto graph = make_grid_graph(6, 6);
    auto ch = build_ch(*graph);
    ShortcutIndex idx(ch);
    const auto m = graph->edge_count();

    // Edge 0 always fails, edge 1 never fails; the rest are coin flips.
    std::vector<double> p(m, 0.5);
    p[0] = 1.0;
    p[1] = 0.0;
    StochasticFragilityConfig cfg;
    cfg.monte_carlo_runs = 32;
    cfg.od_sample_count = 8;
    cfg.seed = 5;

    auto a = stochastic_fragility(*graph, ch, idx, p, cfg);
    REQUIRE(a.edge_failure_frequency.size() == m);
    CHECK(a.edge_failure_frequency[0] == 1.0);   // certain-failure edge
    CHECK(a.edge_failure_frequency[1] == 0.0);   // never-failure edge
    for (double f : a.edge_failure_frequency) {
        CHECK(f >= 0.0);
        CHECK(f <= 1.0);
    }

    // Same seed => identical per-edge frequencies (integer counts, fixed per-run seeds).
    auto b = stochastic_fragility(*graph, ch, idx, p, cfg);
    for (std::size_t e = 0; e < m; ++e)
        CHECK(a.edge_failure_frequency[e] == b.edge_failure_frequency[e]);
}
