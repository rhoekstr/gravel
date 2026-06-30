# Vendored Eigen 3.4.0 — minimal CMake config package.
#
# Provides the Eigen3::Eigen imported target pointing at the headers vendored
# alongside this file (../Eigen). This lets find_package(Eigen3) resolve fully
# offline — no gitlab.com clone, and not the Eigen 5.0 that Homebrew/vcpkg now
# ship. Both gravel and the FetchContent'd Spectra consume Eigen via this target.
# See ../VENDORING.md.

if(NOT TARGET Eigen3::Eigen)
    add_library(Eigen3::Eigen INTERFACE IMPORTED)
    set_target_properties(Eigen3::Eigen PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/..")
endif()

set(Eigen3_FOUND TRUE)
set(EIGEN3_FOUND TRUE)
set(Eigen3_VERSION "3.4.0")
set(EIGEN3_VERSION_STRING "3.4.0")
