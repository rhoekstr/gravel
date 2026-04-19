#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/geo/elevation.h"
#include <cmath>
#include <string>

using namespace gravel;

TEST_CASE("Elevation from array", "[elevation]") {
    auto elev = elevation_from_array({100.0, 200.0, 300.0, NAN, 500.0});

    REQUIRE(elev.has_elevation(0));
    REQUIRE(elev.has_elevation(1));
    REQUIRE(elev.has_elevation(2));
    REQUIRE_FALSE(elev.has_elevation(3));  // NAN
    REQUIRE(elev.has_elevation(4));

    REQUIRE_THAT(elev.node_elevation[0], Catch::Matchers::WithinAbs(100.0, 1e-6));
    REQUIRE_THAT(elev.node_elevation[4], Catch::Matchers::WithinAbs(500.0, 1e-6));
}

TEST_CASE("Edge max elevation", "[elevation]") {
    auto elev = elevation_from_array({100.0, 500.0, 300.0});

    REQUIRE_THAT(elev.edge_max_elevation(0, 1), Catch::Matchers::WithinAbs(500.0, 1e-6));
    REQUIRE_THAT(elev.edge_max_elevation(0, 2), Catch::Matchers::WithinAbs(300.0, 1e-6));
    REQUIRE_THAT(elev.edge_max_elevation(1, 2), Catch::Matchers::WithinAbs(500.0, 1e-6));
}

TEST_CASE("Edge max elevation with NaN", "[elevation]") {
    auto elev = elevation_from_array({100.0, NAN, 300.0});

    // One endpoint NaN: return the other
    REQUIRE_THAT(elev.edge_max_elevation(0, 1), Catch::Matchers::WithinAbs(100.0, 1e-6));
    REQUIRE_THAT(elev.edge_max_elevation(1, 2), Catch::Matchers::WithinAbs(300.0, 1e-6));

    // Both NaN
    auto elev2 = elevation_from_array({NAN, NAN});
    REQUIRE(std::isnan(elev2.edge_max_elevation(0, 1)));
}

TEST_CASE("Elevation serialization round-trip", "[elevation]") {
    auto original = elevation_from_array({100.0, 200.0, 300.0, NAN, 500.0});
    std::string path = "/tmp/test_elevation.gravel.elev";
    save_elevation(original, path);

    auto loaded = load_elevation(path);
    REQUIRE(loaded.node_elevation.size() == 5);
    REQUIRE_THAT(loaded.node_elevation[0], Catch::Matchers::WithinAbs(100.0, 1e-6));
    REQUIRE_THAT(loaded.node_elevation[4], Catch::Matchers::WithinAbs(500.0, 1e-6));
    REQUIRE(std::isnan(loaded.node_elevation[3]));
}
