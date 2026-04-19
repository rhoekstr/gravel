#include <catch2/catch_test_macros.hpp>
#include "gravel/core/types.h"
#include <limits>

using namespace gravel;

TEST_CASE("INVALID_NODE is max uint32", "[types]") {
    REQUIRE(INVALID_NODE == std::numeric_limits<uint32_t>::max());
}

TEST_CASE("INF_WEIGHT is positive infinity", "[types]") {
    REQUIRE(INF_WEIGHT == std::numeric_limits<double>::infinity());
    REQUIRE(INF_WEIGHT > 1e18);
}

TEST_CASE("Edge default secondary_weight is zero", "[types]") {
    Edge e{0, 1, 5.0};
    REQUIRE(e.secondary_weight == 0.0);
    REQUIRE(e.label.empty());
}

TEST_CASE("Coord default is zero", "[types]") {
    Coord c;
    REQUIRE(c.lat == 0.0);
    REQUIRE(c.lon == 0.0);
}
