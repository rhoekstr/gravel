#include "gravel/geo/geography_skeleton.h"
#include <vector>

namespace gravel {

ReducedGraph build_reduced_geography_graph(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const RegionAssignment& assignment,
    const BorderEdgeResult& border,
    const ReducedGraphConfig& config) {

    (void)border;  // kept for API compatibility; not needed by the generic algorithm

    // Unwrap RegionAssignment into generic inputs
    std::vector<RegionInfo> regions;
    regions.reserve(assignment.regions.size());
    for (const auto& r : assignment.regions) {
        regions.push_back({r.region_id, r.boundary});
    }

    return build_reduced_graph(graph, ch, assignment.region_index, regions, config);
}

}  // namespace gravel
