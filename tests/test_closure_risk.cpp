#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/analysis/closure_risk.h"
#include <cmath>
#include <utility>
#include <vector>
#include <string>

using namespace gravel;

// Build a simple 4-node graph: 0-1-2-3 (path)
static ArrayGraph make_path4() {
    std::vector<Edge> edges = {
        {0, 1, 1.0}, {1, 0, 1.0},
        {1, 2, 1.0}, {2, 1, 1.0},
        {2, 3, 1.0}, {3, 2, 1.0}
    };
    return ArrayGraph(4, std::move(edges));
}

TEST_CASE("Closure risk classification - low elevation", "[closure_risk]") {
    auto graph = make_path4();
    auto elev = elevation_from_array({200.0, 300.0, 250.0, 100.0});

    auto risk = classify_closure_risk(graph, elev);

    // All edges below moderate threshold (900m) → all LOW
    for (auto tier : risk.edge_tiers) {
        REQUIRE(tier == ClosureRiskTier::LOW);
    }
    REQUIRE_THAT(risk.tier_fraction(ClosureRiskTier::LOW), Catch::Matchers::WithinAbs(1.0, 1e-6));
}

TEST_CASE("Closure risk classification - high elevation + minor road", "[closure_risk]") {
    auto graph = make_path4();
    // Nodes at increasing elevation, last two above severe threshold
    auto elev = elevation_from_array({200.0, 800.0, 1300.0, 1600.0});

    // Edge labels: all unclassified (rank 5 = minor road)
    std::vector<std::string> labels(graph.edge_count(), "unclassified");

    auto risk = classify_closure_risk(graph, elev, labels);

    // Edge 0→1: max(200, 800)=800 < 900 but rank=5 → MODERATE
    // Edge 1→2: max(800, 1300)=1300 >= 1200 and rank=5 >= 3 → HIGH
    // Edge 2→3: max(1300, 1600)=1600 >= 1500 and rank=5 >= 4 → SEVERE

    // Check that we have a mix of tiers
    REQUIRE(risk.tier_fraction(ClosureRiskTier::SEVERE) > 0.0);
    REQUIRE(risk.tier_fraction(ClosureRiskTier::HIGH) > 0.0);
}

TEST_CASE("Closure risk - major road reduces tier", "[closure_risk]") {
    auto graph = make_path4();
    auto elev = elevation_from_array({1600.0, 1600.0, 1600.0, 1600.0});

    // All motorway (rank 0) - even at extreme elevation, rank < 3 prevents HIGH/SEVERE
    std::vector<std::string> labels(graph.edge_count(), "motorway");
    auto risk = classify_closure_risk(graph, elev, labels);

    // Motorway rank=0 < 3, so even at 1600m, max tier is MODERATE (from elevation alone)
    for (auto tier : risk.edge_tiers) {
        REQUIRE(tier <= ClosureRiskTier::MODERATE);
    }
}

TEST_CASE("Seasonal weight multipliers", "[closure_risk]") {
    ClosureRiskData risk;
    risk.edge_tiers = {
        ClosureRiskTier::LOW, ClosureRiskTier::MODERATE,
        ClosureRiskTier::HIGH, ClosureRiskTier::SEVERE
    };

    auto mults = seasonal_weight_multipliers(risk, 1.0, 1.5, 3.0);

    REQUIRE(mults.size() == 4);
    REQUIRE_THAT(mults[0], Catch::Matchers::WithinAbs(1.0, 1e-6));  // LOW
    REQUIRE_THAT(mults[1], Catch::Matchers::WithinAbs(1.0, 1e-6));  // MODERATE
    REQUIRE_THAT(mults[2], Catch::Matchers::WithinAbs(1.5, 1e-6));  // HIGH
    REQUIRE_THAT(mults[3], Catch::Matchers::WithinAbs(3.0, 1e-6));  // SEVERE
}

TEST_CASE("Max tier on path", "[closure_risk]") {
    ClosureRiskData risk;
    risk.edge_tiers = {
        ClosureRiskTier::LOW, ClosureRiskTier::HIGH,
        ClosureRiskTier::MODERATE, ClosureRiskTier::SEVERE
    };

    REQUIRE(risk.max_tier_on_path({0, 2}) == ClosureRiskTier::MODERATE);
    REQUIRE(risk.max_tier_on_path({0, 1, 2, 3}) == ClosureRiskTier::SEVERE);
    REQUIRE(risk.max_tier_on_path({0}) == ClosureRiskTier::LOW);
}
