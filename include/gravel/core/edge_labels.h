/// @file edge_labels.h
/// @brief Edge category labels for graph simplification filtering.
///
/// Provides a generic edge labeling system for category-based filtering.
/// OSM road class filtering is the most common specialization, but the
/// system works with any edge categorization scheme.
///
/// @section usage Example
/// @code
/// // OSM road class filtering: keep tertiary and above
/// auto ranks = EdgeCategoryLabels::osm_road_ranks();
/// auto labels = EdgeCategoryLabels::from_strings(highway_tags, ranks);
/// config.edge_filter = make_category_filter(labels, 4);  // tertiary = rank 4
///
/// // Custom categories: keep edges with priority ≤ 3
/// std::vector<uint8_t> my_categories = {1, 2, 5, 3, 1, ...};
/// EdgeCategoryLabels custom{my_categories};
/// config.edge_filter = make_category_filter(custom, 3);
/// @endcode

#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace gravel {

/// Per-edge category labels, indexed by CSR edge position.
/// Lower category number = higher importance.
struct EdgeCategoryLabels {
    std::vector<uint8_t> categories;  // one per directed edge in CSR order

    /// Construct from string labels and a rank mapping.
    /// Labels not found in rank_map get default_rank.
    static EdgeCategoryLabels from_strings(
        const std::vector<std::string>& labels,
        const std::unordered_map<std::string, uint8_t>& rank_map,
        uint8_t default_rank = 255);

    /// Standard OSM road class ranking (lower = more important):
    ///   0: motorway, motorway_link
    ///   1: trunk, trunk_link
    ///   2: primary, primary_link
    ///   3: secondary, secondary_link
    ///   4: tertiary, tertiary_link
    ///   5: residential, unclassified
    ///   6: service, living_street
    static std::unordered_map<std::string, uint8_t> osm_road_ranks();
};

/// Create an edge filter predicate that keeps edges with category ≤ max_category.
/// @param labels        Per-edge category labels.
/// @param max_category  Maximum category to keep (inclusive). E.g., 4 = keep tertiary+.
/// @return              Predicate: edge_index → bool (true = keep).
std::function<bool(uint32_t)> make_category_filter(
    const EdgeCategoryLabels& labels, uint8_t max_category);

}  // namespace gravel
