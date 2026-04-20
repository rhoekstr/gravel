include(FetchContent)

# Dependency resolution strategy:
#
#   Each FetchContent_Declare below carries a FIND_PACKAGE_ARGS clause
#   (CMake 3.24+). FetchContent_MakeAvailable() will first try
#   find_package() — and if that finds a system / conda-forge / vcpkg
#   install, it uses that instead of git-cloning.
#
#   This matters for three build contexts:
#
#   1. Developer builds and PyPI wheel builds: no system deps are
#      installed, so find_package fails and FetchContent falls back to
#      the git clone. Exactly the pre-3.24 behavior. Unchanged.
#
#   2. conda-forge builds: the host environment provides eigen,
#      nlohmann_json, spectra-cpp, pybind11. find_package succeeds, no
#      network is touched at build time. Required because conda-forge
#      blocks network access in its build sandbox.
#
#   3. Windows CI and distro-package builds: system packages (from
#      vcpkg, apt, brew, etc.) are used when present. On Windows this
#      sidesteps a systemic FetchContent hang we've observed on
#      windows-latest GHA runners where git-cloning Eigen stalls
#      indefinitely.

# nlohmann/json — header only
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
    FIND_PACKAGE_ARGS 3.11 NAMES nlohmann_json
)
FetchContent_MakeAvailable(json)

# Catch2 for tests
if(GRAVEL_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.2
        GIT_SHALLOW    TRUE
        FIND_PACKAGE_ARGS 3.5 NAMES Catch2
    )
    FetchContent_MakeAvailable(Catch2)
    # Catch's CMake helpers live under extras/ when vendored, but are
    # installed as a CMake module when the system package is used.
    # include(Catch) works in either case.
    if(DEFINED Catch2_SOURCE_DIR)
        list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)
    endif()
endif()

# Google Benchmark for performance benchmarks
if(GRAVEL_BUILD_BENCH)
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.8.3
        GIT_SHALLOW    TRUE
        FIND_PACKAGE_ARGS NAMES benchmark
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
endif()

# Eigen 3.4 — header-only linear algebra (required for spectral analysis)
FetchContent_Declare(Eigen3
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG        3.4.0
    GIT_SHALLOW    TRUE
    FIND_PACKAGE_ARGS 3.4 NAMES Eigen3
)
set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Eigen3)

# Spectra 1.0 — header-only Lanczos eigenvalue solver (depends on Eigen).
#
# No FIND_PACKAGE_ARGS here. Every other dep uses CMake targets
# (Eigen3::Eigen, nlohmann_json::nlohmann_json, etc.) which work under
# both find_package and FetchContent code paths. Spectra does NOT ship
# a proper imported target from its find_package config; we consume it
# via a raw include path at ${spectra_SOURCE_DIR}/include in the main
# CMakeLists. FIND_PACKAGE_ARGS causes find_package to take over and
# leave spectra_SOURCE_DIR unset, producing -I/include in the compile
# command and a 'Spectra/SymEigsSolver.h not found' error.
#
# The git clone is cheap (small repo) and reliable. If conda-forge
# needs find_package(Spectra) to succeed (to avoid network at build
# time), we'll add a recipe patch to convert this target to use
# find_package + the conda-forge-provided target.
FetchContent_Declare(spectra
    GIT_REPOSITORY https://github.com/yixuan/spectra.git
    GIT_TAG        v1.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(spectra)

# Apache Arrow / Parquet (optional)
if(GRAVEL_USE_ARROW)
    find_package(Arrow REQUIRED)
    find_package(Parquet REQUIRED)
endif()

# pybind11
#
# No FIND_PACKAGE_ARGS here either. scikit-build-core pip-installs
# pybind11 into an isolated build env when we build a wheel, and
# exposes its CMake dir. If find_package(pybind11) takes over via
# FIND_PACKAGE_ARGS, pybind11's CMake config injects LTO flags that
# get mangled by the outer build toolchain on macOS Python 3.10-3.13
# wheel builds (observed: "unsupported argument '' to option '-flto='").
# FetchContent git-clone of pybind11's small repo is fast and reliable
# on every platform we ship to.
if(GRAVEL_BUILD_PYTHON)
    FetchContent_Declare(pybind11
        GIT_REPOSITORY https://github.com/pybind/pybind11.git
        GIT_TAG        v2.12.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(pybind11)
endif()
