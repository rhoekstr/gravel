#include "gravel/core/edge_labels.h"

namespace gravel {

EdgeCategoryLabels EdgeCategoryLabels::from_strings(
    const std::vector<std::string>& labels,
    const std::unordered_map<std::string, uint8_t>& rank_map,
    uint8_t default_rank) {

    EdgeCategoryLabels result;
    result.categories.resize(labels.size(), default_rank);

    for (size_t i = 0; i < labels.size(); ++i) {
        auto it = rank_map.find(labels[i]);
        if (it != rank_map.end()) {
            result.categories[i] = it->second;
        }
    }

    return result;
}

std::unordered_map<std::string, uint8_t> EdgeCategoryLabels::osm_road_ranks() {
    return {
        {"motorway", 0}, {"motorway_link", 0},
        {"trunk", 1}, {"trunk_link", 1},
        {"primary", 2}, {"primary_link", 2},
        {"secondary", 3}, {"secondary_link", 3},
        {"tertiary", 4}, {"tertiary_link", 4},
        {"residential", 5}, {"unclassified", 5},
        {"service", 6}, {"living_street", 6}
    };
}

std::function<bool(uint32_t)> make_category_filter(
    const EdgeCategoryLabels& labels, uint8_t max_category) {

    // Capture a copy of the categories vector for thread safety
    auto cats = std::make_shared<std::vector<uint8_t>>(labels.categories);

    return [cats, max_category](uint32_t edge_index) -> bool {
        if (edge_index >= cats->size()) return true;  // unknown edges are kept
        return (*cats)[edge_index] <= max_category;
    };
}

}  // namespace gravel
