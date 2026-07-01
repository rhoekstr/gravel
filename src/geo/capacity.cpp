#include "gravel/geo/capacity.h"

#include <cstddef>
#include <stdexcept>

namespace gravel {

CapacityConfig CapacityConfig::hcm() {
    CapacityConfig c;
    // PCE/hour/lane by road class. Motorways carry the most per lane; service and
    // living streets the least. Link ramps are derated relative to their parent.
    c.per_lane_capacity = {
        {"motorway", 2200.0}, {"motorway_link", 1800.0},
        {"trunk", 2000.0},    {"trunk_link", 1600.0},
        {"primary", 1700.0},  {"primary_link", 1400.0},
        {"secondary", 1400.0},{"secondary_link", 1200.0},
        {"tertiary", 1100.0}, {"tertiary_link", 1000.0},
        {"residential", 800.0}, {"unclassified", 800.0},
        {"service", 400.0},   {"living_street", 300.0},
    };
    // Assumed lane count (per direction) when the `lanes` tag is missing.
    c.default_lanes = {
        {"motorway", 2.0}, {"motorway_link", 1.0},
        {"trunk", 2.0},    {"trunk_link", 1.0},
        {"primary", 2.0},  {"primary_link", 1.0},
        {"secondary", 1.0},{"secondary_link", 1.0},
        {"tertiary", 1.0}, {"tertiary_link", 1.0},
        {"residential", 1.0}, {"unclassified", 1.0},
        {"service", 1.0},  {"living_street", 1.0},
    };
    c.fallback_capacity = 600.0;
    return c;
}

namespace {

// Parse a leading positive lane count from an OSM `lanes` value. OSM lanes are
// usually plain integers ("2") but can be decimals ("1.5") or lists ("1;2"); we
// take the leading number. Returns 0 when unparseable/absent.
double parse_lanes(const std::string& s) {
    if (s.empty()) return 0.0;
    try {
        std::size_t pos = 0;
        double v = std::stod(s, &pos);
        if (v > 0.0 && v < 100.0) return v;
    } catch (const std::exception&) {
        // fall through to 0
    }
    return 0.0;
}

}  // namespace

std::vector<double> estimate_capacity(const EdgeMetadata& metadata,
                                       const CapacityConfig& config) {
    const std::vector<std::string>& highway = metadata.get("highway");
    const std::vector<std::string>& lanes = metadata.get("lanes");
    const std::size_t n = highway.size();  // == edge_count for OSM metadata

    std::vector<double> capacity(n, config.fallback_capacity);

    for (std::size_t e = 0; e < n; ++e) {
        auto cap_it = config.per_lane_capacity.find(highway[e]);
        if (cap_it == config.per_lane_capacity.end()) {
            capacity[e] = config.fallback_capacity;
            continue;
        }
        const double per_lane = cap_it->second;

        double lane_count = (e < lanes.size()) ? parse_lanes(lanes[e]) : 0.0;
        if (lane_count <= 0.0) {
            auto dl = config.default_lanes.find(highway[e]);
            lane_count = (dl != config.default_lanes.end()) ? dl->second : 1.0;
        }
        capacity[e] = per_lane * lane_count;
    }
    return capacity;
}

}  // namespace gravel
