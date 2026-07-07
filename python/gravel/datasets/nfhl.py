"""FEMA National Flood Hazard Layer (``gravel.datasets.nfhl``).

Fetch flood-zone polygons from FEMA's NFHL ArcGIS MapServer, and map flood-zone
codes to per-edge closure probabilities for ``gravel.stochastic_fragility``.

Probability semantics (read before trusting output): ``stochastic_fragility``
treats ``prob[e]`` as P(edge fails in one Monte-Carlo run), so what a "run" means
is a modeling choice. :data:`EVENT_CLOSURE` (default) = P(road impassable | the
mapped design flood occurs) — illustrative, sweepable, and **not** FEMA-published
rates. :data:`ANNUAL_PROBABILITY` = the zone's annual exceedance probability (a
"run" = one year), grounded in the zone definitions.
"""

from __future__ import annotations

import os
from typing import TYPE_CHECKING

from ._arcgis import query_layer
from ._hazard import edge_probabilities_from_frame
from ._provenance import Provenance

if TYPE_CHECKING:  # pragma: no cover - typing only
    import geopandas as gpd
    import numpy as np

    from .._gravel import Graph


# FEMA NFHL flood-zone code -> per-event road-closure probability (design-flood
# scenario). ILLUSTRATIVE, sweepable defaults — NOT FEMA-published closure rates.
EVENT_CLOSURE: dict[str, float] = {
    "A": 0.90, "AE": 0.90, "AH": 0.85, "AO": 0.85, "AR": 0.80, "A99": 0.80,
    "V": 0.95, "VE": 0.95,
    "0.2 PCT ANNUAL CHANCE FLOOD HAZARD": 0.25,
}

# FEMA NFHL flood-zone code -> annual exceedance probability. GROUNDED in the zone
# definitions (SFHA = 1%-annual; shaded-X = 0.2%-annual). Use for a "one year" run.
ANNUAL_PROBABILITY: dict[str, float] = {
    "A": 0.01, "AE": 0.01, "AH": 0.01, "AO": 0.01, "AR": 0.01, "A99": 0.01,
    "V": 0.01, "VE": 0.01,
    "0.2 PCT ANNUAL CHANCE FLOOD HAZARD": 0.002,
}

# NFHL ArcGIS MapServer. Override with GRAVEL_NFHL_ENDPOINT, or pass endpoint=.
ENDPOINT: str = os.environ.get(
    "GRAVEL_NFHL_ENDPOINT",
    "https://hazards.fema.gov/arcgis/rest/services/public/NFHL/MapServer",
)
FLOOD_ZONE_LAYER: int = 28  # "Flood Hazard Zones" (S_FLD_HAZ_AR)

# FLD_ZONE -> RGBA fill for a map risk layer. Illustrative ramp — NOT official symbology.
ZONE_COLORS: dict[str, list[int]] = {
    "V": [178, 24, 43, 150], "VE": [178, 24, 43, 150],
    "A": [214, 96, 77, 120], "AE": [214, 96, 77, 120], "AH": [214, 96, 77, 120],
    "AO": [214, 96, 77, 120], "AR": [214, 96, 77, 120], "A99": [214, 96, 77, 120],
    "0.2 PCT ANNUAL CHANCE FLOOD HAZARD": [244, 165, 130, 90],
    "X": [26, 150, 65, 55], "AREA NOT INCLUDED": [150, 150, 150, 40],
}
_DEFAULT_ZONE_COLOR: list[int] = [180, 180, 180, 60]


def zone_color(zone: str) -> list[int]:
    """RGBA fill for a FEMA flood-zone code (see :data:`ZONE_COLORS`)."""
    return ZONE_COLORS.get(str(zone), _DEFAULT_ZONE_COLOR)


def fetch(
    bbox,
    *,
    endpoint: str | None = None,
    layer: int = FLOOD_ZONE_LAYER,
    where: str = "1=1",
    out_fields: str = "FLD_ZONE,ZONE_SUBTY",
    timeout: float = 60.0,
    page_size: int = 100,
):
    """Fetch NFHL flood-zone polygons for a bbox. Returns ``(GeoDataFrame, Provenance)``.

    ``bbox`` = ``(min_lon, min_lat, max_lon, max_lat)`` in WGS84. Polygons come back
    in EPSG:4326 with a ``FLD_ZONE`` column — ready for :func:`edge_probabilities`
    or a map risk layer. Requires geopandas. Endpoint is overridable via the
    ``endpoint=`` argument or the ``GRAVEL_NFHL_ENDPOINT`` environment variable.
    """
    ep = (endpoint or ENDPOINT).rstrip("/")
    gdf = query_layer(
        ep, layer, bbox=bbox, where=where, out_fields=out_fields,
        timeout=timeout, page_size=page_size,
    )
    prov = Provenance.stamp("nfhl", f"{ep}/{layer}/query", "effective")
    return gdf, prov


def edge_probabilities(
    graph,
    footprint,
    *,
    zone_field: str = "FLD_ZONE",
    zone_probabilities: dict[str, float] | None = None,
    baseline: float = 0.0,
    default_probability: float | None = None,
):
    """Per-edge closure probability from an NFHL flood-zone ``GeoDataFrame``.

    Maps each polygon's flood-zone code to a probability (default
    :data:`EVENT_CLOSURE`; pass :data:`ANNUAL_PROBABILITY` for annualized risk)
    and marks the graph edges inside it. Returns a float64 array in CSR edge order
    for :func:`gravel.stochastic_fragility`.
    """
    table = zone_probabilities if zone_probabilities is not None else EVENT_CLOSURE
    return edge_probabilities_from_frame(
        graph, footprint,
        code_field=zone_field, code_probabilities=table,
        baseline=baseline, default_probability=default_probability,
    )
