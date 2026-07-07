#include "gravel/datasets/net_caida.h"

#include "gravel/core/array_graph.h"
#include "gravel/core/types.h"

#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace gravel {

namespace {

/// Registry mapping CAIDA string node ids (e.g. "N33382") to dense [0, N) ids,
/// preserving first-seen order.
struct NodeRegistry {
    std::unordered_map<std::string, NodeID> index;

    NodeID get_or_create(const std::string& id) {
        auto it = index.find(id);
        if (it != index.end()) return it->second;
        NodeID new_id = static_cast<NodeID>(index.size());
        index.emplace(id, new_id);
        return new_id;
    }
};

/// True for the interface addresses CAIDA uses as synthetic non-responding-hop
/// placeholders: IPv4 multicast/reserved `224.0.0.0/3` (first octet >= 224), and
/// `0.0.0.0/8` (first octet == 0) used in releases <= 2013-04. IPv6 `FF00::/8`
/// placeholders begin "FF"/"ff".
bool is_placeholder_address(std::string_view addr) {
    if (addr.empty()) return false;
    // IPv6 placeholder FF00::/8.
    if (addr.size() >= 2 && (addr[0] == 'F' || addr[0] == 'f') &&
        (addr[1] == 'F' || addr[1] == 'f')) {
        return true;
    }
    // Leading octet of an IPv4 dotted quad.
    std::string_view first = addr;
    if (auto dot = addr.find('.'); dot != std::string_view::npos) {
        first = addr.substr(0, dot);
    }
    unsigned octet = 0;
    bool any = false;
    for (char c : first) {
        if (c < '0' || c > '9') return false;  // not a plain IPv4 octet
        octet = octet * 10 + static_cast<unsigned>(c - '0');
        any = true;
    }
    if (!any) return false;
    return octet == 0 || octet >= 224;
}

/// Take everything up to the first ':' — the node id of a `.links` member token,
/// or the label ("node"/"link") plus id of a header token when the caller has
/// already split off the leading keyword.
std::string_view node_id_of(std::string_view token) {
    auto colon = token.find(':');
    return (colon == std::string_view::npos) ? token : token.substr(0, colon);
}

}  // namespace

NetworkGraph load_caida_itdk(const ItdkConfig& config) {
    std::ifstream nodes_in(config.nodes_path);
    if (!nodes_in) {
        throw std::runtime_error("Cannot open CAIDA ITDK .nodes file: " + config.nodes_path);
    }
    std::ifstream links_in(config.links_path);
    if (!links_in) {
        throw std::runtime_error("Cannot open CAIDA ITDK .links file: " + config.links_path);
    }

    NodeRegistry registry;
    std::unordered_set<NodeID> dropped;  // placeholder-only nodes to omit from edges

    // --- Pass 1: .nodes — one router per line, space-separated interface list. ---
    // Format:  node <node_id>:   <i1> <i2> ... <in>
    std::string line;
    while (std::getline(nodes_in, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string keyword, node_tok;
        if (!(ss >> keyword) || keyword != "node") continue;
        if (!(ss >> node_tok)) continue;  // "<node_id>:"

        std::string node_id(node_id_of(node_tok));
        if (node_id.empty()) continue;
        NodeID dense = registry.get_or_create(node_id);

        if (config.drop_placeholder_nodes) {
            bool has_real = false;
            bool has_any = false;
            std::string iface;
            while (ss >> iface) {
                has_any = true;
                if (!is_placeholder_address(iface)) { has_real = true; break; }
            }
            // A node with interfaces, all of which are placeholders, is synthetic.
            if (has_any && !has_real) dropped.insert(dense);
        }
    }

    // --- Pass 2: .links — one IP-layer link per line, space-separated members. ---
    // Format:  link <link_id>:   <N1>:i1 <N2>[:i2] ...   (>2 members possible)
    // Expand each member set into pairwise (CLIQUE) or hub-and-spoke (STAR) edges.
    // Dedup within a link so a repeated member cannot double an edge; undirected
    // edges are emitted in both directions.
    std::vector<std::pair<NodeID, NodeID>> undirected;
    std::vector<NodeID> members;  // reused per line
    while (std::getline(links_in, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string keyword, link_tok;
        if (!(ss >> keyword) || keyword != "link") continue;
        if (!(ss >> link_tok)) continue;  // "<link_id>:"

        members.clear();
        std::string tok;
        while (ss >> tok) {
            std::string mid(node_id_of(tok));
            if (mid.empty() || mid[0] != 'N') continue;  // only node references
            NodeID dense = registry.get_or_create(mid);
            if (dropped.count(dense)) continue;
            // Guard against a member repeated on one line.
            bool seen = false;
            for (NodeID m : members) {
                if (m == dense) { seen = true; break; }
            }
            if (!seen) members.push_back(dense);
        }

        if (members.size() < 2) continue;

        if (config.expansion == ItdkLinkExpansion::STAR) {
            NodeID hub = members.front();
            for (std::size_t j = 1; j < members.size(); ++j) {
                undirected.emplace_back(hub, members[j]);
            }
        } else {  // CLIQUE
            for (std::size_t a = 0; a < members.size(); ++a) {
                for (std::size_t b = a + 1; b < members.size(); ++b) {
                    undirected.emplace_back(members[a], members[b]);
                }
            }
        }
    }

    NodeID num_nodes = static_cast<NodeID>(registry.index.size());

    // Materialize each undirected pair as two directed CSR edges (unit weight).
    std::vector<Edge> edges;
    edges.reserve(undirected.size() * 2);
    for (const auto& [u, v] : undirected) {
        edges.push_back({u, v, 1.0});
        edges.push_back({v, u, 1.0});
    }

    // --- Optional: .nodes.geo — attach (lat, lon). Missing file is not an error. ---
    // Format after colon (TAB-separated):
    //   continent country region city lat long pop IX source
    // Line:  node.geo N11:\tNA\tCA\tON\tToronto\t43.677200\t-79.630600\t\t\thoiho
    std::vector<Coord> coords;
    bool have_coords = false;
    if (!config.nodes_geo_path.empty()) {
        std::ifstream geo_in(config.nodes_geo_path);
        if (geo_in) {
            coords.assign(num_nodes, Coord{});
            std::string gline;
            while (std::getline(geo_in, gline)) {
                if (gline.empty() || gline[0] == '#') continue;

                // Everything up to the first ':' is "node.geo N<id>".
                auto colon = gline.find(':');
                if (colon == std::string::npos) continue;
                std::string head = gline.substr(0, colon);
                // head == "node.geo N<id>"; the node id is its last whitespace field.
                std::istringstream hs(head);
                std::string label, node_id;
                hs >> label;      // "node.geo"
                hs >> node_id;     // "N<id>"
                if (node_id.empty() || node_id[0] != 'N') continue;

                auto it = registry.index.find(node_id);
                if (it == registry.index.end()) continue;  // geo for a node not in graph

                // After the colon a single TAB always precedes the first field, so
                // strip it before splitting. Columns are then, positionally:
                //   0 continent 1 country 2 region 3 city 4 lat 5 long 6 pop 7 IX 8 source
                std::string tail = gline.substr(colon + 1);
                if (!tail.empty() && tail.front() == '\t') tail.erase(tail.begin());
                std::vector<std::string> f;
                std::string cell;
                std::istringstream ts(tail);
                while (std::getline(ts, cell, '\t')) f.push_back(cell);
                if (f.size() < 6) continue;
                const std::string& lat_s = f[4];
                const std::string& lon_s = f[5];
                if (lat_s.empty() || lon_s.empty()) continue;
                try {
                    double lat = std::stod(lat_s);
                    double lon = std::stod(lon_s);
                    coords[it->second] = Coord{lat, lon};
                    have_coords = true;
                } catch (const std::exception&) {
                    // Malformed row: leave this node's coordinate at default.
                }
            }
        }
    }

    NetworkGraph result;
    if (have_coords) {
        // Build CSR manually so coordinates ride along.
        std::vector<uint32_t> offsets(static_cast<std::size_t>(num_nodes) + 1, 0);
        for (const auto& e : edges) offsets[e.source + 1]++;
        for (NodeID i = 1; i <= num_nodes; ++i) offsets[i] += offsets[i - 1];

        std::vector<NodeID> targets(edges.size());
        std::vector<Weight> weights(edges.size());
        auto pos = offsets;
        for (const auto& e : edges) {
            uint32_t idx = pos[e.source]++;
            targets[idx] = e.target;
            weights[idx] = e.weight;
        }
        result.graph = std::make_unique<ArrayGraph>(
            std::move(offsets), std::move(targets), std::move(weights), std::move(coords));
    } else {
        result.graph = std::make_unique<ArrayGraph>(num_nodes, std::move(edges));
    }
    // capacity intentionally left empty: the ITDK carries no per-edge capacity.
    return result;
}

}  // namespace gravel
