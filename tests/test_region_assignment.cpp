#include <catch2/catch_test_macros.hpp>
#include "gravel/geo/region_assignment.h"
#include "gravel/geo/geojson_loader.h"
#include "gravel/core/array_graph.h"

using namespace gravel;

// Two adjacent square regions covering a small area
static std::vector<RegionSpec> make_two_regions() {
    RegionSpec r1;
    r1.region_id = "R1";
    r1.label = "Region 1";
    r1.boundary.vertices = {{35.0, -84.0}, {35.0, -83.5}, {35.5, -83.5}, {35.5, -84.0}, {35.0, -84.0}};

    RegionSpec r2;
    r2.region_id = "R2";
    r2.label = "Region 2";
    r2.boundary.vertices = {{35.0, -83.5}, {35.0, -83.0}, {35.5, -83.0}, {35.5, -83.5}, {35.0, -83.5}};

    return {r1, r2};
}

// Graph with 4 nodes: 0,1 in R1; 2,3 in R2
static ArrayGraph make_two_region_graph() {
    std::vector<Coord> coords = {
        {35.25, -83.75},  // node 0: in R1
        {35.25, -83.60},  // node 1: in R1
        {35.25, -83.40},  // node 2: in R2
        {35.25, -83.25},  // node 3: in R2
    };
    std::vector<Edge> edges = {
        {0, 1, 10}, {1, 0, 10},
        {1, 2, 20}, {2, 1, 20},
        {2, 3, 10}, {3, 2, 10},
    };
    ArrayGraph g(4, std::move(edges));
    return ArrayGraph(g.raw_offsets(), g.raw_targets(), g.raw_weights(), std::move(coords));
}

TEST_CASE("RegionAssignment basic assignment", "[region_assignment]") {
    auto graph = make_two_region_graph();
    auto regions = make_two_regions();

    auto result = assign_nodes_to_regions(graph, regions);

    REQUIRE(result.region_index.size() == 4);
    REQUIRE(result.region_index[0] == 0);  // R1
    REQUIRE(result.region_index[1] == 0);  // R1
    REQUIRE(result.region_index[2] == 1);  // R2
    REQUIRE(result.region_index[3] == 1);  // R2
    REQUIRE(result.region_node_counts[0] == 2);
    REQUIRE(result.region_node_counts[1] == 2);
    REQUIRE(result.unassigned_count == 0);
}

TEST_CASE("RegionAssignment unassigned nodes", "[region_assignment]") {
    std::vector<Coord> coords = {
        {35.25, -83.75},  // in R1
        {0.0, 0.0},       // not in any region
    };
    std::vector<Edge> edges = {{0, 1, 10}, {1, 0, 10}};
    ArrayGraph g(2, std::move(edges));
    auto graph = ArrayGraph(g.raw_offsets(), g.raw_targets(), g.raw_weights(), std::move(coords));

    auto regions = make_two_regions();
    auto result = assign_nodes_to_regions(graph, regions);

    REQUIRE(result.region_index[0] == 0);   // R1
    REQUIRE(result.region_index[1] == -1);  // unassigned
    REQUIRE(result.unassigned_count == 1);
}

TEST_CASE("RegionAssignment region_id_of accessor", "[region_assignment]") {
    auto graph = make_two_region_graph();
    auto regions = make_two_regions();
    auto result = assign_nodes_to_regions(graph, regions);

    REQUIRE(result.region_id_of(0) == "R1");
    REQUIRE(result.region_id_of(2) == "R2");
}

TEST_CASE("GeoJSON loader parses Polygon features", "[geojson_loader]") {
    std::string geojson = R"({
        "type": "FeatureCollection",
        "features": [
            {
                "type": "Feature",
                "properties": {"GEOID": "37173", "NAMELSAD": "Swain County"},
                "geometry": {
                    "type": "Polygon",
                    "coordinates": [[
                        [-83.75, 35.25], [-83.10, 35.25],
                        [-83.10, 35.55], [-83.75, 35.55],
                        [-83.75, 35.25]
                    ]]
                }
            }
        ]
    })";

    auto regions = load_regions_geojson_string(geojson);
    REQUIRE(regions.size() == 1);
    REQUIRE(regions[0].region_id == "37173");
    REQUIRE(regions[0].label == "Swain County");

    // Verify coordinate swap: GeoJSON [-83.75, 35.25] → Gravel {35.25, -83.75}
    REQUIRE(regions[0].boundary.vertices[0].lat == 35.25);
    REQUIRE(regions[0].boundary.vertices[0].lon == -83.75);
}

TEST_CASE("GeoJSON loader custom property names", "[geojson_loader]") {
    std::string geojson = R"({
        "type": "FeatureCollection",
        "features": [
            {
                "type": "Feature",
                "properties": {"CODE": "ABC", "NAME": "Test Region"},
                "geometry": {
                    "type": "Polygon",
                    "coordinates": [[[-1,1],[-1,-1],[1,-1],[1,1],[-1,1]]]
                }
            }
        ]
    })";

    GeoJSONLoadConfig cfg;
    cfg.region_id_property = "CODE";
    cfg.label_property = "NAME";

    auto regions = load_regions_geojson_string(geojson, cfg);
    REQUIRE(regions.size() == 1);
    REQUIRE(regions[0].region_id == "ABC");
    REQUIRE(regions[0].label == "Test Region");
}

TEST_CASE("GeoJSON loader handles MultiPolygon", "[geojson_loader]") {
    std::string geojson = R"({
        "type": "FeatureCollection",
        "features": [
            {
                "type": "Feature",
                "properties": {"GEOID": "12345"},
                "geometry": {
                    "type": "MultiPolygon",
                    "coordinates": [
                        [[[-1,1],[-1,-1],[1,-1],[1,1],[-1,1]]],
                        [[[-5,5],[-5,4],[-4,4],[-4,5],[-5,5]]]
                    ]
                }
            }
        ]
    })";

    auto regions = load_regions_geojson_string(geojson);
    REQUIRE(regions.size() == 1);
    REQUIRE(regions[0].boundary.vertices.size() == 5);  // first polygon only
}
