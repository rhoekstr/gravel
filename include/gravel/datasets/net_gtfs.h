#pragma once
/// @file net_gtfs.h
/// @brief Parse a GTFS Schedule (static) feed directory into a transit `NetworkGraph`.

#include "gravel/datasets/network_graph.h"

#include <string>

namespace gravel {

/// Vehicle-capacity assumptions (persons per vehicle) keyed by GTFS `route_type`,
/// used to turn a per-edge vehicle frequency into a persons-per-hour throughput
/// proxy. GTFS carries **no** vehicle-capacity or consist-size field, so these are
/// injected planning defaults, not spec values — override them per system when you
/// know the real fleet. Metro/rail figures are the least reliable (a GTFS "trip" is
/// one train of unknown car count). See the spec notes in the source file.
struct GtfsCapacityModel {
    double bus = 60.0;       ///< route_type 3 (bus) / 11 (trolleybus): standard 40-ft crush load.
    double tram = 180.0;     ///< route_type 0 (tram/light rail) / 5 (cable) / 12 (monorail).
    double subway = 1000.0;  ///< route_type 1 (subway/metro): per train (car count unknown to GTFS).
    double rail = 800.0;     ///< route_type 2 (rail): per train, consist-dependent.
    double ferry = 400.0;    ///< route_type 4 (ferry).
    double lift = 40.0;      ///< route_type 6 (aerial lift) / 7 (funicular).
    double other = 60.0;     ///< any unrecognized / extended (>=100) route_type: bus-like fallback.
};

/// Configuration for loading a GTFS static feed.
struct GtfsConfig {
    /// Path to a **directory** of already-extracted GTFS `.txt` files (Python owns
    /// the download + unzip). Must contain `stops.txt`, `routes.txt`, `trips.txt`,
    /// and `stop_times.txt`; `frequencies.txt` is used when present.
    std::string dir;

    /// Per-mode vehicle-capacity assumptions for the throughput proxy (see above).
    GtfsCapacityModel capacity_model{};

    /// Analysis window in hours used as the frequency denominator when deriving the
    /// all-day throughput proxy (vehicles / window_hours). Defaults to a nominal
    /// 18-hour service span so a single all-day trip yields a small, non-zero
    /// frequency rather than an unrealistic "1 vehicle in 1 hour".
    double window_hours = 18.0;
};

/// Load a GTFS static feed from a directory of extracted CSVs into a transit graph.
///
/// Builds a **directed** graph whose nodes are the boardable stops
/// (`location_type` 0 or empty) carrying WGS84 coordinates, and whose edges are the
/// consecutive stop-to-stop hops within each trip (from `stop_times.txt`, ordered by
/// `stop_sequence`). Parallel trips over the same physical hop are merged into one
/// directed edge whose per-edge **capacity** is a supply-side throughput proxy:
///
///   `capacity_persons_per_hr = frequency_veh_per_hr(hop) x vehicle_capacity(route_type)`
///
/// summed over the routes traversing the hop. Frequency is derived from the number
/// of distinct trips over the hop across the feed (or from `frequencies.txt`
/// `headway_secs` when a trip is headway-based) divided by `config.window_hours`.
/// The edge weight is the mean stop-to-stop traversal time in seconds
/// (`arrival(b) - departure(a)`, >24:00:00-safe), falling back to a nominal value
/// when intermediate times are interpolated (empty).
///
/// @param config Directory path plus the capacity model and analysis window.
/// @return A `NetworkGraph` with node coordinates populated and a CSR-aligned
///         per-edge `capacity` vector.
/// @throws std::runtime_error if a required file is missing or unparseable.
///
/// @note Simplifications (documented so the capacity is auditable): trips are
///       counted across the whole feed rather than filtered to a representative
///       service day (`calendar.txt` / `calendar_dates.txt` are not consulted);
///       platforms are used directly as nodes rather than collapsed to
///       `parent_station`; and edge geometry from `shapes.txt` is not attached
///       (edges are stop-to-stop, so the graph carries no `EDGE_GEOMETRY`).
NetworkGraph load_gtfs_network(const GtfsConfig& config);

}  // namespace gravel
