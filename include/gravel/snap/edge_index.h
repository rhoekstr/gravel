#pragma once
#include "gravel/core/types.h"
#include <string>
#include <vector>

namespace gravel {

// Lightweight STR-packed R-tree over edge bounding boxes for nearest-edge queries.
// Bulk-loaded once, queried many times. No dynamic insertions.
class EdgeIndex {
public:
    struct EdgeEntry {
        NodeID source;
        NodeID target;
        uint32_t edge_offset;  // global index into CSR arrays
    };

    struct Candidate {
        EdgeEntry edge;
        double min_dist_m;  // lower bound distance from bounding box
    };

    // Build from graph data. coords must have entries for all nodes.
    static EdgeIndex build(const std::vector<Coord>& coords,
                           const std::vector<uint32_t>& offsets,
                           const std::vector<NodeID>& targets);

    // Find k nearest candidate edges to a query point.
    // Returns candidates sorted by bounding-box lower bound distance.
    std::vector<Candidate> query_nearest(Coord point, size_t k = 16) const;

    size_t size() const { return entries_.size(); }

    // Serialize to .gravel.rtree format
    void save(const std::string& path) const;

    // Load from .gravel.rtree format
    static EdgeIndex load(const std::string& path);

private:
    struct BBox {
        double min_lat, max_lat, min_lon, max_lon;
    };

    struct Node {
        BBox bbox;
        uint32_t first_child;  // index into nodes_ (internal) or entries_ (leaf)
        uint32_t count;        // number of children
        bool is_leaf;
    };

    std::vector<Node> nodes_;
    std::vector<EdgeEntry> entries_;
    std::vector<BBox> entry_boxes_;
    std::vector<Coord> coords_;   // copy of node coordinates for distance computation

    void build_tree(std::vector<uint32_t>& indices, uint32_t begin, uint32_t end,
                    int leaf_size);
    static double bbox_min_dist(const BBox& box, Coord point);
};

}  // namespace gravel
