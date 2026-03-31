#pragma once
#include "gravel/core/types.h"
#include "gravel/core/array_graph.h"
#include "gravel/snap/edge_index.h"
#include <stdexcept>
#include <vector>

namespace gravel {

struct SnapResult {
    NodeID edge_source = INVALID_NODE;
    NodeID edge_target = INVALID_NODE;
    double t = 0.0;                    // [0,1] along edge
    Coord snapped_coord = {};
    Weight dist_to_source = 0.0;       // t * edge_weight
    Weight dist_to_target = 0.0;       // (1-t) * edge_weight
    double snap_distance_m = 0.0;      // perpendicular distance in meters
    bool is_exact_node = false;        // t ≈ 0 or t ≈ 1
    Weight edge_weight = 0.0;          // full edge weight

    bool valid() const { return edge_source != INVALID_NODE; }
};

struct SnapQualityReport {
    uint32_t total = 0;
    uint32_t succeeded = 0;
    uint32_t failed = 0;           // no edge within radius
    uint32_t exact_node = 0;       // snapped to existing node
    uint32_t warned = 0;           // snap_distance > warn_threshold

    double p50_distance_m = 0.0;
    double p90_distance_m = 0.0;
    double p95_distance_m = 0.0;
    double p99_distance_m = 0.0;
    double max_distance_m = 0.0;
};

// Generate a snap quality report from a batch of snap results.
SnapQualityReport snap_quality(const std::vector<SnapResult>& results,
                                double warn_threshold_m = 200.0);

class Snapper {
public:
    // Build snapper with spatial index over all edges.
    // Graph must have coordinates.
    explicit Snapper(const ArrayGraph& graph);

    // Snap a coordinate to the nearest edge.
    // Returns invalid SnapResult if no edge within max_distance_m.
    SnapResult snap(Coord point, double max_distance_m = 500.0) const;

    // Batch snap multiple coordinates.
    std::vector<SnapResult> snap_batch(const std::vector<Coord>& points,
                                        double max_distance_m = 500.0) const;

private:
    const ArrayGraph& graph_;
    EdgeIndex index_;
};

}  // namespace gravel
