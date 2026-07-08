"""Shared hazard-overlay core: hazard polygons -> per-edge failure probabilities.

Every hazard dataset (flood, earthquake, drought, multi-hazard risk) reduces to
the same step: a GeoDataFrame of polygons carrying a severity/zone code, mapped
to per-edge failure probabilities via the shipped C++ point-in-polygon kernel.
The derivation lives in Python — the sub-library DAG keeps ``gravel-fragility``
hazard-agnostic (probabilities enter the kernel as a plain array) — and the
spatial predicate reuses the tested :func:`gravel.edges_in_polygon`.

Caveats (disclosed): an edge is "in" a zone only when *both* endpoints fall
inside it; polygon holes are ignored (slightly over-inclusive); the two directed
edges of a road share a probability but fail independently in the sampler.
"""

from __future__ import annotations

from collections.abc import Iterable, Mapping
from typing import TYPE_CHECKING

import numpy as np

from .. import _gravel
from .._gravel import Coord, Polygon

if TYPE_CHECKING:  # pragma: no cover - typing only

    pass


def hazard_edge_probabilities(graph, zones, *, baseline: float = 0.0) -> np.ndarray:
    """Per-edge failure probability from ``(gravel.Polygon, probability)`` zones.

    An edge takes a zone's probability when both endpoints fall inside the ring;
    where zones overlap, the **maximum** wins. Delegates to the C++ engine, which
    bbox-pre-filters and caches per-node point-in-polygon results. Returns a
    float64 array in CSR edge order, ready for :func:`gravel.stochastic_fragility`.
    """
    return np.asarray(
        _gravel.hazard_edge_probabilities(graph, list(zones), float(baseline)),
        dtype=np.float64,
    )


def rings_from_geometry(geom) -> Iterable[Polygon]:
    """Yield ``gravel.Polygon`` exterior rings (lat/lon) from a shapely (Multi)Polygon."""
    parts = geom.geoms if geom.geom_type == "MultiPolygon" else [geom]
    for part in parts:
        if part.is_empty:
            continue
        poly = Polygon()
        # shapely stores (x=lon, y=lat); gravel.Coord is (lat, lon).
        poly.vertices = [Coord(float(y), float(x)) for x, y in part.exterior.coords]
        yield poly


def edge_probabilities_from_frame(
    graph,
    footprint,
    *,
    code_field: str,
    code_probabilities: Mapping[str, float],
    baseline: float = 0.0,
    default_probability: float | None = None,
) -> np.ndarray:
    """Per-edge failure probability from a hazard ``GeoDataFrame``.

    Reprojects to WGS84, maps each polygon's ``code_field`` value through
    ``code_probabilities`` (severity/zone code -> probability in ``[0, 1]``), and
    marks the edges inside it. Shared by every hazard dataset's
    ``edge_probabilities``. Polygons whose code is absent from the table take
    ``default_probability`` (``None`` skips them). Requires geopandas.
    """
    try:
        import geopandas  # noqa: F401
    except ImportError as exc:  # pragma: no cover - exercised only without the dep
        raise ImportError(
            "hazard edge_probabilities requires geopandas — install with "
            "`pip install gravel-fragility[datasets]`."
        ) from exc

    gdf = footprint
    if gdf.crs is not None and gdf.crs.to_epsg() != 4326:
        gdf = gdf.to_crs(4326)

    zones: list[tuple[Polygon, float]] = []
    for geom, code in zip(gdf.geometry, gdf[code_field], strict=True):
        if geom is None or geom.is_empty:
            continue
        prob = code_probabilities.get(str(code).strip(), default_probability)
        if prob is None or prob <= 0.0:
            continue
        for ring in rings_from_geometry(geom):
            zones.append((ring, float(prob)))
    return hazard_edge_probabilities(graph, zones, baseline=baseline)
