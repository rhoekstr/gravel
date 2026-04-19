cmake_minimum_required(VERSION 3.20)
project(Gravel VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(GRAVEL_BUILD_CLI      "Build CLI executable"        ON)
option(GRAVEL_BUILD_PYTHON   "Build Python bindings"       OFF)
option(GRAVEL_BUILD_TESTS    "Build tests"                 ON)
option(GRAVEL_BUILD_BENCH    "Build benchmarks"            OFF)
option(GRAVEL_USE_OSMIUM     "Build OSM graph support"     OFF)
option(GRAVEL_USE_ARROW      "Build Arrow/Parquet output"  OFF)
option(GRAVEL_ENABLE_LTO     "Enable link-time optimization" OFF)
option(GRAVEL_ENABLE_PGO     "Enable profile-guided optimization (set GRAVEL_PGO_DIR)" OFF)

# PGO/LTO build flags
if(GRAVEL_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported)
    if(lto_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    else()
        message(WARNING "LTO requested but not supported by this compiler")
    endif()
endif()

if(GRAVEL_ENABLE_PGO)
    if(NOT GRAVEL_PGO_DIR)
        set(GRAVEL_PGO_DIR "${CMAKE_BINARY_DIR}/pgo_profiles")
    endif()
    if(GRAVEL_PGO_GENERATE)
        add_compile_options(-fprofile-generate=${GRAVEL_PGO_DIR})
        add_link_options(-fprofile-generate=${GRAVEL_PGO_DIR})
    else()
        add_compile_options(-fprofile-use=${GRAVEL_PGO_DIR} -fprofile-correction)
        add_link_options(-fprofile-use=${GRAVEL_PGO_DIR})
    endif()
endif()

include(cmake/Dependencies.cmake)

# ===========================================================================
# Sub-library targets — strict dependency DAG:
#   gravel-core → (nothing)
#   gravel-ch → gravel-core
#   gravel-simplify → gravel-core, gravel-ch
#   gravel-fragility → gravel-core, gravel-ch, gravel-simplify
#   gravel-geo → gravel-core, gravel-simplify
#   gravel-us → gravel-geo  (placeholder, no sources yet)
#   gravel → INTERFACE linking all of the above
# ===========================================================================

find_package(OpenMP QUIET)

# --- gravel-core: graph representation, basic algorithms, utilities ---
add_library(gravel-core
    src/core/array_graph.cpp
    src/core/csv_graph.cpp
    src/core/graph_serialization.cpp
    src/core/hilbert_order.cpp
    src/core/subgraph.cpp
    src/core/priority_queue.cpp
    src/core/four_heap.cpp
    src/core/dijkstra.cpp
    src/core/lca.cpp
    src/core/geo_math.cpp
    src/core/edge_labels.cpp
    src/core/edge_sampler.cpp
    src/core/incremental_sssp.cpp
    src/io/mapped_file.cpp
)
target_include_directories(gravel-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
if(OpenMP_CXX_FOUND)
    target_link_libraries(gravel-core PUBLIC OpenMP::OpenMP_CXX)
endif()

# --- gravel-ch: contraction hierarchies, blocked queries, shortcuts ---
add_library(gravel-ch
    src/ch/contraction.cpp
    src/ch/ch_query.cpp
    src/ch/ch_serialization.cpp
    src/ch/shortcut_index.cpp
    src/ch/blocked_ch_query.cpp
    src/ch/landmarks.cpp
)
target_link_libraries(gravel-ch PUBLIC gravel-core)

# --- gravel-simplify: graph simplification, bridge detection ---
add_library(gravel-simplify
    src/simplify/simplify.cpp
    src/simplify/degree2.cpp
    src/simplify/ch_pruning.cpp
    src/simplify/degradation.cpp
    src/simplify/bridges.cpp
    src/simplify/bridge_classification.cpp
    src/simplify/reduced_graph.cpp
)
target_link_libraries(gravel-simplify PUBLIC gravel-core gravel-ch)

# --- gravel-fragility: all fragility/analysis, spectral metrics, io output ---
add_library(gravel-fragility
    src/fragility/route_fragility.cpp
    src/fragility/hershberger_suri.cpp
    src/fragility/via_path.cpp
    src/fragility/bernstein_approx.cpp
    src/fragility/fragility_filter.cpp
    src/analysis/laplacian.cpp
    src/analysis/algebraic_connectivity.cpp
    src/analysis/betweenness.cpp
    src/analysis/kirchhoff.cpp
    src/analysis/natural_connectivity.cpp
    src/analysis/accessibility.cpp
    src/analysis/county_fragility.cpp
    src/analysis/od_sampling.cpp
    src/analysis/location_fragility.cpp
    src/analysis/closure_risk.cpp
    src/analysis/scenario_fragility.cpp
    src/analysis/uncertainty.cpp
    src/analysis/tiled_fragility.cpp
    src/analysis/progressive_fragility.cpp
    src/analysis/progressive_fragility_mc.cpp
    src/analysis/progressive_fragility_greedy.cpp
    src/analysis/analysis_context.cpp
    src/fragility/inter_region_fragility.cpp
    src/validation/synthetic_graphs.cpp
    src/validation/validator.cpp
    src/validation/fragility_validator.cpp
    src/io/geojson_output.cpp
    src/io/arrow_output.cpp
)
target_link_libraries(gravel-fragility PUBLIC
    gravel-core gravel-ch gravel-simplify
    Eigen3::Eigen nlohmann_json::nlohmann_json
)
target_include_directories(gravel-fragility PUBLIC
    $<BUILD_INTERFACE:${spectra_SOURCE_DIR}/include>
)
if(GRAVEL_USE_ARROW)
    target_compile_definitions(gravel-fragility PUBLIC GRAVEL_HAS_ARROW)
    target_link_libraries(gravel-fragility PUBLIC Arrow::arrow_shared Parquet::parquet_shared)
endif()

# --- gravel-geo: OSM loading, snapping, elevation, edge confidence ---
add_library(gravel-geo
    src/geo/elevation.cpp
    src/geo/edge_confidence.cpp
    src/geo/region_assignment.cpp
    src/geo/geojson_loader.cpp
    src/geo/boundary_nodes.cpp
    src/geo/border_edges.cpp
    src/geo/graph_coarsening.cpp
    src/geo/region_serialization.cpp
    src/geo/geography_skeleton.cpp
    src/snap/edge_index.cpp
    src/snap/snapper.cpp
)
target_link_libraries(gravel-geo PUBLIC gravel-core gravel-simplify nlohmann_json::nlohmann_json)

if(GRAVEL_USE_OSMIUM)
    target_sources(gravel-geo PRIVATE src/geo/osm_graph.cpp)

    find_package(Osmium QUIET)
    if(NOT Osmium_FOUND)
        find_path(OSMIUM_INCLUDE_DIR osmium/osm.hpp
                  PATHS /opt/homebrew/include /usr/local/include /usr/include)
        if(OSMIUM_INCLUDE_DIR)
            target_include_directories(gravel-geo PUBLIC ${OSMIUM_INCLUDE_DIR})
        else()
            message(FATAL_ERROR "libosmium not found. Install with: brew install libosmium")
        endif()
    else()
        target_include_directories(gravel-geo PUBLIC ${OSMIUM_INCLUDE_DIRS})
    endif()
    target_compile_definitions(gravel-geo PUBLIC GRAVEL_HAS_OSMIUM)

    find_package(ZLIB REQUIRED)
    find_package(BZip2 QUIET)
    find_package(EXPAT QUIET)
    target_link_libraries(gravel-geo PUBLIC ZLIB::ZLIB)
    if(BZip2_FOUND)
        target_link_libraries(gravel-geo PUBLIC BZip2::BZip2)
    endif()
    if(EXPAT_FOUND)
        target_link_libraries(gravel-geo PUBLIC ${EXPAT_LIBRARIES})
    endif()
    find_library(LZ4_LIBRARY lz4 PATHS /opt/homebrew/lib /usr/local/lib)
    if(LZ4_LIBRARY)
        target_link_libraries(gravel-geo PUBLIC ${LZ4_LIBRARY})
    endif()
endif()

# --- gravel-us: US-specific specializations (placeholder) ---
add_library(gravel-us INTERFACE)
target_link_libraries(gravel-us INTERFACE gravel-geo)

# --- gravel: convenience target linking all sub-libraries ---
add_library(gravel INTERFACE)
target_link_libraries(gravel INTERFACE
    gravel-core gravel-ch gravel-simplify gravel-fragility gravel-geo gravel-us
)

if(GRAVEL_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(GRAVEL_BUILD_PYTHON)
    add_subdirectory(python)
endif()

if(GRAVEL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(GRAVEL_BUILD_BENCH)
    add_subdirectory(bench)
endif()
