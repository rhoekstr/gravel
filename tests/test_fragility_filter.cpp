#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "gravel/fragility/fragility_filter.h"
#include "gravel/fragility/route_fragility.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/ch/landmarks.h"
#include "gravel/validation/synthetic_graphs.h"
#include <cmath>

using namespace gravel;

TEST_CASE("Filtered fragility matches unfiltered on computed edges", "[fragility_filter]") {
    auto graph = make_grid_graph(5, 5);

    CHBuildConfig ch_config;
    auto ch = build_ch(*graph, ch_config);
    ShortcutIndex idx(ch);
    auto landmarks = precompute_landmarks(*graph, 4, 42);

    NodeID s = 0, t = 24;

    // Unfiltered
    auto unfiltered = route_fragility(ch, idx, *graph, s, t);
    REQUIRE(unfiltered.valid());

    // Filtered (with all filters disabled - should match exactly)
    FilterConfig fc;
    fc.use_ch_level_filter = false;
    fc.use_alt_filter = false;
    auto filtered = filtered_route_fragility(ch, idx, *graph, landmarks, s, t, fc);

    REQUIRE(filtered.valid());
    REQUIRE(filtered.edge_fragilities.size() == unfiltered.edge_fragilities.size());

    // When no filters active, all edges should be computed (not screened by CH/ALT)
    // Bridge filter may still screen some edges
    for (size_t i = 0; i < filtered.edge_fragilities.size(); ++i) {
        const auto& fe = filtered.edge_fragilities[i];
        const auto& ue = unfiltered.edge_fragilities[i];

        // Both should agree on bridge status
        bool f_bridge = std::isinf(fe.fragility_ratio);
        bool u_bridge = std::isinf(ue.fragility_ratio);
        REQUIRE(f_bridge == u_bridge);

        // For non-bridge edges that were computed, distances should match
        if (!f_bridge && !u_bridge) {
            REQUIRE_THAT(fe.replacement_distance,
                         Catch::Matchers::WithinAbs(ue.replacement_distance, 1e-6));
        }
    }
}

TEST_CASE("Filtered fragility screens some edges with filters on", "[fragility_filter]") {
    auto graph = make_grid_graph(8, 8);

    CHBuildConfig ch_config;
    auto ch = build_ch(*graph, ch_config);
    ShortcutIndex idx(ch);
    auto landmarks = precompute_landmarks(*graph, 4, 42);

    NodeID s = 0, t = 63;

    FilterConfig fc;
    fc.use_ch_level_filter = true;
    fc.use_alt_filter = true;
    auto result = filtered_route_fragility(ch, idx, *graph, landmarks, s, t, fc);

    REQUIRE(result.valid());
    REQUIRE(result.edges_screened + result.edges_computed ==
            result.edge_fragilities.size());
}
