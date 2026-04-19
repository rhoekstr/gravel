#include "gravel/core/csv_graph.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace gravel {

std::unique_ptr<ArrayGraph> load_csv_graph(const CSVConfig& config) {
    std::ifstream in(config.path);
    if (!in) throw std::runtime_error("Cannot open CSV file: " + config.path);

    // Parse header to find column indices
    std::string line;
    int src_idx = -1, tgt_idx = -1, wgt_idx = -1;

    if (config.has_header) {
        if (!std::getline(in, line)) {
            throw std::runtime_error("Empty CSV file");
        }
        std::stringstream ss(line);
        std::string col;
        int idx = 0;
        while (std::getline(ss, col, config.delimiter)) {
            // Trim whitespace
            while (!col.empty() && col.front() == ' ') col.erase(col.begin());
            while (!col.empty() && col.back() == ' ') col.pop_back();

            if (col == config.source_col) src_idx = idx;
            else if (col == config.target_col) tgt_idx = idx;
            else if (col == config.weight_col) wgt_idx = idx;
            ++idx;
        }
        if (src_idx < 0) throw std::runtime_error("Source column not found: " + config.source_col);
        if (tgt_idx < 0) throw std::runtime_error("Target column not found: " + config.target_col);
        if (wgt_idx < 0) throw std::runtime_error("Weight column not found: " + config.weight_col);
    } else {
        // Default: columns 0, 1, 2
        src_idx = 0;
        tgt_idx = 1;
        wgt_idx = 2;
    }

    // Read edges and map string/int node IDs to dense IDs
    std::unordered_map<std::string, NodeID> node_map;
    std::vector<Edge> edges;

    auto get_or_create = [&](const std::string& id) -> NodeID {
        auto it = node_map.find(id);
        if (it != node_map.end()) return it->second;
        NodeID new_id = static_cast<NodeID>(node_map.size());
        node_map[id] = new_id;
        return new_id;
    };

    while (std::getline(in, line)) {
        if (line.empty()) continue;

        std::stringstream ss(line);
        std::string field;
        std::vector<std::string> fields;
        while (std::getline(ss, field, config.delimiter)) {
            while (!field.empty() && field.front() == ' ') field.erase(field.begin());
            while (!field.empty() && field.back() == ' ') field.pop_back();
            fields.push_back(field);
        }

        int max_idx = std::max({src_idx, tgt_idx, wgt_idx});
        if (static_cast<int>(fields.size()) <= max_idx) continue;

        NodeID src = get_or_create(fields[src_idx]);
        NodeID tgt = get_or_create(fields[tgt_idx]);
        Weight w = std::stod(fields[wgt_idx]);

        edges.push_back({src, tgt, w});
        if (config.bidirectional) {
            edges.push_back({tgt, src, w});
        }
    }

    NodeID num_nodes = static_cast<NodeID>(node_map.size());
    return std::make_unique<ArrayGraph>(num_nodes, std::move(edges));
}

}  // namespace gravel
