pybind11_add_module(_gravel
    bindings.cpp
)

target_link_libraries(_gravel PRIVATE gravel)

# When building via scikit-build-core (wheel / editable install), install the
# extension inside the gravel Python package so `import gravel` finds it.
if(SKBUILD)
    install(TARGETS _gravel LIBRARY DESTINATION gravel)
endif()
