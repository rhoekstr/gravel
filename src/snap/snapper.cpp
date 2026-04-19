#include "gravel/snap/snapper.h"
#include "gravel/core/geo_math.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace gravel {

static constexpr double EXACT_NODE_THRESHOLD = 0.01;  // t within 1% of endpoint

static EdgeIndex build_index_checked(const ArrayGraph& graph) {
    if (graph.raw_coords().empty()) {
        throw std::runtime_error("Snapper requires a graph with coordinates");
    }
    return EdgeIndex::build(graph.raw_coords(), graph.raw_offsets(), graph.raw_targets());
}

Snapper::Snapper(const ArrayGraph& graph)
    : graph_(graph),
      index_(build_index_checked(graph)) {
}

SnapResult Snapper::snap(Coord point, double max_distance_m) const {
    auto candidates = index_.query_nearest(point, 16);

    SnapResult best;
    double best_dist = max_distance_m;

    for (const auto& cand : candidates) {
        // Early termination: if bbox lower bound exceeds current best, skip rest
        if (cand.min_dist_m > best_dist) break;

        NodeID u = cand.edge.source;
        NodeID v = cand.edge.target;
        auto coord_u = graph_.node_coordinate(u);
        auto coord_v = graph_.node_coordinate(v);
        if (!coord_u || !coord_v) continue;

        auto proj = project_to_segment(point, *coord_u, *coord_v);

        if (proj.distance_m < best_dist) {
            best_dist = proj.distance_m;

            // Get edge weight from graph
            auto targets = graph_.outgoing_targets(u);
            auto weights = graph_.outgoing_weights(u);
            Weight edge_weight = 0.0;
            for (uint32_t i = 0; i < targets.size(); ++i) {
                if (targets[i] == v) {
                    edge_weight = weights[i];
                    break;
                }
            }

            best.edge_source = u;
            best.edge_target = v;
            best.t = proj.t;
            best.snapped_coord = proj.projected;
            best.edge_weight = edge_weight;
            best.dist_to_source = proj.t * edge_weight;
            best.dist_to_target = (1.0 - proj.t) * edge_weight;
            best.snap_distance_m = proj.distance_m;
            best.is_exact_node = (proj.t < EXACT_NODE_THRESHOLD ||
                                   proj.t > (1.0 - EXACT_NODE_THRESHOLD));
        }
    }

    return best;
}

std::vector<SnapResult> Snapper::snap_batch(const std::vector<Coord>& points,
                                             double max_distance_m) const {
    int n = static_cast<int>(points.size());
    std::vector<SnapResult> results(n);

    #pragma omp parallel for schedule(dynamic) if(n > 16)
    for (int i = 0; i < n; ++i) {
        results[i] = snap(points[i], max_distance_m);
    }

    return results;
}

SnapQualityReport snap_quality(const std::vector<SnapResult>& results,
                                double warn_threshold_m) {
    SnapQualityReport report;
    report.total = static_cast<uint32_t>(results.size());

    std::vector<double> distances;
    distances.reserve(results.size());

    for (const auto& r : results) {
        if (r.valid()) {
            report.succeeded++;
            distances.push_back(r.snap_distance_m);
            if (r.is_exact_node) report.exact_node++;
            if (r.snap_distance_m > warn_threshold_m) report.warned++;
        } else {
            report.failed++;
        }
    }

    if (!distances.empty()) {
        std::sort(distances.begin(), distances.end());
        size_t n = distances.size();
        report.p50_distance_m = distances[n / 2];
        report.p90_distance_m = distances[std::min(n - 1, n * 9 / 10)];
        report.p95_distance_m = distances[std::min(n - 1, n * 95 / 100)];
        report.p99_distance_m = distances[std::min(n - 1, n * 99 / 100)];
        report.max_distance_m = distances.back();
    }

    return report;
}

}  // namespace gravel
