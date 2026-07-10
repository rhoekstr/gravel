#include <catch2/catch_test_macros.hpp>

#include "gravel/analysis/cascade_fragility.h"
#include "gravel/validation/synthetic_graphs.h"

#include <vector>

using namespace gravel;

// Exact (deterministic) betweenness so cascades are reproducible.
static CascadeFragilityConfig base_cfg() {
    CascadeFragilityConfig cfg;
    cfg.betweenness_config.sample_sources = 0;  // exact
    cfg.max_iterations = 40;
    return cfg;
}

TEST_CASE("Cascade: huge tolerance contains the cascade near the trigger", "[cascade]") {
    auto g = make_grid_graph(6, 6);
    auto cfg = base_cfg();
    cfg.alpha = 100.0;  // enormous headroom

    auto r = cascade_fragility(*g, cfg);
    REQUIRE(r.trigger_size >= 1);
    CHECK(r.cascade_size >= r.trigger_size);
    CHECK(r.cascade_fraction < 0.3);  // little/no secondary failure
    CHECK(r.failed_edges.size() == r.cascade_size);
}

TEST_CASE("Cascade: tight tolerance cascades at least as far as loose", "[cascade]") {
    auto g = make_grid_graph(6, 6);
    auto hi = base_cfg();
    hi.alpha = 100.0;
    auto lo = base_cfg();
    lo.alpha = 0.01;

    auto rhi = cascade_fragility(*g, hi);
    auto rlo = cascade_fragility(*g, lo);
    CHECK(rlo.cascade_size >= rhi.cascade_size);  // less tolerance => bigger cascade
}

TEST_CASE("Cascade: reproducible with exact betweenness", "[cascade]") {
    auto g = make_grid_graph(7, 7);
    auto cfg = base_cfg();
    cfg.alpha = 0.2;

    auto a = cascade_fragility(*g, cfg);
    auto b = cascade_fragility(*g, cfg);
    CHECK(a.cascade_size == b.cascade_size);
    CHECK(a.iterations == b.iterations);
}

TEST_CASE("Cascade: cascade_vs_alpha endpoints are ordered by tolerance", "[cascade]") {
    auto g = make_grid_graph(6, 6);
    auto cfg = base_cfg();
    std::vector<double> alphas = {0.01, 0.5, 100.0};

    auto pts = cascade_vs_alpha(*g, cfg, alphas);
    REQUIRE(pts.size() == 3);
    CHECK(pts.front().alpha == 0.01);
    CHECK(pts.back().alpha == 100.0);
    // Most tolerance => smallest cascade fraction.
    CHECK(pts.front().cascade_fraction >= pts.back().cascade_fraction - 1e-12);
}

TEST_CASE("Cascade: explicit trigger edge is honored", "[cascade]") {
    auto g = make_grid_graph(5, 5);
    auto cfg = base_cfg();
    cfg.alpha = 0.1;
    // Trigger a specific edge that exists in a grid (node 0 -> node 1).
    cfg.trigger_edges = {{0, 1}};

    auto r = cascade_fragility(*g, cfg);
    CHECK(r.trigger_size == 1);
    CHECK(r.cascade_size >= 1);
}

TEST_CASE("Cascade: largest_component_fraction tracks fragmentation", "[cascade]") {
    // A tree with a few redundant edges: robust enough that a contained cascade leaves it
    // whole, fragile enough that a tight-tolerance cascade shatters it.
    auto g = make_tree_with_bridges(63, 4, 42);
    auto whole = base_cfg();
    whole.alpha = 100.0;  // contained cascade
    whole.betweenness_config.deterministic = true;  // stable near the capacity threshold
    auto shatter = base_cfg();
    shatter.alpha = 0.01;  // shattering cascade
    shatter.betweenness_config.deterministic = true;

    auto rw = cascade_fragility(*g, whole);
    auto rs = cascade_fragility(*g, shatter);

    // Valid fraction in (0, 1].
    CHECK(rs.largest_component_fraction > 0.0);
    CHECK(rw.largest_component_fraction <= 1.0);
    // A contained cascade leaves the network essentially whole...
    CHECK(rw.largest_component_fraction >= 0.99);
    // ...while a tight tolerance shatters it (largest surviving component well under whole)...
    CHECK(rs.largest_component_fraction < 0.9);
    // ...and less tolerance never fragments less than more.
    CHECK(rs.largest_component_fraction <= rw.largest_component_fraction);
}
