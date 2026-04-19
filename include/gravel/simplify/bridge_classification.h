/// @file bridge_classification.h
/// @brief Distinguish structural bridges from filter-induced bridges.
///
/// When edges are filtered (e.g., keeping only tertiary+ roads), some edges
/// that are NOT bridges in the full graph become bridges in the filtered subgraph.
/// These "filter-induced bridges" are artifacts of the filtering decision, not
/// genuine structural vulnerabilities.
///
/// This matters for research validity: reporting "county X has 12 bridges" when
/// 8 are artifacts of dropping residential roads is misleading. Classifying
/// bridges into structural vs filter-induced lets you report the true count
/// and flag data-quality caveats.
///
/// @section usage Example
/// @code
/// auto labels = EdgeCategoryLabels::from_strings(highway_tags, EdgeCategoryLabels::osm_road_ranks());
/// auto filter = make_category_filter(labels, 4);  // keep tertiary+
/// auto classification = classify_bridges(graph, filter);
/// // classification.structural_count → real bridges
/// // classification.filter_induced_count → artifacts of filtering
/// @endcode

#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/simplify/bridges.h"
#include <functional>
#include <vector>
#include <utility>

namespace gravel {

/// Whether a bridge is structural (exists in the full graph) or only appears
/// because edge filtering removed the alternative paths.
enum class BridgeType : uint8_t {
    STRUCTURAL = 0,     ///< Bridge in both full and filtered graph — real vulnerability
    FILTER_INDUCED = 1  ///< Bridge only in filtered graph — artifact of filtering
};

/// Result of bridge classification: every bridge in the filtered graph is typed.
struct BridgeClassification {
    std::vector<std::pair<NodeID, NodeID>> bridges;  ///< All bridges in filtered graph
    std::vector<BridgeType> types;                    ///< Parallel: classification per bridge
    uint32_t structural_count = 0;
    uint32_t filter_induced_count = 0;
};

/// Classify bridges as structural or filter-induced.
///
/// Runs find_bridges() on both the full graph and a filtered subgraph,
/// then classifies each filtered bridge by checking if it also appears
/// in the full graph's bridge set.
///
/// @param graph       The full (unfiltered) graph.
/// @param edge_filter Predicate: CSR edge index → true to keep. Same predicate
///                    used for graph simplification edge filtering.
BridgeClassification classify_bridges(
    const ArrayGraph& graph,
    const std::function<bool(uint32_t)>& edge_filter);

}  // namespace gravel
