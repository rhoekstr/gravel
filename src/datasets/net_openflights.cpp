#include "gravel/datasets/net_openflights.h"

#include "gravel/core/array_graph.h"
#include "gravel/core/geo_math.h"
#include "gravel/core/types.h"

#include <cstdint>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gravel {

namespace {

/// Parse one RFC-4180 line into fields. Honors double-quoted fields (which may
/// contain commas and doubled `""` escapes) — airport names in OpenFlights do.
/// Unquoted fields are taken verbatim, so the literal NULL token `\N` survives
/// as the two characters backslash-N for the caller to recognize.
std::vector<std::string> parse_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (in_quotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field.push_back('"');  // escaped quote
                    ++i;
                } else {
                    in_quotes = false;
                }
            } else {
                field.push_back(c);
            }
        } else {
            if (c == '"') {
                in_quotes = true;
            } else if (c == ',') {
                fields.push_back(std::move(field));
                field.clear();
            } else if (c == '\r') {
                // ignore CR (tolerate CRLF line endings)
            } else {
                field.push_back(c);
            }
        }
    }
    fields.push_back(std::move(field));
    return fields;
}

/// True when a field is the OpenFlights NULL token or empty.
bool is_null(const std::string& s) { return s.empty() || s == "\\N"; }

}  // namespace

NetworkGraph load_openflights_network(const std::string& airports_path,
                                      const std::string& routes_path,
                                      bool collapse_parallel,
                                      bool drop_codeshare,
                                      std::vector<std::string>* node_iata) {
    std::ifstream airports_in(airports_path);
    if (!airports_in) {
        throw std::runtime_error("Cannot open OpenFlights airports file: " + airports_path);
    }

    // Pass 1: airports.dat → dense node ids + coordinates, plus lookup indices.
    // airports.dat columns (1-indexed): 1=AirportID 5=IATA 7=Latitude 8=Longitude.
    std::vector<Coord> coords;
    std::vector<std::string> node_iata_codes;              // node → IATA ("" if none), for the T-100 overlay
    std::unordered_map<std::string, NodeID> id_to_node;    // OpenFlights Airport ID → node
    std::unordered_map<std::string, NodeID> iata_to_node;  // IATA (non-null) → node

    std::string line;
    while (std::getline(airports_in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> f = parse_csv_line(line);
        if (f.size() < 8) continue;  // malformed / truncated row

        const std::string& airport_id = f[0];
        if (is_null(airport_id)) continue;

        double lat = 0.0;
        double lon = 0.0;
        try {
            lat = std::stod(f[6]);
            lon = std::stod(f[7]);
        } catch (const std::exception&) {
            continue;  // unparseable coordinates → skip the airport
        }

        NodeID node = static_cast<NodeID>(coords.size());
        // First occurrence of an Airport ID wins; duplicates are ignored.
        if (id_to_node.emplace(airport_id, node).second) {
            coords.push_back({lat, lon});
            const std::string& iata = f[4];
            node_iata_codes.push_back(is_null(iata) ? std::string() : iata);
            if (!is_null(iata)) {
                iata_to_node.emplace(iata, node);  // first-seen IATA wins
            }
        }
    }

    const NodeID num_nodes = static_cast<NodeID>(coords.size());

    // Pass 2: routes.dat → directed edges resolved to node ids.
    // routes.dat columns (1-indexed): 3=SrcIATA 4=SrcAirportID 5=DstIATA
    //                                 6=DstAirportID 7=Codeshare.
    std::ifstream routes_in(routes_path);
    if (!routes_in) {
        throw std::runtime_error("Cannot open OpenFlights routes file: " + routes_path);
    }

    // Resolve an airport reference to a node id: prefer the OpenFlights Airport ID
    // foreign key, fall back to the IATA string index. Returns INVALID_NODE if
    // neither resolves.
    auto resolve = [&](const std::string& id_field,
                       const std::string& iata_field) -> NodeID {
        if (!is_null(id_field)) {
            auto it = id_to_node.find(id_field);
            if (it != id_to_node.end()) return it->second;
        }
        if (!is_null(iata_field)) {
            auto it = iata_to_node.find(iata_field);
            if (it != iata_to_node.end()) return it->second;
        }
        return INVALID_NODE;
    };

    std::vector<std::pair<NodeID, NodeID>> raw_edges;
    std::unordered_set<uint64_t> seen;  // (src<<32 | dst) for parallel collapse

    while (std::getline(routes_in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> f = parse_csv_line(line);
        if (f.size() < 6) continue;

        if (drop_codeshare && f.size() >= 7 && f[6] == "Y") continue;

        NodeID src = resolve(f[3], f[2]);
        NodeID dst = resolve(f[5], f[4]);
        if (src == INVALID_NODE || dst == INVALID_NODE) continue;
        if (src == dst) continue;  // self-loop (bad data) → drop

        if (collapse_parallel) {
            uint64_t key = (static_cast<uint64_t>(src) << 32) | dst;
            if (!seen.insert(key).second) continue;
        }
        raw_edges.emplace_back(src, dst);
    }

    // Build CSR arrays directly so we can carry node coordinates. Weight is the
    // great-circle distance in metres between the two airports.
    const std::size_t edge_count = raw_edges.size();
    std::vector<uint32_t> offsets(static_cast<std::size_t>(num_nodes) + 1, 0);
    for (const auto& [src, dst] : raw_edges) {
        (void)dst;
        offsets[static_cast<std::size_t>(src) + 1]++;
    }
    for (NodeID i = 1; i <= num_nodes; ++i) offsets[i] += offsets[i - 1];

    std::vector<NodeID> targets(edge_count);
    std::vector<Weight> weights(edge_count);
    std::vector<uint32_t> pos = offsets;
    for (const auto& [src, dst] : raw_edges) {
        uint32_t idx = pos[src]++;
        targets[idx] = dst;
        weights[idx] = haversine_meters(coords[src], coords[dst]);
    }

    if (node_iata) *node_iata = std::move(node_iata_codes);

    NetworkGraph result;
    result.graph = std::make_unique<ArrayGraph>(
        std::move(offsets), std::move(targets), std::move(weights), std::move(coords));
    // capacity intentionally left empty: OpenFlights carries no per-edge capacity.
    return result;
}

}  // namespace gravel
