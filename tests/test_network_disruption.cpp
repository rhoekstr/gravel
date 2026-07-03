#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gravel/analysis/network_disruption.h"

#include <cmath>
#include <limits>
#include <vector>

using namespace gravel;

TEST_CASE("network_disruption: severed curve + stranded on a splitting path", "[network_disruption]") {
    // Bidirectional path 0-1-2-3-4. Block both 2<->3 edges at round 1 -> {3,4} is cut off from the
    // hub (node 1, the highest-degree interior node in {0,1,2}).
    std::vector<uint32_t> offsets = {0, 1, 3, 5, 7, 8};
    std::vector<NodeID> targets = {1, 0, 2, 1, 3, 2, 4, 3};
    std::vector<Weight> weights(targets.size(), 1.0);
    ArrayGraph g(std::move(offsets), std::move(targets), std::move(weights),
                 std::vector<Coord>(5));

    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto off = g.raw_offsets();
    auto tg = g.raw_targets();
    std::vector<double> fr(tg.size(), nan);
    for (uint32_t u = 0; u < g.node_count(); ++u) {
        for (uint32_t e = off[u]; e < off[u + 1]; ++e) {
            NodeID v = tg[e];
            if ((u == 2 && v == 3) || (u == 3 && v == 2)) fr[e] = 1.0;  // block the 2-3 link
        }
    }

    auto res = network_disruption(g, fr);

    // Connectivity curve: stage 0 fully connected; stage 1 splits into {0,1,2} and {3,4}.
    REQUIRE(res.severed_fraction.size() == 2);
    REQUIRE(res.severed_fraction[0] == Catch::Approx(0.0));
    REQUIRE(res.severed_fraction[1] == Catch::Approx(1.0 - (9.0 + 4.0) / 25.0));

    // Stranded: the intact 3<->4 edges strand at round 1; the blocked 2-3 edges and the still-
    // connected {0,1,2} edges never strand.
    for (uint32_t u = 0; u < g.node_count(); ++u) {
        for (uint32_t e = off[u]; e < off[u + 1]; ++e) {
            NodeID v = tg[e];
            if ((u == 3 && v == 4) || (u == 4 && v == 3)) {
                REQUIRE(res.stranded_round[e] == Catch::Approx(1.0));
            } else {
                REQUIRE(std::isnan(res.stranded_round[e]));
            }
        }
    }
}

TEST_CASE("network_disruption: nothing removed -> no severance, no strand", "[network_disruption]") {
    std::vector<uint32_t> offsets = {0, 1, 2};
    std::vector<NodeID> targets = {1, 0};
    std::vector<Weight> weights(2, 1.0);
    ArrayGraph g(std::move(offsets), std::move(targets), std::move(weights),
                 std::vector<Coord>(2));
    auto res = network_disruption(g, {std::nan(""), std::nan("")});
    REQUIRE(res.severed_fraction.size() == 1);
    REQUIRE(res.severed_fraction[0] == Catch::Approx(0.0));
    REQUIRE(std::isnan(res.stranded_round[0]));
    REQUIRE(std::isnan(res.stranded_round[1]));
}
