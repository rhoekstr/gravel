/// @file edge_confidence.h
/// @brief Per-edge data confidence scoring.
///
/// OSM data quality varies: a road tagged in 2008 with no name is less reliable
/// than a well-mapped motorway updated recently. This module assigns a [0,1]
/// confidence score to each edge based on available metadata.
///
/// Why this matters for research: if a bridge finding rests on a low-confidence
/// edge, the finding itself is uncertain. Propagating confidence through fragility
/// analysis lets you report "this county has 5 high-confidence bridges and 3
/// low-confidence bridges" rather than just "8 bridges."
///
/// The confidence score is a heuristic, not ground truth. It captures observable
/// data quality signals: named roads are more likely correct, major roads get
/// more editing attention, roads with surface/lane tags have been actively
/// maintained in OSM.

#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/geo/osm_graph.h"
#include <vector>

namespace gravel {

/// Per-edge confidence scores [0.0, 1.0] as a parallel overlay on the CSR.
/// Higher = more confident the edge data is correct.
struct EdgeConfidence {
    std::vector<double> scores;  ///< One per directed edge in CSR order

    /// Weight penalty multiplier for low-confidence edges.
    /// confidence 1.0 → multiplier 1.0 (no penalty)
    /// confidence 0.5 → multiplier 1.5 (50% longer travel time assumed)
    /// confidence 0.0 → multiplier 2.0 (double travel time — maximum uncertainty)
    double weight_multiplier(uint32_t edge_index) const {
        if (edge_index >= scores.size()) return 1.0;
        return 2.0 - scores[edge_index];
    }
};

/// Estimate edge confidence from OSM metadata.
///
/// Scoring heuristic (additive, clamped to [0, 1]):
///   +0.20 base (every edge gets some credit for existing in OSM)
///   +0.25 if road class ≤ secondary (major roads get more editing attention)
///   +0.20 if the edge has a name tag
///   +0.15 if the edge has a surface tag (active maintenance signal)
///   +0.10 if the edge has lane count info
///   +0.10 if the edge has a maxspeed tag
///
/// @param graph    The graph (for edge count)
/// @param metadata Edge metadata from load_osm_graph_with_labels()
EdgeConfidence estimate_osm_confidence(
    const ArrayGraph& graph,
    const EdgeMetadata& metadata);

/// Create from a flat array (for non-OSM data or external confidence sources).
EdgeConfidence confidence_from_array(std::vector<double> values);

}  // namespace gravel
