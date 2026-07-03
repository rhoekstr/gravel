"""Hazard-based edge failure probabilities for stochastic fragility.

Turns a spatial hazard footprint (e.g. a FEMA National Flood Hazard Layer
floodplain) into the per-edge failure-probability array that
:func:`gravel.stochastic_fragility` consumes. The derivation lives here in
Python on purpose: the sub-library DAG keeps ``gravel-fragility``
hazard-agnostic — probabilities enter the kernel as a plain array, and *where*
they come from is a geo/Python concern. The spatial predicate reuses the
shipped, tested C++ :func:`gravel.edges_in_polygon` rather than a second
point-in-polygon.

Two-layer API
-------------
* :func:`hazard_edge_probabilities` — geopandas-free core. You supply
  ``(gravel.Polygon, probability)`` zones; it returns the per-edge array. Use it
  for any hazard you can express as polygons (flood, wildfire perimeter, seismic
  liquefaction zone, a hand-drawn scenario footprint).
* :func:`flood_edge_probabilities` — FEMA NFHL convenience. You supply a
  GeoPandas ``GeoDataFrame`` of flood polygons; it maps flood-zone codes to
  probabilities and calls the core. Needs ``pip install gravel-fragility[interop]``.

Probability semantics — read this before trusting the output
------------------------------------------------------------
``stochastic_fragility`` treats ``prob[e]`` as the chance edge ``e`` fails in a
single Monte-Carlo realization, so what one "run" *means* is a modeling choice:

* **Event / scenario** (the default here): ``prob[e] = P(road impassable | the
  mapped flood occurs)``. A run is one realization of a design flood; roads in the
  Special Flood Hazard Area close with high probability. This matches "what
  happens to the network when the 100-year flood hits." The defaults in
  :data:`NFHL_EVENT_CLOSURE` are *illustrative* closure rates chosen to be
  reasonable — they are **not** FEMA-published closure probabilities. Sweep them.
* **Annualized**: ``prob[e]`` = the zone's annual exceedance probability (SFHA
  ≈ 0.01, the 0.2%-annual-chance zone ≈ 0.002 — what the zone codes literally
  encode). A run is one "year." Pass :data:`NFHL_ANNUAL_PROBABILITY` as
  ``zone_probabilities``. These numbers are grounded in the zone definitions.

Caveats (disclosed, not hidden)
-------------------------------
* An edge is "in" a zone only when *both* endpoints fall inside it (this matches
  :func:`gravel.edges_in_polygon`; an edge straddling the boundary is excluded).
* Polygon holes (interior rings) are ignored — a road inside a donut hole is still
  counted as flooded. Slightly over-inclusive.
* The two directed edges of a road get the same probability, but the sampler fails
  them independently, so a road can occasionally "half-close" across runs.
* Cost is O(#rings x #nodes): each polygon ring scans every node. For a
  national-scale flood layer, dissolve/merge the polygons by zone first (fewer,
  larger rings) before calling this.
"""

from __future__ import annotations

import json
import os
from collections.abc import Iterable
from typing import TYPE_CHECKING
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

import numpy as np

from ._gravel import Coord, Polygon, edges_in_polygon

if TYPE_CHECKING:  # pragma: no cover - typing only
    import geopandas as gpd

    from ._gravel import Graph


# FEMA NFHL flood-zone code -> per-event road-closure probability, for a
# design-flood ("100-year") scenario. ILLUSTRATIVE defaults: reasonable, sweepable,
# and explicitly NOT FEMA-published closure rates. High-hazard zones (A*, V*) close
# with high probability when the design flood occurs; the 0.2%-annual-chance fringe
# closes far less often in that same event.
NFHL_EVENT_CLOSURE: dict[str, float] = {
    "A": 0.90,
    "AE": 0.90,
    "AH": 0.85,
    "AO": 0.85,
    "AR": 0.80,
    "A99": 0.80,
    "V": 0.95,
    "VE": 0.95,
    "0.2 PCT ANNUAL CHANCE FLOOD HAZARD": 0.25,
}

# FEMA NFHL flood-zone code -> annual exceedance probability. GROUNDED in the zone
# definitions: Special Flood Hazard Areas are the 1%-annual-chance ("100-year")
# floodplain; the shaded-X zone is the 0.2%-annual-chance ("500-year") floodplain.
# Use this when a Monte-Carlo run should represent "one year," not "one design flood."
NFHL_ANNUAL_PROBABILITY: dict[str, float] = {
    "A": 0.01,
    "AE": 0.01,
    "AH": 0.01,
    "AO": 0.01,
    "AR": 0.01,
    "A99": 0.01,
    "V": 0.01,
    "VE": 0.01,
    "0.2 PCT ANNUAL CHANCE FLOOD HAZARD": 0.002,
}


# FEMA National Flood Hazard Layer (NFHL) ArcGIS MapServer. Override with the
# ``GRAVEL_NFHL_ENDPOINT`` environment variable, or pass ``endpoint=`` to the fetcher
# (e.g. a mirror, a regional service, or a locally hosted copy).
NFHL_ENDPOINT: str = os.environ.get(
    "GRAVEL_NFHL_ENDPOINT",
    "https://hazards.fema.gov/arcgis/rest/services/public/NFHL/MapServer",
)
NFHL_FLOOD_ZONE_LAYER: int = 28  # "Flood Hazard Zones" (S_FLD_HAZ_AR)

# FLD_ZONE -> RGBA fill for a map risk layer (deep red = coastal high hazard, red =
# 1%-annual SFHA, amber = 0.2%-annual fringe, green = minimal). An illustrative
# cartographic ramp for visualization — NOT official FEMA symbology.
NFHL_ZONE_COLORS: dict[str, list[int]] = {
    "V": [178, 24, 43, 150], "VE": [178, 24, 43, 150],
    "A": [214, 96, 77, 120], "AE": [214, 96, 77, 120], "AH": [214, 96, 77, 120],
    "AO": [214, 96, 77, 120], "AR": [214, 96, 77, 120], "A99": [214, 96, 77, 120],
    "0.2 PCT ANNUAL CHANCE FLOOD HAZARD": [244, 165, 130, 90],
    "X": [26, 150, 65, 55], "AREA NOT INCLUDED": [150, 150, 150, 40],
}
NFHL_DEFAULT_ZONE_COLOR: list[int] = [180, 180, 180, 60]


def nfhl_zone_color(zone: str) -> list[int]:
    """RGBA fill for a FEMA flood-zone code (see :data:`NFHL_ZONE_COLORS`)."""
    return NFHL_ZONE_COLORS.get(str(zone), NFHL_DEFAULT_ZONE_COLOR)


def fetch_nfhl_flood_zones(
    bbox: tuple[float, float, float, float],
    *,
    endpoint: str | None = None,
    layer: int = NFHL_FLOOD_ZONE_LAYER,
    where: str = "1=1",
    out_fields: str = "FLD_ZONE,ZONE_SUBTY",
    timeout: float = 60.0,
    page_size: int = 100,
) -> gpd.GeoDataFrame:
    """Fetch FEMA NFHL flood-hazard polygons for a bounding box as a ``GeoDataFrame``.

    Queries the NFHL ArcGIS MapServer (paginated) and returns flood-zone polygons in
    EPSG:4326 with a ``FLD_ZONE`` column — ready to hand to
    :func:`flood_edge_probabilities` or to draw as a map risk layer.

    Parameters
    ----------
    bbox : (min_lon, min_lat, max_lon, max_lat)
        Query envelope in WGS84 (lon/lat).
    endpoint : str, optional
        NFHL MapServer URL. Defaults to :data:`NFHL_ENDPOINT` (the ``GRAVEL_NFHL_ENDPOINT``
        environment variable, else FEMA's public service). Pass this to use a mirror or a
        self-hosted copy.
    layer : int, optional
        Feature-layer id (default 28, "Flood Hazard Zones").
    where, out_fields, timeout, page_size
        ArcGIS query knobs. ``page_size`` is the per-request feature cap (default 100 —
        NFHL rejects large GeoJSON polygon pages with HTTP 500, so the fetcher also halves
        the page size and retries on a 500). Pagination follows ``exceededTransferLimit``.

    Returns
    -------
    geopandas.GeoDataFrame
        Flood-zone polygons (EPSG:4326). Empty frame if the bbox has no mapped zones.

    Notes
    -----
    Requires geopandas (``pip install gravel-fragility[interop]``). Uses only the standard
    library for HTTP, so no extra networking dependency.
    """
    try:
        import geopandas as gpd
        import pandas as pd
    except ImportError as exc:  # pragma: no cover - environment dependent
        raise ImportError(
            "fetch_nfhl_flood_zones needs geopandas: pip install gravel-fragility[interop]"
        ) from exc

    base = (endpoint or NFHL_ENDPOINT).rstrip("/")
    query_url = f"{base}/{layer}/query"
    minx, miny, maxx, maxy = (float(v) for v in bbox)

    frames = []
    offset = 0
    page = int(page_size)
    while True:
        params = {
            "where": where,
            "geometry": f"{minx},{miny},{maxx},{maxy}",
            "geometryType": "esriGeometryEnvelope",
            "inSR": "4326", "outSR": "4326",
            "spatialRel": "esriSpatialRelIntersects",
            "outFields": out_fields,
            "returnGeometry": "true",
            "resultOffset": str(offset),
            "resultRecordCount": str(page),
            "f": "geojson",
        }
        req = Request(f"{query_url}?{urlencode(params)}",
                      headers={"User-Agent": "gravel-fragility"})
        try:
            with urlopen(req, timeout=timeout) as resp:  # noqa: S310 - fixed https endpoint
                payload = json.loads(resp.read().decode("utf-8"))
        except HTTPError as exc:
            # NFHL 500s when a GeoJSON page exceeds its response-size limit (flood
            # polygons are large). Back off the page size and retry the same offset.
            if exc.code == 500 and page > 20:
                page = max(20, page // 2)
                continue
            raise
        features = payload.get("features", [])
        if features:
            frames.append(gpd.GeoDataFrame.from_features(features, crs="EPSG:4326"))
            offset += len(features)
        if not features or not payload.get("exceededTransferLimit"):
            break

    if not frames:
        return gpd.GeoDataFrame({"FLD_ZONE": []}, geometry=[], crs="EPSG:4326")
    return gpd.GeoDataFrame(pd.concat(frames, ignore_index=True), crs="EPSG:4326")


def _edge_index_map(graph: Graph) -> tuple[dict[tuple[int, int], int], int]:
    """Map each directed CSR edge ``(u, v)`` to its edge index; also return m."""
    sources, targets, _ = graph.to_coo()
    edge_index = {
        (int(u), int(v)): e for e, (u, v) in enumerate(zip(sources, targets, strict=True))
    }
    return edge_index, int(targets.shape[0])


def hazard_edge_probabilities(
    graph: Graph,
    zones: Iterable[tuple[Polygon, float]],
    *,
    baseline: float = 0.0,
) -> np.ndarray:
    """Per-edge failure probability from ``(polygon, probability)`` hazard zones.

    An edge receives a zone's probability when *both* of its endpoints fall inside
    that zone's polygon (matching :func:`gravel.edges_in_polygon`). Where zones
    overlap, the **maximum** probability wins.

    Parameters
    ----------
    graph : gravel.Graph
        The network. Edge order is CSR (aligned with :meth:`Graph.to_coo`).
    zones : iterable of (gravel.Polygon, float)
        Hazard footprints paired with per-edge failure probabilities in ``[0, 1]``.
    baseline : float, optional
        Probability assigned to edges that fall in no zone (default ``0.0``: those
        edges never fail).

    Returns
    -------
    numpy.ndarray
        Shape ``(graph.edge_count,)``, dtype ``float64``, in CSR edge order. Feed
        directly to :func:`gravel.stochastic_fragility` as ``edge_probabilities``.
    """
    edge_index, m = _edge_index_map(graph)
    probs = np.full(m, float(baseline), dtype=np.float64)
    # Apply zones from lowest to highest probability so the max effectively wins.
    for polygon, prob in sorted(zones, key=lambda z: float(z[1])):
        prob = float(prob)
        for u, v in edges_in_polygon(graph, polygon):
            e = edge_index.get((int(u), int(v)))
            if e is not None and prob > probs[e]:
                probs[e] = prob
    return probs


def _rings_from_geometry(geom) -> Iterable[Polygon]:
    """Yield gravel.Polygon exterior rings (lat/lon) from a shapely (Multi)Polygon."""
    parts = geom.geoms if geom.geom_type == "MultiPolygon" else [geom]
    for part in parts:
        if part.is_empty:
            continue
        poly = Polygon()
        # shapely stores (x=lon, y=lat); gravel.Coord is (lat, lon).
        poly.vertices = [Coord(float(y), float(x)) for x, y in part.exterior.coords]
        yield poly


def flood_edge_probabilities(
    graph: Graph,
    flood_zones: gpd.GeoDataFrame,
    *,
    zone_field: str = "FLD_ZONE",
    zone_probabilities: dict[str, float] | None = None,
    baseline: float = 0.0,
    default_probability: float | None = None,
) -> np.ndarray:
    """Per-edge failure probability from a FEMA NFHL flood-zone ``GeoDataFrame``.

    Reprojects the layer to WGS84, maps each polygon's flood-zone code to a
    probability, and marks the graph edges inside it (via
    :func:`hazard_edge_probabilities`). See the module docstring for the
    event-vs-annualized probability distinction and the accuracy caveats.

    Parameters
    ----------
    graph : gravel.Graph
    flood_zones : geopandas.GeoDataFrame
        NFHL flood polygons. A ``geometry`` column plus a flood-zone code column
        (``zone_field``) are required.
    zone_field : str, optional
        Column holding the flood-zone code (default ``"FLD_ZONE"``).
    zone_probabilities : dict[str, float], optional
        Flood-zone code -> probability. Defaults to :data:`NFHL_EVENT_CLOSURE`
        (design-flood scenario). Pass :data:`NFHL_ANNUAL_PROBABILITY` for annualized
        risk, or your own calibrated table.
    baseline : float, optional
        Probability for edges outside every mapped zone (default ``0.0``).
    default_probability : float, optional
        Probability for polygons whose code is absent from ``zone_probabilities``.
        ``None`` (default) skips them.

    Returns
    -------
    numpy.ndarray
        Shape ``(graph.edge_count,)``, dtype ``float64``, CSR edge order.

    Raises
    ------
    ImportError
        If geopandas is not installed (``pip install gravel-fragility[interop]``).
    """
    if zone_probabilities is None:
        zone_probabilities = NFHL_EVENT_CLOSURE

    try:
        import geopandas  # noqa: F401
    except ImportError as exc:  # pragma: no cover - exercised only without the dep
        raise ImportError(
            "flood_edge_probabilities requires geopandas — install with "
            "`pip install gravel-fragility[interop]`."
        ) from exc

    gdf = flood_zones
    if gdf.crs is not None and gdf.crs.to_epsg() != 4326:
        gdf = gdf.to_crs(4326)

    zones: list[tuple[Polygon, float]] = []
    for geom, code in zip(gdf.geometry, gdf[zone_field], strict=True):
        if geom is None or geom.is_empty:
            continue
        prob = zone_probabilities.get(str(code).strip(), default_probability)
        if prob is None or prob <= 0.0:
            continue
        for ring in _rings_from_geometry(geom):
            zones.append((ring, float(prob)))

    return hazard_edge_probabilities(graph, zones, baseline=baseline)
