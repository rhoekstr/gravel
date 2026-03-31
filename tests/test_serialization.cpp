#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/core/graph_serialization.h"
#include "gravel/ch/ch_serialization.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/ch_query.h"
#include "gravel/validation/validator.h"
#include "gravel/validation/synthetic_graphs.h"
#include <cstdio>
#include <fstream>

using namespace gravel;

namespace {
struct TempFile {
    std::string path;
    TempFile(const char* name) : path(std::string("/tmp/gravel_test_") + name) {}
    ~TempFile() { std::remove(path.c_str()); }
};
}

TEST_CASE("Graph round-trip serialization", "[serialization]") {
    auto graph = make_grid_graph(5, 5);

    TempFile tmp("graph.gravel.meta");
    save_graph(*graph, tmp.path);

    auto loaded = load_graph(tmp.path);
    REQUIRE(loaded->node_count() == graph->node_count());
    REQUIRE(loaded->edge_count() == graph->edge_count());

    // Verify edges match
    for (NodeID u = 0; u < graph->node_count(); ++u) {
        auto orig_t = graph->outgoing_targets(u);
        auto orig_w = graph->outgoing_weights(u);
        auto load_t = loaded->outgoing_targets(u);
        auto load_w = loaded->outgoing_weights(u);
        REQUIRE(orig_t.size() == load_t.size());
        for (size_t i = 0; i < orig_t.size(); ++i) {
            REQUIRE(orig_t[i] == load_t[i]);
            REQUIRE(orig_w[i] == load_w[i]);
        }
    }
}

TEST_CASE("Graph serialization with coordinates", "[serialization]") {
    std::vector<uint32_t> offsets = {0, 1, 2, 2};
    std::vector<NodeID> targets = {1, 2};
    std::vector<Weight> weights = {3.0, 5.0};
    std::vector<Coord> coords = {{35.0, -83.0}, {35.1, -83.1}, {35.2, -83.2}};
    ArrayGraph graph(std::move(offsets), std::move(targets), std::move(weights), std::move(coords));

    TempFile tmp("graph_coords.gravel.meta");
    save_graph(graph, tmp.path);

    auto loaded = load_graph(tmp.path);
    auto c = loaded->node_coordinate(0);
    REQUIRE(c.has_value());
    REQUIRE_THAT(c->lat, Catch::Matchers::WithinAbs(35.0, 1e-9));
}

TEST_CASE("CH round-trip serialization preserves correctness", "[serialization]") {
    auto graph = make_grid_graph(10, 10);
    auto ch = build_ch(*graph);

    TempFile tmp("test.gravel.ch");
    save_ch(ch, tmp.path);

    auto loaded_ch = load_ch(tmp.path);
    CHQuery query(loaded_ch);

    // Validate loaded CH against original graph
    auto report = validate_ch(*graph, query, {.mode = ValidationConfig::EXHAUSTIVE});
    REQUIRE(report.passed);
    REQUIRE(report.mismatches == 0);
}

TEST_CASE("Graph mmap round-trip", "[serialization][mmap]") {
    auto graph = make_grid_graph(5, 5);

    TempFile tmp("graph_mmap.gravel.meta");
    save_graph(*graph, tmp.path);

    auto loaded = load_graph_mmap(tmp.path);
    REQUIRE(loaded->node_count() == graph->node_count());
    REQUIRE(loaded->edge_count() == graph->edge_count());

    for (NodeID u = 0; u < graph->node_count(); ++u) {
        auto orig_t = graph->outgoing_targets(u);
        auto load_t = loaded->outgoing_targets(u);
        REQUIRE(orig_t.size() == load_t.size());
        for (size_t i = 0; i < orig_t.size(); ++i) {
            REQUIRE(orig_t[i] == load_t[i]);
        }
    }
}

TEST_CASE("CH mmap round-trip preserves correctness", "[serialization][mmap]") {
    auto graph = make_grid_graph(10, 10);
    auto ch = build_ch(*graph);

    TempFile tmp("test_mmap.gravel.ch");
    save_ch(ch, tmp.path);

    auto loaded_ch = load_ch_mmap(tmp.path);
    CHQuery query(loaded_ch);

    auto report = validate_ch(*graph, query, {.mode = ValidationConfig::EXHAUSTIVE});
    REQUIRE(report.passed);
    REQUIRE(report.mismatches == 0);
}

TEST_CASE("Invalid magic throws", "[serialization]") {
    TempFile tmp("bad.gravel.meta");
    {
        std::ofstream out(tmp.path, std::ios::binary);
        out.write("XXXX", 4);
    }
    REQUIRE_THROWS(load_graph(tmp.path));
}
