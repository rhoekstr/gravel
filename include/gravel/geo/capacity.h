/// @file capacity.h
/// @brief Inferred per-edge road capacity (throughput) from OSM metadata.
///
/// OSM carries no direct capacity field; capacity is *inferred* from road class ×
/// lane count using Highway-Capacity-Manual-style per-lane throughput. This lives in
/// gravel-geo (it consumes OSM tags); the resulting per-edge capacity array is fed
/// into gravel-fragility as plain input data — fragility never derives capacity or
/// depends on geo (the DAG stays intact).
///
/// The constants are a disclosed modeling assumption, not ground truth: expose them,
/// sensitivity-sweep them, and report results as a function of them.

#pragma once
#include "gravel/core/edge_metadata.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace gravel {

/// Per-edge capacity model. Defaults follow HCM-style per-lane throughput
/// (passenger-car-equivalents / hour / lane) by OSM highway class. All values are
/// overridable and intended to be swept, never treated as exact.
struct CapacityConfig {
    /// Per-lane capacity (PCE/hour/lane) keyed by OSM `highway` class.
    std::unordered_map<std::string, double> per_lane_capacity;

    /// Default lane count by highway class, used when the `lanes` tag is absent.
    std::unordered_map<std::string, double> default_lanes;

    /// Capacity assigned when the highway class is unknown / unmapped.
    double fallback_capacity = 600.0;

    /// Citable HCM-style defaults (motorway ~2200 PCE/h/lane down to service ~400).
    static CapacityConfig hcm();
};

/// Estimate per-edge capacity (PCE/hour), indexed in CSR edge order — aligned with
/// `EdgeMetadata` and `Graph.to_coo()`. `capacity[e] = lanes[e] × per_lane(class[e])`,
/// using the `lanes` tag when present and parseable, otherwise the class default.
/// Edges whose class is unmapped get `config.fallback_capacity`.
std::vector<double> estimate_capacity(
    const EdgeMetadata& metadata,
    const CapacityConfig& config = CapacityConfig::hcm());

}  // namespace gravel
