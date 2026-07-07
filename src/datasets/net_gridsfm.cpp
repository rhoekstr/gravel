#include "gravel/datasets/net_gridsfm.h"

#include "gravel/core/array_graph.h"
#include "gravel/core/types.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gravel {

namespace {

/// A parsed branch, endpoints already resolved to dense node ids.
struct ResolvedEdge {
    NodeID source;
    NodeID target;
    double capacity_mva;  ///< rate_a * baseMVA.
    double length_km;     ///< 0.0 for transformers.
};

}  // namespace

NetworkGraph load_gridsfm_network(const std::string& model_json_path) {
    std::ifstream in(model_json_path);
    if (!in) {
        throw std::runtime_error("Cannot open GridSFM model file: " + model_json_path);
    }

    nlohmann::json doc;
    try {
        in >> doc;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Invalid JSON in GridSFM model file '" + model_json_path +
                                 "': " + e.what());
    }

    if (!doc.contains("bus") || !doc["bus"].is_object()) {
        throw std::runtime_error(
            "GridSFM model file missing object field 'bus': " + model_json_path);
    }
    if (!doc.contains("branch") || !doc["branch"].is_object()) {
        throw std::runtime_error(
            "GridSFM model file missing object field 'branch': " + model_json_path);
    }

    // baseMVA converts per-unit ratings to MVA; PowerModels default is 100.0.
    const double base_mva = doc.value("baseMVA", 100.0);

    // --- Nodes: iterate the bus dict; key == bus_i == index. ---
    // Buses without valid lat/lon are dropped, so build the id map defensively.
    std::unordered_map<int64_t, NodeID> id_to_node;
    std::vector<Coord> coords;
    id_to_node.reserve(doc["bus"].size());
    coords.reserve(doc["bus"].size());

    for (const auto& [key, bus] : doc["bus"].items()) {
        if (!bus.contains("lat") || !bus.contains("lon") || bus["lat"].is_null() ||
            bus["lon"].is_null()) {
            continue;  // no geography — skip this bus.
        }
        if (!bus.contains("bus_i") || !bus["bus_i"].is_number()) {
            continue;  // malformed — no usable id.
        }

        const int64_t bus_i = bus["bus_i"].get<int64_t>();
        if (id_to_node.count(bus_i)) {
            continue;  // duplicate id; keep first.
        }

        id_to_node.emplace(bus_i, static_cast<NodeID>(coords.size()));
        coords.push_back(Coord{bus["lat"].get<double>(), bus["lon"].get<double>()});
    }

    const NodeID num_nodes = static_cast<NodeID>(coords.size());

    // --- Edges: iterate the branch dict; f_bus/t_bus are bus ids, not indices. ---
    std::vector<ResolvedEdge> resolved;
    resolved.reserve(doc["branch"].size());

    for (const auto& [key, br] : doc["branch"].items()) {
        // Keep only in-service branches.
        if (br.value("br_status", 1) != 1) {
            continue;
        }
        if (!br.contains("f_bus") || !br.contains("t_bus")) {
            continue;
        }

        const auto fu = id_to_node.find(br["f_bus"].get<int64_t>());
        const auto tv = id_to_node.find(br["t_bus"].get<int64_t>());
        if (fu == id_to_node.end() || tv == id_to_node.end()) {
            continue;  // endpoint bus was dropped (missing coords) or unknown.
        }

        const double rate_a = br.value("rate_a", 0.0);
        resolved.push_back(ResolvedEdge{
            fu->second,
            tv->second,
            rate_a * base_mva,
            br.value("length_km", 0.0),
        });
    }

    // --- Assemble CSR arrays manually so `capacity` stays in exact lock-step ---
    // with the CSR target/weight ordering. Weight = physical length in km
    // (0.0 for transformers); capacity is carried separately as MVA.
    std::vector<uint32_t> offsets(static_cast<size_t>(num_nodes) + 1, 0);
    for (const auto& e : resolved) {
        offsets[e.source + 1]++;
    }
    for (NodeID i = 1; i <= num_nodes; ++i) {
        offsets[i] += offsets[i - 1];
    }

    const size_t edge_count = resolved.size();
    std::vector<NodeID> targets(edge_count);
    std::vector<Weight> weights(edge_count);
    std::vector<double> capacity(edge_count);

    std::vector<uint32_t> cursor = offsets;
    for (const auto& e : resolved) {
        const uint32_t idx = cursor[e.source]++;
        targets[idx] = e.target;
        weights[idx] = e.length_km;
        capacity[idx] = e.capacity_mva;
    }

    NetworkGraph result;
    result.graph = std::make_unique<ArrayGraph>(
        std::move(offsets), std::move(targets), std::move(weights), std::move(coords));
    result.capacity = std::move(capacity);
    return result;
}

}  // namespace gravel
