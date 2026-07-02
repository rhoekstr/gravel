#pragma once
/// @file edge_geometry.h
/// @brief Optional ragged per-edge polyline geometry, CSR-aligned to a graph's edges.

#include "gravel/core/types.h"

#include <cstdint>
#include <span>
#include <vector>

namespace gravel {

/// Per-edge polyline geometry stored in ragged-CSR form, index-aligned to a
/// graph's edge order (the same order as `ArrayGraph::to_coo()` / the CSR
/// target array).
///
/// Edge `e`'s polyline is `points[offsets[e] .. offsets[e + 1])`, running from
/// the edge's source coordinate (first point) to its target coordinate (last
/// point) and passing through any intermediate shape points. Every populated
/// edge has at least two points.
///
/// This is *optional* modeling data: it is only produced when explicitly
/// requested (see `SimplificationConfig::emit_geometry`) and is empty otherwise,
/// in which case consumers fall back to straight source→target segments. Pure
/// data — it carries no graph dependency and lives in `gravel-core` so both
/// `gravel-simplify` (which creates it when collapsing degree-2 chains) and
/// `gravel-geo` may populate it without crossing the sub-library DAG.
struct EdgeGeometry {
    /// Prefix-sum offsets into `points`; size is `edge_count() + 1` when
    /// populated, empty when no geometry is present.
    std::vector<uint32_t> offsets;

    /// Concatenated per-edge coordinate runs, addressed via `offsets`.
    std::vector<Coord> points;

    /// Number of edges this geometry describes (0 when empty).
    uint32_t edge_count() const {
        return offsets.empty() ? 0u : static_cast<uint32_t>(offsets.size() - 1);
    }

    /// True when no geometry is stored (consumers should use straight segments).
    bool empty() const { return offsets.size() <= 1; }

    /// The polyline for edge `e` as a contiguous view. Caller must ensure
    /// `e < edge_count()`.
    std::span<const Coord> points_for(uint32_t e) const {
        return std::span<const Coord>(points.data() + offsets[e],
                                      points.data() + offsets[e + 1]);
    }
};

}  // namespace gravel
