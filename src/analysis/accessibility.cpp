#include "gravel/analysis/accessibility.h"
#include "gravel/ch/ch_query.h"
#include "gravel/fragility/route_fragility.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <utility>

namespace gravel {

AccessibilityResult analyze_accessibility(
    const ArrayGraph& full_graph,
    const ContractionResult& full_ch,
    const ShortcutIndex& full_idx,
    const SubgraphResult& subgraph) {

    AccessibilityResult result;
    if (!subgraph.graph || subgraph.graph->node_count() == 0) return result;

    // Step 1: Identify entry points — subgraph nodes with external neighbors
    for (NodeID new_id = 0; new_id < subgraph.graph->node_count(); ++new_id) {
        NodeID orig = subgraph.new_to_original[new_id];
        auto targets = full_graph.outgoing_targets(orig);

        std::vector<NodeID> external;
        for (NodeID t : targets) {
            if (subgraph.original_to_new.find(t) == subgraph.original_to_new.end()) {
                external.push_back(t);
            }
        }

        if (!external.empty()) {
            result.entry_points.push_back({orig, new_id, std::move(external)});
        }
    }

    // Step 2: Compute corridor fragilities between entry point pairs.
    //
    // Performance: route_fragility() computes a BlockedCHQuery for every path edge,
    // which is O(path_length × CH_query). For large subgraphs (>10K nodes), routes
    // between distant entry points can have hundreds of edges, making 10 pairs × 200
    // blocked queries = 2000+ queries. Cap to 5 pairs and use distance-only for
    // scoring when the subgraph is large.

    size_t num_entry = result.entry_points.size();
    if (num_entry < 2) {
        result.accessibility_score = 0.0;
        return result;
    }

    // For large subgraphs, sample entry points and use distance-based scoring
    bool use_fast_mode = (subgraph.graph->node_count() > 5000);
    size_t max_pairs = use_fast_mode ? 5 : 10;
    max_pairs = std::min(max_pairs, num_entry * (num_entry - 1) / 2);

    if (use_fast_mode) {
        // Fast mode: compute CH distance between sampled entry point pairs.
        // Accessibility = connectivity check, not full fragility analysis.
        CHQuery chq(full_ch);
        std::mt19937_64 rng(42);

        // Sample entry point pairs
        std::vector<size_t> ep_indices(num_entry);
        std::iota(ep_indices.begin(), ep_indices.end(), 0);
        std::shuffle(ep_indices.begin(), ep_indices.end(), rng);
        size_t sample_n = std::min(num_entry, static_cast<size_t>(10));

        double total_distance = 0.0;
        uint32_t reachable_pairs = 0;
        uint32_t tested = 0;

        for (size_t i = 0; i < sample_n && tested < max_pairs; ++i) {
            for (size_t j = i + 1; j < sample_n && tested < max_pairs; ++j) {
                NodeID s = result.entry_points[ep_indices[i]].original_id;
                NodeID t = result.entry_points[ep_indices[j]].original_id;
                Weight d = chq.distance(s, t);
                if (d < INF_WEIGHT) {
                    total_distance += d;
                    reachable_pairs++;
                }
                tested++;
            }
        }

        // Accessibility score based on reachability and average distance
        if (tested > 0) {
            double reachable_frac = static_cast<double>(reachable_pairs) / tested;
            result.accessibility_score = reachable_frac;
        }
    } else {
        // Full mode: compute route fragility between pairs (original behavior)
        size_t computed = 0;
        for (size_t i = 0; i < num_entry && computed < max_pairs; ++i) {
            for (size_t j = i + 1; j < num_entry && computed < max_pairs; ++j) {
                NodeID s = result.entry_points[i].original_id;
                NodeID t = result.entry_points[j].original_id;
                auto frag = route_fragility(full_ch, full_idx, full_graph, s, t);
                if (frag.valid()) {
                    result.corridor_fragilities.push_back(std::move(frag));
                    ++computed;
                }
            }
        }

        if (result.corridor_fragilities.empty()) {
            result.accessibility_score = 0.0;
        } else {
            double sum_ratio = 0.0;
            uint32_t finite_count = 0;
            for (const auto& frag : result.corridor_fragilities) {
                double ratio = frag.bottleneck().fragility_ratio;
                if (std::isfinite(ratio)) {
                    sum_ratio += ratio;
                    ++finite_count;
                }
            }
            if (finite_count > 0) {
                double avg = sum_ratio / finite_count;
                result.accessibility_score = 1.0 / (1.0 + avg);
            }
        }
    }

    return result;
}

}  // namespace gravel
