#pragma once
#include "gravel/core/array_graph.h"
#include "gravel/ch/contraction.h"
#include <vector>
#include <utility>

namespace gravel {

struct SamplingConfig {
    uint32_t total_samples = 1000;
    uint32_t distance_strata = 10;       // stratify by distance decile
    double long_distance_weight = 3.0;   // over-sample long-distance pairs
    double long_distance_threshold = 0.0; // auto-detected if 0
    uint64_t seed = 42;

    /// Optional per-node importance weights for non-uniform sampling.
    /// When non-empty (must have graph.node_count() entries), nodes are sampled
    /// proportionally to their weight. Higher weight = more likely to be selected
    /// as origin or destination.
    ///
    /// Why: Uniform node sampling over-represents dead-end residential streets
    /// relative to their actual human significance. Population-weighted sampling
    /// (using Census block centroids or facility locations) measures fragility
    /// for the routes that actually matter to people — hospitals, shelters,
    /// emergency operations centers, highway interchanges.
    std::vector<double> node_weights;
};

// Generate stratified O-D pairs for network-level fragility analysis.
// Stratifies by distance to ensure coverage of short/local and long/cross-region routes.
std::vector<std::pair<NodeID, NodeID>> stratified_sample(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    SamplingConfig config = {});

}  // namespace gravel
