#pragma once
/// @file net_gridsfm.h
/// @brief Loader for the Microsoft Research GridSFM US transmission-grid dataset.

#include "gravel/datasets/network_graph.h"

#include <string>

namespace gravel {

/// Parse one GridSFM power-grid case into a `NetworkGraph`.
///
/// Reads a Hugging Face `<name>_model.json` case from the
/// `microsoft/GridSFM_US_power_grid` dataset — a PowerModels/MATPOWER-style JSON
/// object whose `bus` and `branch` collections are dict-of-dicts keyed by
/// stringified integer id. Buses become nodes carrying WGS84 `lat`/`lon`
/// coordinates; in-service branches (`br_status == 1`) become edges whose
/// endpoints are resolved through the `bus_i` id map (`f_bus`/`t_bus` are bus
/// *ids*, not positional indices). The per-edge capacity is the long-term
/// thermal rating in MVA, computed as `branch.rate_a * baseMVA` (baseMVA is
/// 100.0). Transformers and AC lines share the `branch` table and are both kept.
///
/// The graph is built directed (one edge per branch) and the returned
/// `capacity` vector is CSR-aligned with the graph's edges. Buses missing a
/// `lat`/`lon` are dropped defensively; branches referencing a dropped bus are
/// skipped.
///
/// @param model_json_path Path to a single `*_model.json` case file.
/// @return A `NetworkGraph` with node coordinates and a per-edge capacity (MVA)
///         vector aligned in CSR edge order.
/// @throws std::runtime_error if the file cannot be opened or is not a valid
///         GridSFM model object (missing `bus`/`branch`).
NetworkGraph load_gridsfm_network(const std::string& model_json_path);

}  // namespace gravel
