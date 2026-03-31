#pragma once
#include "gravel/core/array_graph.h"
#include <string>
#include <vector>

namespace gravel {

struct ElevationData {
    std::vector<double> node_elevation;  // meters, one per node (NaN if unknown)

    // Max elevation along an edge (max of endpoints).
    double edge_max_elevation(NodeID u, NodeID v) const;

    // Valid if we have elevation for this node.
    bool has_elevation(NodeID node) const;
};

// Load SRTM HGT tile(s) and interpolate elevation per graph node.
// srtm_dir: directory containing .hgt files (e.g., N35W084.hgt)
// The graph must have node coordinates.
ElevationData load_srtm_elevation(const ArrayGraph& graph, const std::string& srtm_dir);

// Assign elevation from a flat array (for testing or external data sources).
ElevationData elevation_from_array(std::vector<double> elevations);

// Serialize/deserialize elevation data
void save_elevation(const ElevationData& elev, const std::string& path);
ElevationData load_elevation(const std::string& path);

}  // namespace gravel
