add_executable(gravel-cli
    main.cpp
    cmd_build_graph.cpp
    cmd_build_ch.cpp
    cmd_route.cpp
    cmd_matrix.cpp
    cmd_validate.cpp
    cmd_route_fragility.cpp
    cmd_batch_fragility.cpp
    cmd_county_fragility.cpp
    cmd_county_index.cpp
    cmd_location_fragility.cpp
    cmd_snap.cpp
    cmd_simplify.cpp
    cmd_progressive_fragility.cpp
)

target_include_directories(gravel-cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gravel-cli PRIVATE gravel)
set_target_properties(gravel-cli PROPERTIES OUTPUT_NAME "gravel")
