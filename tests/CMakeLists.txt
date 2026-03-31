include(Catch)

set(TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")

add_executable(gravel_tests
    test_types.cpp
    test_array_graph.cpp
    test_priority_queue.cpp
    test_four_heap.cpp
    test_dijkstra.cpp
    test_synthetic_graphs.cpp
    test_csv_graph.cpp
    test_ch_vs_dijkstra.cpp
    test_serialization.cpp
    test_geo_math.cpp
    test_hilbert_order.cpp
    test_snapping.cpp
    test_bridges.cpp
    test_blocked_ch_query.cpp
    test_route_fragility.cpp
    test_hershberger_suri.cpp
    test_via_path.cpp
    test_bernstein.cpp
    test_subgraph.cpp
    test_laplacian.cpp
    test_algebraic_connectivity.cpp
    test_betweenness.cpp
    test_kirchhoff.cpp
    test_natural_connectivity.cpp
    test_accessibility.cpp
    test_county_fragility.cpp
    test_parallel_ch.cpp
    test_landmarks.cpp
    test_fragility_filter.cpp
    test_od_sampling.cpp
    test_validation_connectivity.cpp
    test_location_fragility.cpp
    test_elevation.cpp
    test_closure_risk.cpp
    test_snap_validation.cpp
    test_output_formats.cpp
    test_simplify.cpp
    test_v2_enhancements.cpp
    test_progressive_fragility.cpp
    test_edge_sampler.cpp
    test_incremental_sssp.cpp
    test_region_assignment.cpp
    test_boundary_contraction.cpp
    test_deferred_features.cpp
)

if(GRAVEL_USE_OSMIUM)
    target_sources(gravel_tests PRIVATE
        test_real_osm.cpp
        test_swain_progressive.cpp
        test_swain_timing.cpp
        test_performance_profile.cpp
    )
endif()

target_link_libraries(gravel_tests PRIVATE gravel Catch2::Catch2WithMain)
target_compile_definitions(gravel_tests PRIVATE TEST_DATA_DIR="${TEST_DATA_DIR}")

catch_discover_tests(gravel_tests)
