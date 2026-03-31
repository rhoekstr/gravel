/// @file tiled_fragility.h
/// @brief Spatial decomposition of fragility across a region.
///
/// County-level fragility is a single number. But "which part of the county
/// drives the score?" matters for both analysis and policy. This module
/// tiles a region into a grid and runs location_fragility at each tile center,
/// producing a spatial field of isolation risk.
///
/// Why: A county with composite_index=0.45 might have one corner with risk 0.8
/// (near a single-bridge mountain pass) and everywhere else at 0.2. The tiled
/// decomposition reveals this spatial structure, which is invisible in the
/// aggregate score.
///
/// The output is directly mappable: each tile has a center coordinate and an
/// isolation_risk value, suitable for GeoJSON heatmap visualization.

#pragma once
#include "gravel/analysis/location_fragility.h"
#include <vector>

namespace gravel {

/// Configuration for tiled fragility analysis.
struct TileConfig {
    double min_lat, max_lat, min_lon, max_lon;  ///< Bounding box
    double tile_size_meters = 16000.0;           ///< ~10 miles per tile

    /// Base LocationFragilityConfig (center is overridden per tile).
    LocationFragilityConfig location_config;
};

/// Result for a single tile.
struct TileResult {
    Coord center;
    LocationFragilityResult fragility;
};

/// Result of tiled fragility across a spatial grid.
struct TiledFragilityResult {
    std::vector<TileResult> tiles;
    uint32_t rows = 0, cols = 0;

    /// Aggregate statistics
    double mean_isolation_risk = 0.0;
    double max_isolation_risk = 0.0;
    double min_isolation_risk = 1.0;
    Coord max_risk_location;    ///< Tile center with highest isolation risk
};

/// Run location_fragility at each tile center across a spatial grid.
/// Parallelized with OpenMP when available.
///
/// @param graph   Full graph with coordinates.
/// @param ch      Pre-built CH on the full graph.
/// @param config  Tile grid configuration.
TiledFragilityResult tiled_fragility_analysis(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const TileConfig& config);

}  // namespace gravel
