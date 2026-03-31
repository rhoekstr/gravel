#include "gravel/geo/osm_graph.h"
#include "gravel/core/geo_math.h"
#include <stdexcept>

#ifdef GRAVEL_HAS_OSMIUM

#include <osmium/io/any_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/index/map/flex_mem.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>

#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace gravel {

namespace {

// Wrapper for backward compatibility with existing call sites
double haversine(double lat1, double lon1, double lat2, double lon2) {
    return haversine_meters({lat1, lon1}, {lat2, lon2});
}

using index_type = osmium::index::map::FlexMem<osmium::unsigned_object_id_type,
                                                 osmium::Location>;
using location_handler_type = osmium::handler::NodeLocationsForWays<index_type>;

struct WayCollector : public osmium::handler::Handler {
    const std::unordered_map<std::string, double>& speed_profile;
    bool bidirectional;

    struct RawEdge {
        int64_t from_osm_id;
        int64_t to_osm_id;
        double distance_m;
        double speed_kmh;
        std::unordered_map<std::string, std::string> tags;
    };

    std::vector<RawEdge> edges;
    std::unordered_map<int64_t, Coord> node_coords;
    std::unordered_set<int64_t> used_nodes;

    WayCollector(const std::unordered_map<std::string, double>& sp, bool bidir)
        : speed_profile(sp), bidirectional(bidir) {}

    void way(const osmium::Way& way) {
        const char* highway = way.tags()["highway"];
        if (!highway) return;

        auto it = speed_profile.find(highway);
        if (it == speed_profile.end()) return;
        double speed = it->second;

        bool oneway = false;
        const char* oneway_tag = way.tags()["oneway"];
        if (oneway_tag) {
            std::string ow(oneway_tag);
            oneway = (ow == "yes" || ow == "1" || ow == "true");
        }
        // Motorways are typically one-way
        std::string hw(highway);
        if (hw == "motorway" || hw == "motorway_link") oneway = true;

        const auto& nodes = way.nodes();
        for (size_t i = 0; i + 1 < nodes.size(); ++i) {
            int64_t from_id = nodes[i].ref();
            int64_t to_id = nodes[i + 1].ref();

            auto from_loc = nodes[i].location();
            auto to_loc = nodes[i + 1].location();

            if (!from_loc.valid() || !to_loc.valid()) continue;

            double dist = haversine(from_loc.lat(), from_loc.lon(),
                                    to_loc.lat(), to_loc.lon());

            // Capture useful OSM tags for this way
            std::unordered_map<std::string, std::string> edge_tags;
            edge_tags["highway"] = hw;
            for (const char* key : {"name", "surface", "bridge", "tunnel",
                                     "maxspeed", "lanes", "ref"}) {
                const char* val = way.tags()[key];
                if (val) edge_tags[key] = val;
            }

            edges.push_back({from_id, to_id, dist, speed, edge_tags});
            used_nodes.insert(from_id);
            used_nodes.insert(to_id);

            node_coords[from_id] = {from_loc.lat(), from_loc.lon()};
            node_coords[to_id] = {to_loc.lat(), to_loc.lon()};

            if (!oneway && bidirectional) {
                edges.push_back({to_id, from_id, dist, speed, edge_tags});
            }
        }
    }
};

}  // namespace

std::unique_ptr<ArrayGraph> load_osm_graph(const OSMConfig& config) {
    osmium::io::Reader reader(config.pbf_path);

    index_type index;
    location_handler_type location_handler(index);

    WayCollector collector(config.speed_profile, config.bidirectional);
    osmium::apply(reader, location_handler, collector);
    reader.close();

    // Map OSM node IDs to dense [0, N) range
    std::unordered_map<int64_t, NodeID> id_map;
    NodeID next_id = 0;
    for (int64_t osm_id : collector.used_nodes) {
        id_map[osm_id] = next_id++;
    }

    NodeID num_nodes = next_id;
    std::vector<Edge> gravel_edges;
    gravel_edges.reserve(collector.edges.size());

    for (const auto& e : collector.edges) {
        NodeID from = id_map[e.from_osm_id];
        NodeID to = id_map[e.to_osm_id];
        // Weight = travel time in seconds
        double time_sec = e.distance_m / (e.speed_kmh / 3.6);
        gravel_edges.push_back({from, to, time_sec, e.distance_m});
    }

    // Build coordinates array
    std::vector<Coord> coords(num_nodes);
    for (const auto& [osm_id, coord] : collector.node_coords) {
        coords[id_map[osm_id]] = coord;
    }

    // Build CSR arrays manually to include coordinates
    std::vector<uint32_t> offsets(num_nodes + 1, 0);
    for (const auto& e : gravel_edges) offsets[e.source + 1]++;
    for (NodeID i = 1; i <= num_nodes; ++i) offsets[i] += offsets[i - 1];

    std::vector<NodeID> targets(gravel_edges.size());
    std::vector<Weight> weights(gravel_edges.size());
    auto pos = offsets;
    for (const auto& e : gravel_edges) {
        uint32_t idx = pos[e.source]++;
        targets[idx] = e.target;
        weights[idx] = e.weight;
    }

    return std::make_unique<ArrayGraph>(
        std::move(offsets), std::move(targets), std::move(weights), std::move(coords));
}

OSMLoadResult load_osm_graph_with_labels(const OSMConfig& config) {
    osmium::io::Reader reader(config.pbf_path);

    index_type index;
    location_handler_type location_handler(index);

    WayCollector collector(config.speed_profile, config.bidirectional);
    osmium::apply(reader, location_handler, collector);
    reader.close();

    // Map OSM node IDs to dense [0, N) range
    std::unordered_map<int64_t, NodeID> id_map;
    NodeID next_id = 0;
    for (int64_t osm_id : collector.used_nodes) {
        id_map[osm_id] = next_id++;
    }

    NodeID num_nodes = next_id;

    // Build edge list
    struct IndexedEdge {
        NodeID source, target;
        Weight weight;
        size_t raw_index;  // index into collector.edges for tag lookup
    };
    std::vector<IndexedEdge> indexed_edges;
    indexed_edges.reserve(collector.edges.size());

    for (size_t i = 0; i < collector.edges.size(); ++i) {
        const auto& e = collector.edges[i];
        NodeID from = id_map[e.from_osm_id];
        NodeID to = id_map[e.to_osm_id];
        double time_sec = e.distance_m / (e.speed_kmh / 3.6);
        indexed_edges.push_back({from, to, time_sec, i});
    }

    // Build coordinates
    std::vector<Coord> coords(num_nodes);
    for (const auto& [osm_id, coord] : collector.node_coords) {
        coords[id_map[osm_id]] = coord;
    }

    // Build CSR with tag index mapping
    std::vector<uint32_t> offsets(num_nodes + 1, 0);
    for (const auto& e : indexed_edges) offsets[e.source + 1]++;
    for (NodeID i = 1; i <= num_nodes; ++i) offsets[i] += offsets[i - 1];

    size_t edge_count = indexed_edges.size();
    std::vector<NodeID> targets(edge_count);
    std::vector<Weight> weights(edge_count);
    std::vector<size_t> raw_indices(edge_count);  // maps CSR pos → raw edge index
    auto pos = offsets;
    for (const auto& e : indexed_edges) {
        uint32_t idx = pos[e.source]++;
        targets[idx] = e.target;
        weights[idx] = e.weight;
        raw_indices[idx] = e.raw_index;
    }

    // Collect all unique tag keys across all edges
    std::unordered_set<std::string> all_keys;
    for (const auto& e : collector.edges) {
        for (const auto& [k, v] : e.tags) all_keys.insert(k);
    }

    // Build EdgeMetadata in CSR order
    EdgeMetadata metadata;
    metadata.tag_keys.assign(all_keys.begin(), all_keys.end());
    std::sort(metadata.tag_keys.begin(), metadata.tag_keys.end());
    metadata.tag_values.resize(metadata.tag_keys.size());
    for (auto& v : metadata.tag_values) v.resize(edge_count);

    for (size_t csr_pos = 0; csr_pos < edge_count; ++csr_pos) {
        const auto& raw_edge = collector.edges[raw_indices[csr_pos]];
        for (size_t ki = 0; ki < metadata.tag_keys.size(); ++ki) {
            auto it = raw_edge.tags.find(metadata.tag_keys[ki]);
            if (it != raw_edge.tags.end()) {
                metadata.tag_values[ki][csr_pos] = it->second;
            }
        }
    }

    OSMLoadResult result;
    result.graph = std::make_unique<ArrayGraph>(
        std::move(offsets), std::move(targets), std::move(weights), std::move(coords));
    result.metadata = std::move(metadata);
    return result;
}

}  // namespace gravel

#else  // !GRAVEL_HAS_OSMIUM

namespace gravel {

std::unique_ptr<ArrayGraph> load_osm_graph(const OSMConfig& /*config*/) {
    throw std::runtime_error(
        "OSM support not compiled. Rebuild with -DGRAVEL_USE_OSMIUM=ON");
}

OSMLoadResult load_osm_graph_with_labels(const OSMConfig& /*config*/) {
    throw std::runtime_error(
        "OSM support not compiled. Rebuild with -DGRAVEL_USE_OSMIUM=ON");
}

}  // namespace gravel

#endif
