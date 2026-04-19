pybind11_add_module(_gravel
    bindings.cpp
)

target_link_libraries(_gravel PRIVATE gravel)

# Install the extension inside the gravel Python package so `import gravel`
# can find _gravel. Uses all target types (MODULE/LIBRARY/RUNTIME) so it
# works on Linux (MODULE), macOS (MODULE), and Windows (RUNTIME).
install(TARGETS _gravel
    LIBRARY DESTINATION gravel
    RUNTIME DESTINATION gravel
    ARCHIVE DESTINATION gravel)
