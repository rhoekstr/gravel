#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/core/geo_math.h"

using namespace gravel;

TEST_CASE("Haversine known distances", "[geo]") {
    // New York to London: approximately 5,570 km
    Coord nyc = {40.7128, -74.0060};
    Coord london = {51.5074, -0.1278};
    double d = haversine_meters(nyc, london);
    REQUIRE_THAT(d, Catch::Matchers::WithinRel(5570000.0, 0.02));  // within 2%

    // Same point
    REQUIRE(haversine_meters(nyc, nyc) == 0.0);

    // Short distance: ~111 km per degree of latitude
    Coord a = {35.0, -83.0};
    Coord b = {36.0, -83.0};
    double d2 = haversine_meters(a, b);
    REQUIRE_THAT(d2, Catch::Matchers::WithinRel(111000.0, 0.01));  // within 1%
}

TEST_CASE("Projection onto segment - midpoint", "[geo]") {
    Coord a = {35.0, -83.0};
    Coord b = {35.0, -82.0};
    Coord point = {35.001, -82.5};  // slightly above midpoint

    auto result = project_to_segment(point, a, b);
    REQUIRE_THAT(result.t, Catch::Matchers::WithinAbs(0.5, 0.01));
    REQUIRE_THAT(result.projected.lon, Catch::Matchers::WithinAbs(-82.5, 0.01));
    REQUIRE(result.distance_m > 0.0);
    REQUIRE(result.distance_m < 200.0);  // should be close
}

TEST_CASE("Projection onto segment - endpoint clamp", "[geo]") {
    Coord a = {35.0, -83.0};
    Coord b = {35.0, -82.0};

    SECTION("point beyond start") {
        Coord point = {35.0, -84.0};
        auto result = project_to_segment(point, a, b);
        REQUIRE_THAT(result.t, Catch::Matchers::WithinAbs(0.0, 1e-6));
    }

    SECTION("point beyond end") {
        Coord point = {35.0, -81.0};
        auto result = project_to_segment(point, a, b);
        REQUIRE_THAT(result.t, Catch::Matchers::WithinAbs(1.0, 1e-6));
    }
}

TEST_CASE("Projection onto degenerate segment", "[geo]") {
    Coord a = {35.0, -83.0};
    auto result = project_to_segment({35.001, -83.001}, a, a);
    REQUIRE_THAT(result.t, Catch::Matchers::WithinAbs(0.0, 1e-6));
    REQUIRE(result.distance_m > 0.0);
}
