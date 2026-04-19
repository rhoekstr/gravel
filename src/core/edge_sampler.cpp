#include "gravel/core/edge_sampler.h"
#include "gravel/core/geo_math.h"
#include <algorithm>
#include <cassert>
#include <numeric>
#include <random>
#include <unordered_set>
#include <limits>

namespace gravel {

EdgeSampler::EdgeSampler(const ArrayGraph& graph) : graph_(graph) {}

std::vector<uint32_t> EdgeSampler::sample(const SamplerConfig& config) const {
    // Build candidate pool (all edges or filtered)
    uint32_t total_edges = graph_.edge_count();
    std::vector<uint32_t> pool;
    pool.reserve(total_edges);

    for (uint32_t i = 0; i < total_edges; ++i) {
        if (!config.edge_filter || config.edge_filter(i))
            pool.push_back(i);
    }

    if (pool.empty() || config.target_count == 0) return {};

    switch (config.strategy) {
        case SamplingStrategy::UNIFORM_RANDOM:
            return sample_uniform(config, pool);
        case SamplingStrategy::STRATIFIED_BY_CLASS:
            return sample_stratified(config, pool);
        case SamplingStrategy::IMPORTANCE_WEIGHTED:
            return sample_importance(config, pool);
        case SamplingStrategy::SPATIALLY_STRATIFIED:
            return sample_spatial(config, pool);
        case SamplingStrategy::CLUSTER_DISPERSED:
            return sample_dispersed(config, pool);
    }
    return sample_uniform(config, pool);
}

std::vector<std::pair<NodeID, NodeID>> EdgeSampler::sample_pairs(
    const SamplerConfig& config) const {

    auto indices = sample(config);
    std::vector<std::pair<NodeID, NodeID>> pairs;
    pairs.reserve(indices.size());

    const auto& offsets = graph_.raw_offsets();
    const auto& targets = graph_.raw_targets();

    for (uint32_t idx : indices) {
        // Binary search on offsets to find source node
        auto it = std::upper_bound(offsets.begin(), offsets.end(), idx);
        NodeID src = static_cast<NodeID>(std::distance(offsets.begin(), it) - 1);
        pairs.push_back({src, targets[idx]});
    }
    return pairs;
}

Coord EdgeSampler::edge_midpoint(uint32_t edge_idx) const {
    const auto& coords = graph_.raw_coords();
    if (coords.empty()) return {0.0, 0.0};

    const auto& offsets = graph_.raw_offsets();
    const auto& targets = graph_.raw_targets();

    auto it = std::upper_bound(offsets.begin(), offsets.end(), edge_idx);
    NodeID src = static_cast<NodeID>(std::distance(offsets.begin(), it) - 1);
    NodeID tgt = targets[edge_idx];

    return {(coords[src].lat + coords[tgt].lat) / 2.0,
            (coords[src].lon + coords[tgt].lon) / 2.0};
}

std::vector<uint32_t> EdgeSampler::sample_uniform(
    const SamplerConfig& config,
    const std::vector<uint32_t>& pool) const {

    uint32_t count = std::min(config.target_count, static_cast<uint32_t>(pool.size()));
    auto shuffled = pool;
    std::mt19937_64 rng(config.seed);
    std::shuffle(shuffled.begin(), shuffled.end(), rng);
    shuffled.resize(count);
    return shuffled;
}

std::vector<uint32_t> EdgeSampler::sample_stratified(
    const SamplerConfig& config,
    const std::vector<uint32_t>& pool) const {

    if (!config.labels || config.labels->categories.empty())
        return sample_uniform(config, pool);

    // Group edges by category
    std::unordered_map<uint8_t, std::vector<uint32_t>> by_class;
    for (uint32_t idx : pool) {
        uint8_t cat = (idx < config.labels->categories.size())
                       ? config.labels->categories[idx] : 255;
        by_class[cat].push_back(idx);
    }

    std::mt19937_64 rng(config.seed);
    std::vector<uint32_t> result;
    uint32_t remaining = std::min(config.target_count, static_cast<uint32_t>(pool.size()));

    if (config.proportional) {
        // Sample proportional to class frequency
        for (auto& [cat, edges] : by_class) {
            uint32_t quota = static_cast<uint32_t>(
                static_cast<double>(edges.size()) / pool.size() * remaining + 0.5);
            quota = std::min(quota, static_cast<uint32_t>(edges.size()));
            std::shuffle(edges.begin(), edges.end(), rng);
            result.insert(result.end(), edges.begin(), edges.begin() + quota);
        }
    } else {
        // Equal count per class
        uint32_t per_class = remaining / static_cast<uint32_t>(by_class.size());
        for (auto& [cat, edges] : by_class) {
            uint32_t quota = std::min(per_class, static_cast<uint32_t>(edges.size()));
            std::shuffle(edges.begin(), edges.end(), rng);
            result.insert(result.end(), edges.begin(), edges.begin() + quota);
        }
    }

    // If rounding left us short, fill from uniform
    if (result.size() < remaining) {
        std::unordered_set<uint32_t> selected(result.begin(), result.end());
        auto leftover = pool;
        std::shuffle(leftover.begin(), leftover.end(), rng);
        for (uint32_t idx : leftover) {
            if (result.size() >= remaining) break;
            if (!selected.count(idx)) result.push_back(idx);
        }
    }

    return result;
}

std::vector<uint32_t> EdgeSampler::sample_importance(
    const SamplerConfig& config,
    const std::vector<uint32_t>& pool) const {

    if (config.weights.empty())
        return sample_uniform(config, pool);

    // Build cumulative weight distribution over pool
    std::vector<double> cum_weights;
    cum_weights.reserve(pool.size());
    double total = 0.0;
    for (uint32_t idx : pool) {
        double w = (idx < config.weights.size()) ? std::max(config.weights[idx], 0.0) : 0.0;
        total += w;
        cum_weights.push_back(total);
    }

    if (total <= 0.0)
        return sample_uniform(config, pool);

    std::mt19937_64 rng(config.seed);
    std::uniform_real_distribution<double> dist(0.0, total);

    uint32_t count = std::min(config.target_count, static_cast<uint32_t>(pool.size()));
    std::unordered_set<uint32_t> selected;
    std::vector<uint32_t> result;
    result.reserve(count);

    uint32_t attempts = 0;
    while (result.size() < count && attempts < count * 10) {
        double r = dist(rng);
        auto it = std::lower_bound(cum_weights.begin(), cum_weights.end(), r);
        size_t pos = std::distance(cum_weights.begin(), it);
        if (pos >= pool.size()) pos = pool.size() - 1;
        if (selected.insert(pool[pos]).second)
            result.push_back(pool[pos]);
        ++attempts;
    }

    return result;
}

std::vector<uint32_t> EdgeSampler::sample_spatial(
    const SamplerConfig& config,
    const std::vector<uint32_t>& pool) const {

    const auto& coords = graph_.raw_coords();
    if (coords.empty() || config.grid_rows == 0 || config.grid_cols == 0)
        return sample_uniform(config, pool);

    // Find bounding box of edge midpoints
    double min_lat = 90, max_lat = -90, min_lon = 180, max_lon = -180;
    for (uint32_t idx : pool) {
        auto mid = edge_midpoint(idx);
        min_lat = std::min(min_lat, mid.lat);
        max_lat = std::max(max_lat, mid.lat);
        min_lon = std::min(min_lon, mid.lon);
        max_lon = std::max(max_lon, mid.lon);
    }

    double lat_step = (max_lat - min_lat) / config.grid_rows;
    double lon_step = (max_lon - min_lon) / config.grid_cols;
    if (lat_step <= 0) lat_step = 1.0;
    if (lon_step <= 0) lon_step = 1.0;

    uint32_t total_cells = config.grid_rows * config.grid_cols;

    // Assign edges to grid cells
    std::vector<std::vector<uint32_t>> cells(total_cells);
    for (uint32_t idx : pool) {
        auto mid = edge_midpoint(idx);
        uint32_t r = std::min(static_cast<uint32_t>((mid.lat - min_lat) / lat_step),
                              config.grid_rows - 1);
        uint32_t c = std::min(static_cast<uint32_t>((mid.lon - min_lon) / lon_step),
                              config.grid_cols - 1);
        cells[r * config.grid_cols + c].push_back(idx);
    }

    // Sample target_count / non-empty-cells per cell
    uint32_t non_empty = 0;
    for (const auto& cell : cells) if (!cell.empty()) ++non_empty;
    if (non_empty == 0) return {};

    uint32_t per_cell = std::max(1u, config.target_count / non_empty);
    std::mt19937_64 rng(config.seed);

    std::vector<uint32_t> result;
    result.reserve(config.target_count);

    for (auto& cell : cells) {
        if (cell.empty()) continue;
        std::shuffle(cell.begin(), cell.end(), rng);
        uint32_t take = std::min(per_cell, static_cast<uint32_t>(cell.size()));
        result.insert(result.end(), cell.begin(), cell.begin() + take);
    }

    // Trim to target if over
    if (result.size() > config.target_count) {
        std::shuffle(result.begin(), result.end(), rng);
        result.resize(config.target_count);
    }

    return result;
}

std::vector<uint32_t> EdgeSampler::sample_dispersed(
    const SamplerConfig& config,
    const std::vector<uint32_t>& pool) const {

    const auto& coords = graph_.raw_coords();
    if (coords.empty())
        return sample_uniform(config, pool);

    uint32_t count = std::min(config.target_count, static_cast<uint32_t>(pool.size()));

    // Precompute midpoints
    std::vector<Coord> midpoints(pool.size());
    for (size_t i = 0; i < pool.size(); ++i)
        midpoints[i] = edge_midpoint(pool[i]);

    // Greedy farthest-point selection
    std::mt19937_64 rng(config.seed);
    std::uniform_int_distribution<size_t> first_dist(0, pool.size() - 1);

    std::vector<uint32_t> result;
    result.reserve(count);
    std::vector<bool> selected(pool.size(), false);

    // First edge: random
    size_t first = first_dist(rng);
    result.push_back(pool[first]);
    selected[first] = true;

    // min_dist[i] = minimum distance from pool[i] to any selected edge
    std::vector<double> min_dist(pool.size(), std::numeric_limits<double>::max());
    for (size_t i = 0; i < pool.size(); ++i) {
        if (!selected[i])
            min_dist[i] = haversine_meters(midpoints[first], midpoints[i]);
    }

    while (result.size() < count) {
        // Find candidate with maximum minimum distance
        size_t best = 0;
        double best_dist = -1.0;
        for (size_t i = 0; i < pool.size(); ++i) {
            if (!selected[i] && min_dist[i] > best_dist) {
                best_dist = min_dist[i];
                best = i;
            }
        }

        if (best_dist < config.min_spacing_m && result.size() > 0) break;

        result.push_back(pool[best]);
        selected[best] = true;

        // Update min distances
        for (size_t i = 0; i < pool.size(); ++i) {
            if (!selected[i]) {
                double d = haversine_meters(midpoints[best], midpoints[i]);
                min_dist[i] = std::min(min_dist[i], d);
            }
        }
    }

    return result;
}

}  // namespace gravel
