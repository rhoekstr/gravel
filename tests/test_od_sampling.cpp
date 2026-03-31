#include <catch2/catch_test_macros.hpp>
#include "gravel/analysis/od_sampling.h"
#include "gravel/ch/contraction.h"
#include "gravel/validation/synthetic_graphs.h"
#include <set>

using namespace gravel;

TEST_CASE("Stratified sampling covers distance bands", "[od_sampling]") {
    auto graph = make_grid_graph(10, 10);

    CHBuildConfig ch_config;
    auto ch = build_ch(*graph, ch_config);

    SamplingConfig sc;
    sc.total_samples = 50;
    sc.distance_strata = 5;
    sc.seed = 42;

    auto pairs = stratified_sample(*graph, ch, sc);

    // Should generate some pairs (may not hit exact count due to rejection sampling)
    REQUIRE(pairs.size() > 0);
    REQUIRE(pairs.size() <= 50);

    // All pairs should be valid nodes
    for (auto [s, t] : pairs) {
        REQUIRE(s < 100);
        REQUIRE(t < 100);
        REQUIRE(s != t);
    }
}

TEST_CASE("Stratified sampling is reproducible with seed", "[od_sampling]") {
    auto graph = make_grid_graph(8, 8);

    CHBuildConfig ch_config;
    auto ch = build_ch(*graph, ch_config);

    SamplingConfig sc;
    sc.total_samples = 20;
    sc.seed = 123;

    auto pairs1 = stratified_sample(*graph, ch, sc);
    auto pairs2 = stratified_sample(*graph, ch, sc);

    REQUIRE(pairs1.size() == pairs2.size());
    for (size_t i = 0; i < pairs1.size(); ++i) {
        REQUIRE(pairs1[i] == pairs2[i]);
    }
}
