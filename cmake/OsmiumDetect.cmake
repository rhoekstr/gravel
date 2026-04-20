# OsmiumDetect.cmake
#
# Normalize GRAVEL_USE_OSMIUM (AUTO / ON / OFF — plus boolean back-compat for
# pre-2.2.2 users who passed -DGRAVEL_USE_OSMIUM=TRUE/FALSE/1/0) and decide
# whether OSM support is built in. Emits informative status messages for all
# three outcomes so source-build users never have to guess what they got.
#
# Sets:
#   GRAVEL_OSMIUM_ENABLED       bool — gate the osm_graph source files on this
#   GRAVEL_OSMIUM_INCLUDE_DIRS  path(s) to libosmium headers (only if enabled)
#   GRAVEL_OSMIUM_VERSION       version string for status messages

# ---- normalize the user-facing value ---------------------------------------

if(NOT DEFINED GRAVEL_USE_OSMIUM OR GRAVEL_USE_OSMIUM STREQUAL "")
    set(_gravel_osmium_mode "AUTO")
elseif(GRAVEL_USE_OSMIUM STREQUAL "AUTO")
    set(_gravel_osmium_mode "AUTO")
elseif(GRAVEL_USE_OSMIUM STREQUAL "ON"  OR
       GRAVEL_USE_OSMIUM STREQUAL "TRUE"  OR
       GRAVEL_USE_OSMIUM STREQUAL "YES"   OR
       GRAVEL_USE_OSMIUM EQUAL 1)
    set(_gravel_osmium_mode "ON")
else()  # OFF / FALSE / NO / 0 / anything else falsy
    set(_gravel_osmium_mode "OFF")
endif()

# ---- detect ---------------------------------------------------------------

set(GRAVEL_OSMIUM_ENABLED OFF)
set(GRAVEL_OSMIUM_VERSION "")
set(GRAVEL_OSMIUM_INCLUDE_DIRS "")
set(_gravel_osmium_status_verb "disabled")
set(_gravel_osmium_status_reason "")
set(_gravel_osmium_status_hint "")

function(_gravel_find_osmium_headers out_var out_version)
    # libosmium is header-only; some distributions ship an OsmiumConfig.cmake
    # and some don't. Do a single find_path against common install roots
    # (including the active CMAKE_PREFIX_PATH, which the vcpkg toolchain
    # populates). This avoids the 20+ minute slowdown we saw when
    # find_package(Osmium CONFIG) on Windows CI with many vcpkg packages
    # triggered an apparently expensive vcpkg-wide config scan.
    find_path(GRAVEL_OSMIUM_INCLUDE_DIR osmium/osm.hpp
        HINTS
            ${CMAKE_PREFIX_PATH}
        PATHS
            /opt/homebrew/include
            /usr/local/include
            /usr/include
    )
    if(GRAVEL_OSMIUM_INCLUDE_DIR)
        # Parse version from the header — libosmium/version.hpp defines
        # LIBOSMIUM_VERSION_STRING.
        if(EXISTS "${GRAVEL_OSMIUM_INCLUDE_DIR}/osmium/version.hpp")
            file(STRINGS "${GRAVEL_OSMIUM_INCLUDE_DIR}/osmium/version.hpp" _ver_line
                 REGEX "LIBOSMIUM_VERSION_STRING")
            string(REGEX MATCH "\"([0-9.]+)\"" _m "${_ver_line}")
            set(${out_version} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        endif()
        set(${out_var} "${GRAVEL_OSMIUM_INCLUDE_DIR}" PARENT_SCOPE)
    endif()
endfunction()

if(_gravel_osmium_mode STREQUAL "OFF")
    set(_gravel_osmium_status_reason "GRAVEL_USE_OSMIUM=OFF (explicit)")
else()
    # Both AUTO and ON do the search; they differ only in what happens if
    # libosmium isn't found.
    _gravel_find_osmium_headers(_found_includes _found_version)
    if(_found_includes)
        set(GRAVEL_OSMIUM_ENABLED ON)
        set(GRAVEL_OSMIUM_INCLUDE_DIRS "${_found_includes}")
        set(GRAVEL_OSMIUM_VERSION "${_found_version}")
        if(_gravel_osmium_mode STREQUAL "ON")
            set(_gravel_osmium_status_verb "enabled")
            set(_gravel_osmium_status_reason "required via GRAVEL_USE_OSMIUM=ON")
        else()
            set(_gravel_osmium_status_verb "enabled")
            set(_gravel_osmium_status_reason "auto-detected")
        endif()
    else()
        if(_gravel_osmium_mode STREQUAL "ON")
            # Hard failure: caller explicitly required OSM but we can't build it.
            message(FATAL_ERROR
                "\n  gravel: OSM support was required (GRAVEL_USE_OSMIUM=ON)\n"
                "          but libosmium >=2.20 was not found on the system.\n"
                "\n  Install libosmium:\n"
                "    macOS:   brew install libosmium protozero\n"
                "    Debian:  sudo apt install libosmium2-dev\n"
                "    conda:   conda install -c conda-forge libosmium\n"
                "    vcpkg:   vcpkg install libosmium protozero\n"
                "\n  Or pass -DGRAVEL_USE_OSMIUM=AUTO to build without OSM gracefully."
            )
        endif()
        set(_gravel_osmium_status_verb "disabled")
        set(_gravel_osmium_status_reason "libosmium not found and GRAVEL_USE_OSMIUM=AUTO")
        set(_gravel_osmium_status_hint
            "Install libosmium to enable OSM loaders (load_osm_graph, OSMConfig, SpeedProfile). "
            "See CMake status output below for platform-specific commands.")
    endif()
endif()

# ---- emit status ----------------------------------------------------------

message(STATUS "")
message(STATUS "gravel: OSM support: ${_gravel_osmium_status_verb}")
if(_gravel_osmium_status_reason)
    message(STATUS "        reason: ${_gravel_osmium_status_reason}")
endif()
if(GRAVEL_OSMIUM_ENABLED)
    message(STATUS "        libosmium: ${GRAVEL_OSMIUM_INCLUDE_DIRS} (${GRAVEL_OSMIUM_VERSION})")
    message(STATUS "        Python bindings will expose: load_osm_graph, OSMConfig,")
    message(STATUS "                                      SpeedProfile, load_osm_graph_with_labels")
elseif(_gravel_osmium_mode STREQUAL "AUTO")
    message(STATUS "        To enable OSM loaders, install libosmium and rebuild:")
    message(STATUS "          macOS:   brew install libosmium protozero")
    message(STATUS "          Debian:  sudo apt install libosmium2-dev")
    message(STATUS "          conda:   conda install -c conda-forge libosmium")
    message(STATUS "          vcpkg:   vcpkg install libosmium protozero")
    message(STATUS "        Or pass -DGRAVEL_USE_OSMIUM=ON to error on missing libosmium.")
endif()
message(STATUS "")
