#include "gravel/snap/edge_index.h"
#include "gravel/core/geo_math.h"
#include "gravel/io/binary_format.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <queue>
#include <stdexcept>
#include <utility>
#include <functional>

namespace gravel {

static constexpr int LEAF_SIZE = 128;

EdgeIndex EdgeIndex::build(const std::vector<Coord>& coords,
                           const std::vector<uint32_t>& offsets,
                           const std::vector<NodeID>& targets) {
    EdgeIndex idx;
    idx.coords_ = coords;

    NodeID n = static_cast<NodeID>(offsets.size() - 1);

    // Collect all edges with bounding boxes
    for (NodeID u = 0; u < n; ++u) {
        for (uint32_t i = offsets[u]; i < offsets[u + 1]; ++i) {
            NodeID v = targets[i];
            if (v >= coords.size()) continue;  // skip invalid targets
            const Coord& ca = coords[u];
            const Coord& cb = coords[v];

            idx.entries_.push_back({u, v, i});
            idx.entry_boxes_.push_back({
                std::min(ca.lat, cb.lat), std::max(ca.lat, cb.lat),
                std::min(ca.lon, cb.lon), std::max(ca.lon, cb.lon)
            });
        }
    }

    if (idx.entries_.empty()) return idx;

    // STR bulk loading: sort entries and build tree
    std::vector<uint32_t> indices(idx.entries_.size());
    for (uint32_t i = 0; i < indices.size(); ++i) indices[i] = i;

    // Pre-sort entries by longitude, then latitude within strips (STR ordering)
    {
        // Compute sort keys to avoid lambda captures during sort
        std::vector<float> lon_keys(indices.size());
        std::vector<float> lat_keys(indices.size());
        for (uint32_t i = 0; i < indices.size(); ++i) {
            const auto& b = idx.entry_boxes_[i];
            lon_keys[i] = static_cast<float>((b.min_lon + b.max_lon) * 0.5);
            lat_keys[i] = static_cast<float>((b.min_lat + b.max_lat) * 0.5);
        }

        // Sort by longitude
        std::sort(indices.begin(), indices.end(),
                  [&lon_keys](uint32_t a, uint32_t b) { return lon_keys[a] < lon_keys[b]; });

        // Within longitude strips, sort by latitude
        uint32_t count = static_cast<uint32_t>(indices.size());
        uint32_t num_leaves = (count + LEAF_SIZE - 1) / LEAF_SIZE;
        uint32_t slices = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<double>(num_leaves))));
        uint32_t slice_size = (count + slices - 1) / slices;

        for (uint32_t s = 0; s < slices; ++s) {
            uint32_t s_begin = s * slice_size;
            uint32_t s_end = std::min((s + 1) * slice_size, count);
            if (s_begin >= count) break;
            std::sort(indices.begin() + s_begin, indices.begin() + s_end,
                      [&lat_keys](uint32_t a, uint32_t b) { return lat_keys[a] < lat_keys[b]; });
        }
    }

    // Reorder entries and bboxes to match sorted order
    {
        std::vector<EdgeEntry> sorted_entries(idx.entries_.size());
        std::vector<BBox> sorted_boxes(idx.entry_boxes_.size());
        for (uint32_t i = 0; i < indices.size(); ++i) {
            sorted_entries[i] = idx.entries_[indices[i]];
            sorted_boxes[i] = idx.entry_boxes_[indices[i]];
        }
        idx.entries_ = std::move(sorted_entries);
        idx.entry_boxes_ = std::move(sorted_boxes);
    }

    // Build tree on reordered data (identity indices, no more sorting needed)
    for (uint32_t i = 0; i < indices.size(); ++i) indices[i] = i;
    idx.build_tree(indices, 0, static_cast<uint32_t>(indices.size()), LEAF_SIZE);

    return idx;
}

void EdgeIndex::build_tree(std::vector<uint32_t>& indices, uint32_t begin, uint32_t end,
                           int leaf_size) {
    uint32_t count = end - begin;

    if (count <= static_cast<uint32_t>(leaf_size)) {
        Node node;
        node.is_leaf = true;
        node.first_child = begin;
        node.count = count;
        node.bbox = entry_boxes_[indices[begin]];
        for (uint32_t i = begin + 1; i < end; ++i) {
            const auto& b = entry_boxes_[indices[i]];
            node.bbox.min_lat = std::min(node.bbox.min_lat, b.min_lat);
            node.bbox.max_lat = std::max(node.bbox.max_lat, b.max_lat);
            node.bbox.min_lon = std::min(node.bbox.min_lon, b.min_lon);
            node.bbox.max_lon = std::max(node.bbox.max_lon, b.max_lon);
        }
        nodes_.push_back(node);
        return;
    }

    // Data is pre-sorted by build(). Create leaf nodes directly.
    uint32_t leaves_start = static_cast<uint32_t>(nodes_.size());

    for (uint32_t i = begin; i < end; i += leaf_size) {
        uint32_t group_end = std::min(i + static_cast<uint32_t>(leaf_size), end);

        Node leaf;
        leaf.is_leaf = true;
        leaf.first_child = i;
        leaf.count = group_end - i;
        leaf.bbox = entry_boxes_[indices[i]];
        for (uint32_t j = i + 1; j < group_end; ++j) {
            const auto& b = entry_boxes_[indices[j]];
            leaf.bbox.min_lat = std::min(leaf.bbox.min_lat, b.min_lat);
            leaf.bbox.max_lat = std::max(leaf.bbox.max_lat, b.max_lat);
            leaf.bbox.min_lon = std::min(leaf.bbox.min_lon, b.min_lon);
            leaf.bbox.max_lon = std::max(leaf.bbox.max_lon, b.max_lon);
        }
        nodes_.push_back(leaf);
    }

    uint32_t leaves_end = static_cast<uint32_t>(nodes_.size());
    uint32_t num_leaf_nodes = leaves_end - leaves_start;

    // Build internal levels bottom-up until we have a single root.
    // Each level's nodes are contiguous in nodes_, enabling first_child + i indexing.
    uint32_t level_start = leaves_start;
    uint32_t level_count = num_leaf_nodes;

    while (level_count > 1) {
        uint32_t next_level_start = static_cast<uint32_t>(nodes_.size());

        for (uint32_t i = 0; i < level_count; i += leaf_size) {
            uint32_t chunk = std::min(static_cast<uint32_t>(leaf_size), level_count - i);

            Node internal;
            internal.is_leaf = false;
            internal.first_child = level_start + i;
            internal.count = chunk;
            internal.bbox = nodes_[level_start + i].bbox;
            for (uint32_t j = 1; j < chunk; ++j) {
                const auto& b = nodes_[level_start + i + j].bbox;
                internal.bbox.min_lat = std::min(internal.bbox.min_lat, b.min_lat);
                internal.bbox.max_lat = std::max(internal.bbox.max_lat, b.max_lat);
                internal.bbox.min_lon = std::min(internal.bbox.min_lon, b.min_lon);
                internal.bbox.max_lon = std::max(internal.bbox.max_lon, b.max_lon);
            }
            nodes_.push_back(internal);
        }

        level_start = next_level_start;
        level_count = static_cast<uint32_t>(nodes_.size()) - next_level_start;
    }
}

double EdgeIndex::bbox_min_dist(const BBox& box, Coord point) {
    double clat = std::clamp(point.lat, box.min_lat, box.max_lat);
    double clon = std::clamp(point.lon, box.min_lon, box.max_lon);
    return haversine_meters(point, {clat, clon});
}

std::vector<EdgeIndex::Candidate> EdgeIndex::query_nearest(Coord point, size_t k) const {
    if (nodes_.empty()) return {};

    struct PQEntry {
        double dist;
        uint32_t node_idx;
        bool is_entry;
        uint32_t entry_idx;
        bool operator>(const PQEntry& o) const { return dist > o.dist; }
    };

    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<>> pq;

    // Start from root (last node in the tree)
    uint32_t root = static_cast<uint32_t>(nodes_.size() - 1);
    pq.push({bbox_min_dist(nodes_[root].bbox, point), root, false, 0});

    std::vector<Candidate> results;
    results.reserve(k);

    while (!pq.empty() && results.size() < k) {
        auto top = pq.top();
        pq.pop();

        if (top.is_entry) {
            if (top.entry_idx < entries_.size()) {
                const auto& entry = entries_[top.entry_idx];
                if (entry.source < coords_.size() && entry.target < coords_.size()) {
                    auto proj = project_to_segment(point, coords_[entry.source],
                                                    coords_[entry.target]);
                    results.push_back({entry, proj.distance_m});
                }
            }
            continue;
        }

        if (top.node_idx >= nodes_.size()) continue;
        const auto& node = nodes_[top.node_idx];

        if (node.is_leaf) {
            for (uint32_t i = 0; i < node.count; ++i) {
                uint32_t eidx = node.first_child + i;
                if (eidx < entry_boxes_.size()) {
                    double edist = bbox_min_dist(entry_boxes_[eidx], point);
                    pq.push({edist, 0, true, eidx});
                }
            }
        } else {
            for (uint32_t i = 0; i < node.count; ++i) {
                uint32_t child = node.first_child + i;
                if (child < nodes_.size()) {
                    double cdist = bbox_min_dist(nodes_[child].bbox, point);
                    pq.push({cdist, child, false, 0});
                }
            }
        }
    }

    std::sort(results.begin(), results.end(),
              [](const Candidate& a, const Candidate& b) { return a.min_dist_m < b.min_dist_m; });

    return results;
}

static constexpr char RTREE_MAGIC[4] = {'G', 'R', 'T', 'R'};
static constexpr uint32_t RTREE_VERSION = 1;

void EdgeIndex::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open file for writing: " + path);

    io::write_magic(out, RTREE_MAGIC);
    io::write_u32(out, RTREE_VERSION);
    io::write_vec(out, entries_);
    io::write_vec(out, entry_boxes_);
    io::write_vec(out, coords_);
    io::write_vec(out, nodes_);
}

EdgeIndex EdgeIndex::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open file for reading: " + path);

    io::read_magic(in, RTREE_MAGIC);
    uint32_t version = io::read_u32(in);
    if (version != RTREE_VERSION) {
        throw std::runtime_error("Unsupported rtree version: " + std::to_string(version));
    }

    EdgeIndex idx;
    idx.entries_ = io::read_vec<EdgeEntry>(in);
    idx.entry_boxes_ = io::read_vec<BBox>(in);
    idx.coords_ = io::read_vec<Coord>(in);
    idx.nodes_ = io::read_vec<Node>(in);
    return idx;
}

}  // namespace gravel
