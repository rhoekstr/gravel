"""U.S. Drought Monitor (``gravel.datasets.usdm``).

Fetch weekly USDM drought polygons (drought categories D0-D4) from the NDMC
ArcGIS Feature Service, and map drought category to per-edge failure
probabilities for ``gravel.stochastic_fragility``.

**Drought is a weak road-failure hazard — read before trusting output.** Unlike a
flood, drought does not directly close roads. Any road effect is *secondary and
correlated*: subsidence and cracking on expansive/organic soils, culvert and
low-water-crossing issues once drought breaks, wildfire-burn-scar debris flows,
unpaved-road degradation. :data:`DROUGHT_FAILURE` therefore ships as an
**illustrative, demonstrative, sweepable** category->probability table — it is
*not* an empirically calibrated or authoritative closure model, and the default
probabilities are deliberately tiny. Treat it as a scaffold for your own
scenario, not a published rate.

Version resolution: the USDM "valid date" is always a **Tuesday** (data valid
through Tuesday 7:00 a.m. Eastern; the map is released the following Thursday). A
call to :func:`fetch` with any date snaps to the Tuesday of its USDM week — a
Monday input belongs to the *prior* published week, so it snaps back to the prior
Tuesday — and the resolved Tuesday is recorded in ``Provenance.resolved_version``.

Provenance / attribution (required by NDMC): "The U.S. Drought Monitor is jointly
produced by the National Drought Mitigation Center at the University of
Nebraska-Lincoln, the United States Department of Agriculture, the National
Oceanic and Atmospheric Administration and the National Aeronautics and Space
Administration. Map courtesy of NDMC." Free to use, mandatory attribution.

Endpoint note: the NDMC ArcGIS service (:data:`ENDPOINT`) serves the **current
week only** — it has no historical archive. Passing a past ``date`` still snaps
and stamps that week, but the service returns its latest release; for dated
historical maps use the NDMC dated shapefiles under
``droughtmonitor.unl.edu/data/shapefiles_m/``. The category field ``DM`` is
identical across the GeoJSON, the shapefile, and this service.
"""

from __future__ import annotations

import os
from typing import TYPE_CHECKING

from ._arcgis import query_layer
from ._hazard import edge_probabilities_from_frame
from ._provenance import Provenance

if TYPE_CHECKING:  # pragma: no cover - typing only

    pass


# USDM drought category (``DM`` integer 0-4) -> per-week road-failure probability.
# ILLUSTRATIVE / DEMONSTRATIVE ONLY — drought does not directly close roads; these
# stand in for weak secondary effects (soil movement, burn-scar debris flow,
# unpaved-road degradation). NOT calibrated, NOT authoritative. Sweep them.
DROUGHT_FAILURE: dict[str, float] = {
    "0": 0.001,  # D0 Abnormally Dry
    "1": 0.002,  # D1 Moderate Drought
    "2": 0.005,  # D2 Severe Drought
    "3": 0.010,  # D3 Extreme Drought
    "4": 0.020,  # D4 Exceptional Drought
}

# ``DM`` integer -> USDM code, for a human-readable ``category`` column.
DM_LABEL: dict[int, str] = {0: "D0", 1: "D1", 2: "D2", 3: "D3", 4: "D4"}

# NDMC USDM ArcGIS FeatureServer. Override with GRAVEL_USDM_ENDPOINT, or pass
# endpoint=. NOTE: current-week only (no historical archive) — see module docstring.
ENDPOINT: str = os.environ.get(
    "GRAVEL_USDM_ENDPOINT",
    "https://services5.arcgis.com/0OTVzJS4K09zlixn/arcgis/rest/services"
    "/USDM_current/FeatureServer",
)
DROUGHT_LAYER: int = 0  # "USDM current"
CATEGORY_FIELD: str = "DM"  # integer 0-4; identical in GeoJSON and shapefile

# ``DM`` integer -> RGBA fill for a map risk layer. Illustrative ramp (USDM tan ->
# dark red), NOT official symbology.
DM_COLORS: dict[int, list[int]] = {
    0: [255, 255, 0, 90],    # D0 pale yellow
    1: [252, 211, 127, 120],  # D1 tan
    2: [255, 170, 0, 140],   # D2 orange
    3: [230, 0, 0, 160],     # D3 red
    4: [115, 0, 0, 180],     # D4 dark red
}
_DEFAULT_DM_COLOR: list[int] = [180, 180, 180, 60]


def dm_color(dm) -> list[int]:
    """RGBA fill for a USDM drought category ``DM`` (0-4; see :data:`DM_COLORS`)."""
    try:
        key = int(dm)
    except (TypeError, ValueError):
        return _DEFAULT_DM_COLOR
    return DM_COLORS.get(key, _DEFAULT_DM_COLOR)


def _snap_to_valid_tuesday(date):  # -> pandas.Timestamp (lazy import)
    """Snap ``date`` to its USDM valid date — the Tuesday of that published week.

    USDM valid dates are Tuesdays. A Monday belongs to the *prior* published week,
    so it snaps back to the prior Tuesday (not forward). Any other weekday snaps
    back to the most recent Tuesday.
    """
    import pandas as pd

    d = pd.Timestamp(date).normalize()
    # weekday(): Mon=0, Tue=1, ... Sun=6. Days since the most recent Tuesday.
    back = (d.weekday() - 1) % 7  # Tue->0, Wed->1, ..., Mon->6
    return d - pd.Timedelta(days=back)


def fetch(
    date,
    *,
    endpoint: str | None = None,
    layer: int = DROUGHT_LAYER,
    where: str = "1=1",
    out_fields: str = "DM",
    timeout: float = 60.0,
    page_size: int = 100,
):
    """Fetch USDM weekly drought polygons for ``date``. Returns ``(GeoDataFrame, Provenance)``.

    ``date`` is any date-like value; it is snapped to its USDM **valid date** (the
    Tuesday of that published week — a Monday snaps back to the prior Tuesday), and
    the resolved Tuesday is recorded in ``Provenance.resolved_version`` (as
    ``YYYY-MM-DD``). Polygons come back in EPSG:4326 with an integer ``DM`` column
    (0-4 = D0..D4) plus a derived ``category`` ("D0".."D4") — ready for
    :func:`edge_probabilities` or a map risk layer. Requires geopandas. Endpoint is
    overridable via the ``endpoint=`` argument or the ``GRAVEL_USDM_ENDPOINT``
    environment variable.

    Note: the default :data:`ENDPOINT` (NDMC ArcGIS ``USDM_current``) serves only
    the current week and has no historical archive, so for a past ``date`` it
    returns the latest release while still stamping the requested week. For dated
    historical maps, fetch the NDMC ``shapefiles_m`` archive directly.
    """
    ep = (endpoint or ENDPOINT).rstrip("/")
    valid_tuesday = _snap_to_valid_tuesday(date)
    gdf = query_layer(
        ep, layer, where=where, out_fields=out_fields,
        timeout=timeout, page_size=page_size,
    )
    if CATEGORY_FIELD in gdf.columns:
        gdf["category"] = [
            DM_LABEL.get(int(v), None) if v is not None else None
            for v in gdf[CATEGORY_FIELD]
        ]
    prov = Provenance.stamp(
        "usdm", f"{ep}/{layer}/query", valid_tuesday.date().isoformat()
    )
    return gdf, prov


def edge_probabilities(
    graph,
    footprint,
    *,
    category_field: str = CATEGORY_FIELD,
    category_probabilities: dict[str, float] | None = None,
    baseline: float = 0.0,
    default_probability: float | None = None,
):
    """Per-edge failure probability from a USDM drought ``GeoDataFrame``.

    Maps each polygon's drought category (the integer ``DM`` field, 0-4) to a
    probability (default :data:`DROUGHT_FAILURE` — **illustrative only**; drought
    does not directly close roads) and marks the graph edges inside it. Returns a
    float64 array in CSR edge order for :func:`gravel.stochastic_fragility`.

    The table is keyed by the ``DM`` value as a string ("0".."4"), matching how the
    shared overlay stringifies codes — identical whether the frame came from the
    GeoJSON, the shapefile, or the ArcGIS service.
    """
    table = (
        category_probabilities
        if category_probabilities is not None
        else DROUGHT_FAILURE
    )
    return edge_probabilities_from_frame(
        graph, footprint,
        code_field=category_field, code_probabilities=table,
        baseline=baseline, default_probability=default_probability,
    )
