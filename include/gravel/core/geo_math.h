#pragma once
#include "gravel/core/types.h"
#include <vector>

namespace gravel {

// Haversine distance in meters between two coordinates.
double haversine_meters(Coord a, Coord b);

// Ray-casting point-in-polygon test.
// polygon is a closed ring (first == last) or auto-closed.
bool point_in_polygon(Coord point, const std::vector<Coord>& polygon);

struct ProjectionResult {
    Coord projected;       // closest point on segment
    double t;              // parameter along segment [0,1], clamped
    double distance_m;     // perpendicular distance in meters
};

// Project a point onto a line segment defined by endpoints a and b.
// Returns the closest point on segment [a,b], the parameter t in [0,1],
// and the distance in meters from the point to the projected location.
ProjectionResult project_to_segment(Coord point, Coord seg_a, Coord seg_b);

}  // namespace gravel
