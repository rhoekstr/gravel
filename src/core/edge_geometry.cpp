#include "gravel/core/edge_geometry.h"

#include <cmath>
#include <utility>
#include <vector>

namespace gravel {
namespace {

// Perpendicular distance from p to segment a-b, treating (lon, lat) as a plane (fine for the
// small spans involved in visualization downscaling).
double perp_distance(const Coord& p, const Coord& a, const Coord& b) {
    const double dx = b.lon - a.lon, dy = b.lat - a.lat;
    const double len2 = dx * dx + dy * dy;
    double px = a.lon, py = a.lat;
    if (len2 > 0.0) {
        double t = ((p.lon - a.lon) * dx + (p.lat - a.lat) * dy) / len2;
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        px = a.lon + t * dx;
        py = a.lat + t * dy;
    }
    const double ex = p.lon - px, ey = p.lat - py;
    return std::sqrt(ex * ex + ey * ey);
}

// Douglas-Peucker: keep endpoints and any vertex farther than `tol` from the running chord.
std::vector<Coord> douglas_peucker(const std::span<const Coord>& pts, double tol) {
    const size_t n = pts.size();
    if (n <= 2) return {pts.begin(), pts.end()};
    std::vector<char> keep(n, 0);
    keep[0] = keep[n - 1] = 1;
    std::vector<std::pair<size_t, size_t>> stack{{0, n - 1}};
    while (!stack.empty()) {
        auto [lo, hi] = stack.back();
        stack.pop_back();
        double dmax = 0.0;
        size_t idx = lo;
        for (size_t i = lo + 1; i < hi; ++i) {
            double d = perp_distance(pts[i], pts[lo], pts[hi]);
            if (d > dmax) {
                dmax = d;
                idx = i;
            }
        }
        if (dmax > tol) {
            keep[idx] = 1;
            stack.push_back({lo, idx});
            stack.push_back({idx, hi});
        }
    }
    std::vector<Coord> out;
    for (size_t i = 0; i < n; ++i) {
        if (keep[i]) out.push_back(pts[i]);
    }
    return out;
}

}  // namespace

EdgeGeometry simplify_edge_geometry(const EdgeGeometry& geometry, double tolerance) {
    if (geometry.empty() || tolerance <= 0.0) return geometry;
    EdgeGeometry out;
    const uint32_t e_count = geometry.edge_count();
    out.offsets.reserve(e_count + 1);
    out.offsets.push_back(0);
    for (uint32_t e = 0; e < e_count; ++e) {
        std::vector<Coord> simp = douglas_peucker(geometry.points_for(e), tolerance);
        for (const Coord& c : simp) out.points.push_back(c);
        out.offsets.push_back(static_cast<uint32_t>(out.points.size()));
    }
    return out;
}

}  // namespace gravel
