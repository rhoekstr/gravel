#include "gravel/core/geo_math.h"
#include "gravel/core/constants.h"
#include <cmath>
#include <algorithm>
#include <vector>

namespace gravel {

static constexpr double EARTH_RADIUS = 6371000.0;  // meters

bool point_in_polygon(Coord point, const std::vector<Coord>& polygon) {
    if (polygon.size() < 3) return false;

    // Ray-casting algorithm: count crossings of ray from point going +lon
    bool inside = false;
    size_t n = polygon.size();
    // Auto-close: if last != first, treat edge (last, first) as implicit
    bool closed = (polygon.front().lat == polygon.back().lat &&
                   polygon.front().lon == polygon.back().lon);
    size_t edges = closed ? n - 1 : n;

    for (size_t i = 0, j = edges - 1; i < edges; j = i++) {
        const Coord& vi = polygon[i];
        const Coord& vj = polygon[j >= n ? 0 : j];

        if (((vi.lat > point.lat) != (vj.lat > point.lat)) &&
            (point.lon < (vj.lon - vi.lon) * (point.lat - vi.lat) /
                         (vj.lat - vi.lat) + vi.lon)) {
            inside = !inside;
        }
    }
    return inside;
}

double haversine_meters(Coord a, Coord b) {
    double dlat = (b.lat - a.lat) * DEG_TO_RAD;
    double dlon = (b.lon - a.lon) * DEG_TO_RAD;
    double s = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(a.lat * DEG_TO_RAD) * std::cos(b.lat * DEG_TO_RAD) *
               std::sin(dlon / 2) * std::sin(dlon / 2);
    return EARTH_RADIUS * 2.0 * std::atan2(std::sqrt(s), std::sqrt(1.0 - s));
}

ProjectionResult project_to_segment(Coord point, Coord seg_a, Coord seg_b) {
    // Work in a local flat approximation (equirectangular projection)
    // centered at seg_a. For short segments (roads), this is accurate.
    double cos_lat = std::cos(seg_a.lat * DEG_TO_RAD);

    double ax = 0.0, ay = 0.0;
    double bx = (seg_b.lon - seg_a.lon) * cos_lat;
    double by = (seg_b.lat - seg_a.lat);
    double px = (point.lon - seg_a.lon) * cos_lat;
    double py = (point.lat - seg_a.lat);

    double dx = bx - ax;
    double dy = by - ay;
    double len_sq = dx * dx + dy * dy;

    double t;
    if (len_sq < 1e-20) {
        // Degenerate segment (same start and end)
        t = 0.0;
    } else {
        t = ((px - ax) * dx + (py - ay) * dy) / len_sq;
        t = std::clamp(t, 0.0, 1.0);
    }

    Coord projected;
    projected.lat = seg_a.lat + t * (seg_b.lat - seg_a.lat);
    projected.lon = seg_a.lon + t * (seg_b.lon - seg_a.lon);

    double distance_m = haversine_meters(point, projected);

    return {projected, t, distance_m};
}

}  // namespace gravel
