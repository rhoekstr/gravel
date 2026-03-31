#include "gravel/geo/region_assignment.h"
#include "gravel/core/geo_math.h"
#include "gravel/core/subgraph.h"

namespace gravel {

RegionAssignment assign_nodes_to_regions(
    const ArrayGraph& graph,
    const std::vector<RegionSpec>& regions,
    const AssignmentConfig& /*config*/) {

    RegionAssignment result;
    result.regions = regions;
    uint32_t n = graph.node_count();
    result.region_index.assign(n, -1);
    result.region_node_counts.assign(regions.size(), 0);
    result.unassigned_count = 0;

    const auto& coords = graph.raw_coords();

    for (NodeID node = 0; node < n; ++node) {
        if (node >= coords.size()) {
            result.unassigned_count++;
            continue;
        }

        Coord c = coords[node];
        bool assigned = false;

        for (size_t r = 0; r < regions.size(); ++r) {
            if (point_in_polygon(c, regions[r].boundary.vertices)) {
                result.region_index[node] = static_cast<int32_t>(r);
                result.region_node_counts[r]++;
                assigned = true;
                break;
            }
        }

        if (!assigned) result.unassigned_count++;
    }

    return result;
}

}  // namespace gravel
