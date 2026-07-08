#pragma once
/// @file net_openflights.h
/// @brief Load the OpenFlights air-transport network (airports + routes) as a NetworkGraph.

#include "gravel/datasets/network_graph.h"

#include <string>
#include <vector>

namespace gravel {

/// Load the OpenFlights air network from its two raw data files.
///
/// OpenFlights ships as headerless, comma-separated, RFC-4180-quoted `.dat` files
/// (https://openflights.org/data.php). This loader consumes two of them:
///
///  - `airports.dat` → graph **nodes**. Each airport becomes one node keyed by its
///    OpenFlights Airport ID (column 1); the node carries the airport's decimal
///    latitude (column 7) and longitude (column 8) as its `Coord`. Airports whose
///    IATA code is the NULL token `\N` are still loaded as nodes (they can be edge
///    endpoints via the Airport-ID foreign key), but they are omitted from the
///    IATA→node index used for string-code fallback.
///
///  - `routes.dat` → directed graph **edges**, one per `(source airport → destination
///    airport)` row. An endpoint is resolved by its OpenFlights Airport ID (routes
///    columns 4 and 6) when present, falling back to the 3-letter IATA code (columns
///    3 and 5) via the airport IATA index. Rows whose endpoints resolve to no known
///    airport are dropped. Edge weight is the great-circle (haversine) distance in
///    metres between the two airports.
///
/// The result carries node coordinates but an **empty** `capacity` vector: OpenFlights
/// has no native per-edge capacity. Seat / passenger / departure capacity is a separate
/// BTS T-100 overlay joined by ordered IATA pair, which is out of scope for this loader.
///
/// @param airports_path  Filesystem path to OpenFlights `airports.dat` (or
///                        `airports-extended.dat`; the column layout is identical).
/// @param routes_path    Filesystem path to OpenFlights `routes.dat`.
/// @param collapse_parallel  When true (default), multiple rows sharing the same ordered
///                        `(source, destination)` airport pair — different airlines,
///                        equipment, or codeshare duplicates — collapse to a single
///                        directed edge. When false, every route row yields its own edge.
/// @param drop_codeshare  When true (default), rows flagged as codeshares
///                        (routes column 7 == `Y`) are skipped, since they are marketing
///                        duplicates of an operated flight rather than distinct routes.
/// @return A NetworkGraph whose `graph` holds the directed air network with per-node
///         coordinates, and whose `capacity` is empty.
/// @throws std::runtime_error if either file cannot be opened.
/// @param node_iata  When non-null, filled with each node's IATA code ("" for
///                   airports without one), index-aligned to the graph's nodes —
///                   the join key for the BTS T-100 capacity overlay.
NetworkGraph load_openflights_network(const std::string& airports_path,
                                      const std::string& routes_path,
                                      bool collapse_parallel = true,
                                      bool drop_codeshare = true,
                                      std::vector<std::string>* node_iata = nullptr);

}  // namespace gravel
