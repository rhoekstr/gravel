pybind11_add_module(_gravel
    bindings.cpp
)

target_link_libraries(_gravel PRIVATE gravel)

# For local dev builds (cmake --build build), place _gravel.so next to the
# Python source package so `PYTHONPATH=python python3 -c "import gravel"`
# works. scikit-build-core ignores this for wheel builds — it uses install()
# to place the .so in the wheel's gravel/ directory.
set_target_properties(_gravel PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/python/gravel)

# Install into the gravel package directory so `import gravel` finds
# _gravel via the package-relative import (from ._gravel import ...).
install(TARGETS _gravel
    LIBRARY DESTINATION gravel
    RUNTIME DESTINATION gravel
    ARCHIVE DESTINATION gravel)
