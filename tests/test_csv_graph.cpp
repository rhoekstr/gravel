#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/core/csv_graph.h"
#include "gravel/core/dijkstra.h"
#include <cstdio>
#include <fstream>

using namespace gravel;
using Catch::Matchers::WithinAbs;

TEST_CASE("CSV graph loads correctly", "[csv]") {
    CSVConfig cfg;
    cfg.path = TEST_DATA_DIR "/small_graph.csv";
    auto g = load_csv_graph(cfg);

    REQUIRE(g->node_count() == 4);
    REQUIRE(g->edge_count() == 10);

    // Check shortest path: 0 -> 3 should be 0->1->2->3 = 1+2+3 = 6
    auto d = dijkstra_pair(*g, 0, 3);
    REQUIRE_THAT(d, WithinAbs(6.0, 1e-9));
}

TEST_CASE("CSV graph with bidirectional flag", "[csv]") {
    // Write a temp CSV with one-directional edges
    std::string tmp = "/tmp/gravel_test_bidir.csv";
    {
        std::ofstream out(tmp);
        out << "src,dst,w\n";
        out << "0,1,5.0\n";
        out << "1,2,3.0\n";
    }

    CSVConfig cfg;
    cfg.path = tmp;
    cfg.source_col = "src";
    cfg.target_col = "dst";
    cfg.weight_col = "w";
    cfg.bidirectional = true;

    auto g = load_csv_graph(cfg);
    REQUIRE(g->node_count() == 3);
    REQUIRE(g->edge_count() == 4);  // 2 original + 2 reverse

    // Can route backwards
    REQUIRE(dijkstra_pair(*g, 2, 0) < INF_WEIGHT);

    std::remove(tmp.c_str());
}
