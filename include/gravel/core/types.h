#pragma once
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace gravel {

using NodeID = uint32_t;
using EdgeID = uint32_t;
using Weight = double;
using Level  = uint32_t;

constexpr NodeID INVALID_NODE = std::numeric_limits<NodeID>::max();
constexpr Weight INF_WEIGHT   = std::numeric_limits<Weight>::infinity();

struct Coord {
    double lat = 0.0;
    double lon = 0.0;
};

struct Edge {
    NodeID source;
    NodeID target;
    Weight weight;
    Weight secondary_weight = 0.0;
    std::string label;
};

// Compact edge for CSR storage — no source (implicit from offset array)
struct CompactEdge {
    NodeID target;
    Weight weight;
};

}  // namespace gravel
