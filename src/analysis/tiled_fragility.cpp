#include "gravel/analysis/tiled_fragility.h"
#include <cmath>

namespace gravel {

TiledFragilityResult tiled_fragility_analysis(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const TileConfig& config) {

    TiledFragilityResult result;

    // Compute grid dimensions
    // Approximate degrees per tile (at the center latitude)
    double center_lat = (config.min_lat + config.max_lat) / 2.0;
    double meters_per_deg_lat = 111320.0;
    double meters_per_deg_lon = 111320.0 * std::cos(center_lat * M_PI / 180.0);

    double lat_step = config.tile_size_meters / meters_per_deg_lat;
    double lon_step = config.tile_size_meters / meters_per_deg_lon;

    result.rows = static_cast<uint32_t>(
        std::ceil((config.max_lat - config.min_lat) / lat_step));
    result.cols = static_cast<uint32_t>(
        std::ceil((config.max_lon - config.min_lon) / lon_step));

    if (result.rows == 0) result.rows = 1;
    if (result.cols == 0) result.cols = 1;

    uint32_t total_tiles = result.rows * result.cols;
    result.tiles.resize(total_tiles);

    // Compute tile centers
    for (uint32_t r = 0; r < result.rows; ++r) {
        for (uint32_t c = 0; c < result.cols; ++c) {
            uint32_t idx_t = r * result.cols + c;
            result.tiles[idx_t].center = {
                config.min_lat + (r + 0.5) * lat_step,
                config.min_lon + (c + 0.5) * lon_step
            };
        }
    }

    // Run location_fragility at each tile center (parallelized)
    int nt = static_cast<int>(total_tiles);

    #pragma omp parallel for schedule(dynamic) if(nt > 4)
    for (int i = 0; i < nt; ++i) {
        auto cfg = config.location_config;
        cfg.center = result.tiles[i].center;
        result.tiles[i].fragility = location_fragility(graph, ch, cfg);
    }

    // Aggregate statistics
    double sum_risk = 0.0;
    result.max_isolation_risk = 0.0;
    result.min_isolation_risk = 1.0;

    for (const auto& tile : result.tiles) {
        double risk = tile.fragility.isolation_risk;
        sum_risk += risk;
        if (risk > result.max_isolation_risk) {
            result.max_isolation_risk = risk;
            result.max_risk_location = tile.center;
        }
        if (risk < result.min_isolation_risk) {
            result.min_isolation_risk = risk;
        }
    }

    result.mean_isolation_risk = (total_tiles > 0) ? sum_risk / total_tiles : 0.0;

    return result;
}

}  // namespace gravel
