/// @file edge_sampler.h
/// @brief General-purpose edge sampling strategies for graph analysis.
///
/// Provides unified sampling primitives used by progressive fragility,
/// betweenness approximation, and border edge characterization.

#pragma once

#include "gravel/core/array_graph.h"
#include "gravel/core/edge_labels.h"
#include <functional>
#include <vector>

namespace gravel {

enum class SamplingStrategy {
    UNIFORM_RANDOM,        ///< Baseline: no structure assumed
    STRATIFIED_BY_CLASS,   ///< Proportional representation per road class
    IMPORTANCE_WEIGHTED,   ///< Sample proportional to a weight vector
    SPATIALLY_STRATIFIED,  ///< Grid-based geographic coverage
    CLUSTER_DISPERSED,     ///< Sampled edges are spatially spread out
};

struct SamplerConfig {
    SamplingStrategy strategy = SamplingStrategy::UNIFORM_RANDOM;

    uint32_t target_count = 100;  ///< Number of edges to sample

    // IMPORTANCE_WEIGHTED: one weight per edge in CSR order.
    // Empty = fall back to UNIFORM_RANDOM.
    std::vector<double> weights;

    // STRATIFIED_BY_CLASS
    const EdgeCategoryLabels* labels = nullptr;
    bool proportional = true;  ///< true = proportional to class frequency

    // SPATIALLY_STRATIFIED
    uint32_t grid_rows = 10;
    uint32_t grid_cols = 10;

    // CLUSTER_DISPERSED
    double min_spacing_m = 500.0;  ///< Minimum distance between sampled edges (meters)

    /// Optional: only consider edges where predicate returns true.
    std::function<bool(uint32_t)> edge_filter = nullptr;

    uint64_t seed = 42;
};

/// General-purpose edge sampler for ArrayGraph.
/// Constructed once per graph; each sample() call is independent and thread-safe.
class EdgeSampler {
public:
    explicit EdgeSampler(const ArrayGraph& graph);

    /// Sample edges, returning CSR edge indices.
    std::vector<uint32_t> sample(const SamplerConfig& config) const;

    /// Sample edges as (source, target) node pairs.
    std::vector<std::pair<NodeID, NodeID>> sample_pairs(const SamplerConfig& config) const;

private:
    const ArrayGraph& graph_;

    std::vector<uint32_t> sample_uniform(const SamplerConfig& config,
                                          const std::vector<uint32_t>& pool) const;
    std::vector<uint32_t> sample_stratified(const SamplerConfig& config,
                                             const std::vector<uint32_t>& pool) const;
    std::vector<uint32_t> sample_importance(const SamplerConfig& config,
                                             const std::vector<uint32_t>& pool) const;
    std::vector<uint32_t> sample_spatial(const SamplerConfig& config,
                                          const std::vector<uint32_t>& pool) const;
    std::vector<uint32_t> sample_dispersed(const SamplerConfig& config,
                                            const std::vector<uint32_t>& pool) const;

    /// Get midpoint coordinates for an edge (for spatial strategies).
    Coord edge_midpoint(uint32_t edge_idx) const;
};

}  // namespace gravel
