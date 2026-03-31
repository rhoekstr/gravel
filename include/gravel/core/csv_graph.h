#pragma once
#include "gravel/core/array_graph.h"
#include <memory>
#include <string>

namespace gravel {

struct CSVConfig {
    std::string path;
    char delimiter = ',';
    bool has_header = true;
    std::string source_col = "source";
    std::string target_col = "target";
    std::string weight_col = "weight";
    std::string secondary_weight_col;  // optional
    std::string label_col;             // optional
    bool bidirectional = false;        // add reverse edges automatically
};

std::unique_ptr<ArrayGraph> load_csv_graph(const CSVConfig& config);

}  // namespace gravel
