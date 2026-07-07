#include "gravel/datasets/net_opfdata.h"

#include "gravel/core/array_graph.h"
#include "gravel/core/types.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gravel {

namespace {

using nlohmann::json;

/// Feature-column indices for the long-term thermal rating `rate_a`. The two edge
/// types order their feature rows differently, so the constants must NOT be shared:
/// ac_line is [angmin, angmax, b_fr, b_to, br_r, br_x, rate_a, ...] (rate_a at 6),
/// transformer is [angmin, angmax, br_r, br_x, rate_a, ...] (rate_a at 4).
constexpr std::size_t kAcLineRateA = 6;
constexpr std::size_t kTransformerRateA = 4;

/// Column indices for the series impedance (br_r, br_x), also edge-type-specific.
constexpr std::size_t kAcLineBrR = 4;
constexpr std::size_t kAcLineBrX = 5;
constexpr std::size_t kTransformerBrR = 2;
constexpr std::size_t kTransformerBrX = 3;

/// One parsed line/transformer, before CSR assembly.
struct RawBranch {
    NodeID from;
    NodeID to;
    double capacity;  ///< physical MVA thermal rating; +inf if rate_a == 0 (no limit).
    double weight;    ///< impedance magnitude hypot(br_r, br_x); an electrical "length".
};

/// Read grid.context[0] as baseMVA, defaulting to 1.0 (per-unit passthrough) when
/// the context array is absent or empty. baseMVA converts p.u. ratings to physical MVA.
double read_base_mva(const json& grid) {
    auto it = grid.find("context");
    if (it == grid.end() || !it->is_array() || it->empty()) return 1.0;
    const json& first = (*it)[0];
    if (first.is_array()) {  // tolerate a nested [[baseMVA]] shape.
        if (first.empty()) return 1.0;
        return first[0].get<double>();
    }
    return first.get<double>();
}

/// Convert a per-unit rate_a to a physical MVA capacity. In PGLib, rate_a == 0
/// means "no thermal limit" (unbounded), so it maps to +infinity, never zero.
double capacity_from_rate_a(double rate_a_pu, double base_mva) {
    if (rate_a_pu == 0.0) return std::numeric_limits<double>::infinity();
    return rate_a_pu * base_mva;
}

/// Parse one edge group (ac_line or transformer): parallel senders/receivers plus a
/// per-edge feature row. Appends a RawBranch per edge to `out`.
void parse_edge_group(const json& edges, const char* key, std::size_t rate_a_col,
                      std::size_t br_r_col, std::size_t br_x_col, double base_mva,
                      NodeID num_buses, std::vector<RawBranch>& out) {
    auto group_it = edges.find(key);
    if (group_it == edges.end()) return;
    const json& group = *group_it;

    auto senders_it = group.find("senders");
    auto receivers_it = group.find("receivers");
    auto features_it = group.find("features");
    if (senders_it == group.end() || receivers_it == group.end()) return;

    const json& senders = *senders_it;
    const json& receivers = *receivers_it;
    const bool has_features = (features_it != group.end() && features_it->is_array());
    const json empty_features = json::array();
    const json& features = has_features ? *features_it : empty_features;

    if (!senders.is_array() || !receivers.is_array()) return;
    if (senders.size() != receivers.size()) {
        throw std::runtime_error(
            std::string("OPFData: senders/receivers length mismatch in edge group '") +
            key + "'");
    }

    for (std::size_t k = 0; k < senders.size(); ++k) {
        const auto from = senders[k].get<int64_t>();
        const auto to = receivers[k].get<int64_t>();
        if (from < 0 || to < 0 || from >= num_buses || to >= num_buses) {
            throw std::runtime_error(
                std::string("OPFData: bus id out of range in edge group '") + key + "'");
        }

        double capacity = std::numeric_limits<double>::infinity();
        double weight = 0.0;
        if (k < features.size()) {
            const json& row = features[k];
            if (row.is_array()) {
                if (rate_a_col < row.size()) {
                    capacity = capacity_from_rate_a(row[rate_a_col].get<double>(), base_mva);
                }
                if (br_r_col < row.size() && br_x_col < row.size()) {
                    weight = std::hypot(row[br_r_col].get<double>(),
                                        row[br_x_col].get<double>());
                }
            }
        }

        out.push_back({static_cast<NodeID>(from), static_cast<NodeID>(to), capacity, weight});
    }
}

}  // namespace

NetworkGraph load_opfdata_graph(const std::string& json_path) {
    std::ifstream in(json_path);
    if (!in) throw std::runtime_error("OPFData: cannot open file: " + json_path);

    json root;
    try {
        in >> root;
    } catch (const json::parse_error& e) {
        throw std::runtime_error("OPFData: invalid JSON in " + json_path + ": " + e.what());
    }

    auto grid_it = root.find("grid");
    if (grid_it == root.end())
        throw std::runtime_error("OPFData: missing top-level 'grid' object in " + json_path);
    const json& grid = *grid_it;

    auto nodes_it = grid.find("nodes");
    if (nodes_it == grid.end())
        throw std::runtime_error("OPFData: missing 'grid.nodes' in " + json_path);
    auto bus_it = nodes_it->find("bus");
    if (bus_it == nodes_it->end() || !bus_it->is_array())
        throw std::runtime_error("OPFData: missing 'grid.nodes.bus' array in " + json_path);

    const NodeID num_buses = static_cast<NodeID>(bus_it->size());
    const double base_mva = read_base_mva(grid);

    std::vector<RawBranch> branches;
    auto edges_it = grid.find("edges");
    if (edges_it != grid.end()) {
        parse_edge_group(*edges_it, "ac_line", kAcLineRateA, kAcLineBrR, kAcLineBrX,
                         base_mva, num_buses, branches);
        parse_edge_group(*edges_it, "transformer", kTransformerRateA, kTransformerBrR,
                         kTransformerBrX, base_mva, num_buses, branches);
    }

    // Build CSR directly so per-edge capacity stays aligned with the target array.
    // Each undirected branch contributes two directed CSR entries (forward + reverse)
    // that carry the same capacity and weight.
    const std::size_t directed_count = branches.size() * 2;
    std::vector<uint32_t> offsets(num_buses + 1, 0);
    for (const auto& b : branches) {
        ++offsets[b.from + 1];
        ++offsets[b.to + 1];
    }
    for (NodeID i = 1; i <= num_buses; ++i) offsets[i] += offsets[i - 1];

    std::vector<NodeID> targets(directed_count);
    std::vector<Weight> weights(directed_count);
    std::vector<double> capacity(directed_count);
    std::vector<uint32_t> pos(offsets.begin(), offsets.end());
    for (const auto& b : branches) {
        const uint32_t fwd = pos[b.from]++;
        targets[fwd] = b.to;
        weights[fwd] = b.weight;
        capacity[fwd] = b.capacity;

        const uint32_t rev = pos[b.to]++;
        targets[rev] = b.from;
        weights[rev] = b.weight;
        capacity[rev] = b.capacity;
    }

    NetworkGraph result;
    result.graph = std::make_unique<ArrayGraph>(
        std::move(offsets), std::move(targets), std::move(weights));  // no coords
    result.capacity = std::move(capacity);
    return result;
}

}  // namespace gravel
