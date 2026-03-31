#pragma once

// Arrow/Parquet output — only available when GRAVEL_HAS_ARROW is defined.
// Gated behind cmake option GRAVEL_USE_ARROW.

#include "gravel/fragility/fragility_result.h"
#include "gravel/analysis/county_fragility.h"
#include "gravel/analysis/location_fragility.h"
#include "gravel/analysis/betweenness.h"
#include <string>
#include <vector>

namespace gravel {

#ifdef GRAVEL_HAS_ARROW

// Write batch fragility results to Parquet.
// Columns: source, target, primary_distance, bottleneck_source, bottleneck_target,
//          bottleneck_ratio, replacement_distance, path_length
void write_fragility_parquet(const std::vector<FragilityResult>& results,
                              const std::vector<std::pair<NodeID, NodeID>>& od_pairs,
                              const std::string& path);

// Write county fragility results to Parquet.
// Columns: composite_index, subgraph_nodes, subgraph_edges, bridges_count,
//          algebraic_connectivity, kirchhoff_index, entry_point_count, accessibility_score
void write_county_fragility_parquet(const std::vector<CountyFragilityResult>& results,
                                      const std::vector<std::string>& labels,
                                      const std::string& path);

// Write betweenness scores to Parquet.
// Columns: edge_index, source, target, betweenness_score
void write_betweenness_parquet(const BetweennessResult& result,
                                const ArrayGraph& graph,
                                const std::string& path);

#endif  // GRAVEL_HAS_ARROW

// JSON-based fallback — always available, no Arrow dependency.
// Write fragility results as JSON Lines (.jsonl).
void write_fragility_jsonl(const std::vector<FragilityResult>& results,
                            const std::vector<std::pair<NodeID, NodeID>>& od_pairs,
                            const std::string& path);

}  // namespace gravel
