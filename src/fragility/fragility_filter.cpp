#include "gravel/fragility/fragility_filter.h"
#include "gravel/simplify/bridges.h"
#include "gravel/fragility/route_fragility.h"
#include "gravel/ch/ch_query.h"
#include "gravel/ch/blocked_ch_query.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <limits>

namespace gravel {

FilteredFragilityResult filtered_route_fragility(
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const ArrayGraph& graph,
    const LandmarkData& landmarks,
    NodeID source, NodeID target,
    FilterConfig config) {

    FilteredFragilityResult result;

    // Step 1: Get primary path via CH query
    CHQuery chq(ch);
    auto primary = chq.route(source, target);
    if (primary.distance >= INF_WEIGHT || primary.path.size() < 2) {
        return result;
    }

    result.primary_distance = primary.distance;
    result.primary_path = primary.path;

    // Step 2: Bridge detection — bridges get INF fragility immediately
    auto bridge_result = find_bridges(graph);
    std::unordered_set<uint64_t> bridge_set;
    for (const auto& [u, v] : bridge_result.bridges) {
        bridge_set.insert(ContractionResult::pack_edge(u, v));
        bridge_set.insert(ContractionResult::pack_edge(v, u));
    }

    // Determine CH level threshold for screening
    uint32_t level_threshold = 0;
    if (config.use_ch_level_filter) {
        std::vector<Level> sorted_levels(ch.node_levels.begin(), ch.node_levels.end());
        std::sort(sorted_levels.begin(), sorted_levels.end());
        size_t idx_thresh = static_cast<size_t>(config.ch_level_percentile * sorted_levels.size());
        idx_thresh = std::min(idx_thresh, sorted_levels.size() - 1);
        level_threshold = sorted_levels[idx_thresh];
    }

    // Step 3: Process each path edge through filter pipeline
    BlockedCHQuery blocked(ch, idx, graph);

    for (size_t i = 0; i + 1 < result.primary_path.size(); ++i) {
        NodeID u = result.primary_path[i];
        NodeID v = result.primary_path[i + 1];

        EdgeFragility ef;
        ef.source = u;
        ef.target = v;

        // Filter 1: Bridge check
        uint64_t edge_key = ContractionResult::pack_edge(u, v);
        if (bridge_set.count(edge_key)) {
            ef.replacement_distance = INF_WEIGHT;
            ef.fragility_ratio = std::numeric_limits<double>::infinity();
            result.edge_fragilities.push_back(ef);
            result.edges_screened++;
            continue;
        }

        // Filter 2: CH level screening
        if (config.use_ch_level_filter) {
            if (ch.node_levels[u] >= level_threshold && ch.node_levels[v] >= level_threshold) {
                // Both endpoints are high-level — unlikely to be critical
                // Use primary distance as replacement (ratio ≈ 1.0)
                ef.replacement_distance = result.primary_distance;
                ef.fragility_ratio = 1.0;
                result.edge_fragilities.push_back(ef);
                result.edges_screened++;
                continue;
            }
        }

        // Filter 3: ALT lower bound check
        if (config.use_alt_filter && landmarks.num_landmarks > 0) {
            Weight lb = landmarks.lower_bound(source, target);
            if (lb > 0 && result.primary_distance / lb < config.skip_ratio_threshold) {
                // The lower bound is close to the actual distance — removing any single
                // edge is unlikely to cause a significant detour
                ef.replacement_distance = result.primary_distance;
                ef.fragility_ratio = 1.0;
                result.edge_fragilities.push_back(ef);
                result.edges_screened++;
                continue;
            }
        }

        // Filter 4: Exact blocked CH query (survivors only)
        Weight blocked_dist = blocked.distance_blocking(source, target, {{u, v}});
        ef.replacement_distance = blocked_dist;
        ef.fragility_ratio = (blocked_dist < INF_WEIGHT) ?
                             blocked_dist / result.primary_distance :
                             std::numeric_limits<double>::infinity();
        result.edge_fragilities.push_back(ef);
        result.edges_computed++;
    }

    return result;
}

}  // namespace gravel
