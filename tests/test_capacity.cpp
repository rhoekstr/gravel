#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "gravel/geo/capacity.h"
#include "gravel/core/array_graph.h"
#include "gravel/analysis/betweenness.h"

#include <stdexcept>
#include <vector>

using namespace gravel;
using Catch::Matchers::WithinAbs;

// Build an EdgeMetadata with per-edge highway + lanes tags (CSR order).
static EdgeMetadata make_metadata(std::vector<std::string> highway,
                                  std::vector<std::string> lanes) {
    EdgeMetadata md;
    md.tag_keys = {"highway", "lanes"};
    md.tag_values = {std::move(highway), std::move(lanes)};
    return md;
}

TEST_CASE("Capacity: HCM defaults compute lanes x per-lane capacity", "[capacity]") {
    auto md = make_metadata(
        {"motorway", "primary",  "residential", "service", "some_unknown_class"},
        {"3",        "",         "2",           "",        ""});

    auto cap = estimate_capacity(md);  // HCM default
    REQUIRE(cap.size() == 5);

    // motorway, lanes=3 -> 2200 * 3
    CHECK_THAT(cap[0], WithinAbs(6600.0, 1e-9));
    // primary, no lanes -> default 2 lanes -> 1700 * 2
    CHECK_THAT(cap[1], WithinAbs(3400.0, 1e-9));
    // residential, lanes=2 -> 800 * 2
    CHECK_THAT(cap[2], WithinAbs(1600.0, 1e-9));
    // service, no lanes -> default 1 -> 400
    CHECK_THAT(cap[3], WithinAbs(400.0, 1e-9));
    // unmapped class -> fallback
    CHECK_THAT(cap[4], WithinAbs(600.0, 1e-9));
}

TEST_CASE("Capacity: lanes tag overrides the class default", "[capacity]") {
    auto md = make_metadata({"primary", "primary"}, {"", "4"});
    auto cap = estimate_capacity(md);
    CHECK_THAT(cap[0], WithinAbs(1700.0 * 2.0, 1e-9));  // default 2 lanes
    CHECK_THAT(cap[1], WithinAbs(1700.0 * 4.0, 1e-9));  // explicit 4 lanes
}

TEST_CASE("Capacity: malformed lanes values fall back to the class default", "[capacity]") {
    auto md = make_metadata({"secondary", "secondary", "secondary"},
                            {"abc", "0", "2;3"});
    auto cap = estimate_capacity(md);
    // "abc" unparseable -> default 1 lane; "0" -> non-positive -> default;
    // "2;3" -> leading number 2.
    CHECK_THAT(cap[0], WithinAbs(1400.0 * 1.0, 1e-9));
    CHECK_THAT(cap[1], WithinAbs(1400.0 * 1.0, 1e-9));
    CHECK_THAT(cap[2], WithinAbs(1400.0 * 2.0, 1e-9));
}

TEST_CASE("Capacity: config constants are overridable (sweepable)", "[capacity]") {
    auto md = make_metadata({"motorway"}, {"2"});

    CapacityConfig cfg = CapacityConfig::hcm();
    cfg.per_lane_capacity["motorway"] = 2000.0;  // sweep the constant down
    auto cap = estimate_capacity(md, cfg);
    CHECK_THAT(cap[0], WithinAbs(2000.0 * 2.0, 1e-9));
}

TEST_CASE("Capacity: empty metadata yields empty result", "[capacity]") {
    EdgeMetadata empty;
    auto cap = estimate_capacity(empty);
    CHECK(cap.empty());
}

TEST_CASE("Capacity: criticality and weighted importance derive from betweenness", "[capacity]") {
    std::vector<Edge> edges = {
        {0, 1, 1.0}, {1, 2, 1.0}, {2, 3, 1.0}, {1, 3, 2.0}, {0, 2, 2.0}};
    ArrayGraph g(4, edges);
    const EdgeID m = g.edge_count();

    std::vector<double> capacity(m);
    for (EdgeID e = 0; e < m; ++e) capacity[e] = 100.0 * static_cast<double>(e + 1);

    BetweennessConfig cfg;
    cfg.edge_capacity = capacity;
    auto b = edge_betweenness(g, cfg);

    // criticality = betweenness / capacity
    REQUIRE(b.criticality.size() == m);
    for (EdgeID e = 0; e < m; ++e) {
        double expected = capacity[e] > 0.0 ? b.edge_scores[e] / capacity[e] : 0.0;
        CHECK_THAT(b.criticality[e], WithinAbs(expected, 1e-9));
    }

    // capacity_weighted_importance = betweenness * capacity
    auto imp = capacity_weighted_importance(b, capacity);
    REQUIRE(imp.size() == m);
    for (EdgeID e = 0; e < m; ++e) {
        CHECK_THAT(imp[e], WithinAbs(b.edge_scores[e] * capacity[e], 1e-9));
    }
}

TEST_CASE("Capacity: criticality is skipped when no capacity supplied", "[capacity]") {
    ArrayGraph g(3, std::vector<Edge>{{0, 1, 1.0}, {1, 2, 1.0}});
    auto b = edge_betweenness(g);  // no edge_capacity
    CHECK(b.criticality.empty());
}

TEST_CASE("Capacity: weighted importance rejects size mismatch", "[capacity]") {
    BetweennessResult b;
    b.edge_scores = {1.0, 2.0};
    std::vector<double> cap = {1.0};  // wrong length
    CHECK_THROWS_AS(capacity_weighted_importance(b, cap), std::invalid_argument);
}
