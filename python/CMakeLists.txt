pybind11_add_module(_gravel
    bindings.cpp
)

target_link_libraries(_gravel PRIVATE gravel)
