# OpenMPDetect.cmake — OpenMP detection with an Apple Clang / Homebrew libomp fallback.
#
# Plain `find_package(OpenMP)` silently fails on Apple Clang, which ships no OpenMP
# runtime. The result was that every `#pragma omp` in Gravel became a no-op on macOS
# and the parallel kernels (fragility, betweenness, distance matrices) ran serial
# *without any warning*. This module fixes that: on Apple it locates Homebrew's
# `libomp` and constructs a working `OpenMP::OpenMP_CXX` target, and it always emits a
# loud status message so a serial build is never silent.
#
# On success, sets OpenMP_CXX_FOUND=TRUE and provides the OpenMP::OpenMP_CXX target.

include(CheckCXXSourceCompiles)

find_package(OpenMP QUIET)

if(NOT OpenMP_CXX_FOUND AND APPLE)
    execute_process(
        COMMAND brew --prefix libomp
        OUTPUT_VARIABLE _gravel_libomp_prefix
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_gravel_libomp_prefix AND EXISTS "${_gravel_libomp_prefix}/include/omp.h")
        # Retry find_package with hints so it can build a validated target.
        set(OpenMP_CXX_FLAGS "-Xclang -fopenmp -I${_gravel_libomp_prefix}/include")
        set(OpenMP_CXX_LIB_NAMES "omp")
        set(OpenMP_omp_LIBRARY "${_gravel_libomp_prefix}/lib/libomp.dylib")
        find_package(OpenMP QUIET)
        if(NOT OpenMP_CXX_FOUND)
            # FindOpenMP's compile check is flaky with `-Xclang -fopenmp`, so do our own
            # compile+LINK test before committing to a hand-built target. Building an
            # executable (not a -undefined dynamic_lookup module) means undefined symbols
            # are hard link errors — which ALSO guards against an arch mismatch: Homebrew's
            # libomp is host-arch only, so the x86_64 macOS wheel cross-built on an arm64
            # runner fails this link and degrades to SERIAL, instead of producing a wheel
            # that dies at import with a missing __kmpc_* symbol. arm64 macOS, Linux, and
            # Windows keep OpenMP.
            set(CMAKE_REQUIRED_FLAGS "-Xclang -fopenmp -I${_gravel_libomp_prefix}/include")
            set(CMAKE_REQUIRED_LIBRARIES "${_gravel_libomp_prefix}/lib/libomp.dylib")
            check_cxx_source_compiles(
                "#include <omp.h>\nint main() { return omp_get_max_threads() > 0 ? 0 : 1; }"
                GRAVEL_LIBOMP_LINKS)
            unset(CMAKE_REQUIRED_FLAGS)
            unset(CMAKE_REQUIRED_LIBRARIES)
            if(GRAVEL_LIBOMP_LINKS)
                add_library(OpenMP::OpenMP_CXX INTERFACE IMPORTED)
                set_target_properties(OpenMP::OpenMP_CXX PROPERTIES
                    INTERFACE_COMPILE_OPTIONS "-Xclang;-fopenmp"
                    INTERFACE_INCLUDE_DIRECTORIES "${_gravel_libomp_prefix}/include"
                    INTERFACE_LINK_LIBRARIES "${_gravel_libomp_prefix}/lib/libomp.dylib")
                set(OpenMP_CXX_FOUND TRUE)
            endif()
        endif()
        if(OpenMP_CXX_FOUND)
            set(_gravel_openmp_source " (Homebrew libomp: ${_gravel_libomp_prefix})")
        endif()
    endif()
endif()

if(OpenMP_CXX_FOUND)
    message(STATUS "Gravel: OpenMP ENABLED${_gravel_openmp_source} — parallel kernels active.")
else()
    message(STATUS "Gravel: OpenMP NOT FOUND — building SERIAL. Parallel kernels "
                   "(fragility, betweenness, distance matrices) will run single-threaded.")
    if(APPLE)
        message(STATUS "        macOS: brew install libomp   (then re-run cmake)")
    elseif(UNIX)
        message(STATUS "        Linux: install a libgomp-capable compiler (gcc) or libomp-dev.")
    endif()
    message(STATUS "        Check at runtime with gravel.HAS_OPENMP (Python) / GRAVEL_HAS_OPENMP (C++).")
endif()
