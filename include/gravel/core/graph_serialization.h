#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/io/mapped_file.h"
#include <memory>
#include <string>

namespace gravel {

// Serialize an ArrayGraph to .gravel.meta format
void save_graph(const ArrayGraph& graph, const std::string& path);

// Load an ArrayGraph from .gravel.meta format (copies data into memory)
std::unique_ptr<ArrayGraph> load_graph(const std::string& path);

// Load an ArrayGraph via mmap (copies data into ArrayGraph vectors, but avoids
// double-buffering through iostream). The MappedFile must outlive the parse call
// but data is copied into the ArrayGraph, so it can be released after.
std::unique_ptr<ArrayGraph> load_graph_mmap(const std::string& path);

}  // namespace gravel
