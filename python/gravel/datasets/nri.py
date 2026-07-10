"""FEMA National Risk Index (``gravel.datasets.nri``).

Fetch NRI risk polygons (county or census tract) from FEMA's ArcGIS
FeatureServer, and map the composite Risk Index rating to per-edge failure
probabilities for ``gravel.stochastic_fragility``.

The NRI is a **baseline annualized risk surface**, not an event footprint: each
polygon carries a composite Risk Index score/rating (``RISK_SCORE`` /
``RISK_RATNG``) synthesized from Expected Annual Loss, Social Vulnerability, and
Community Resilience across 18 natural hazards. Treat what it drives as a
*standing* per-edge risk, not the consequence of one dated event.

Probability semantics (read before trusting output): ``stochastic_fragility``
treats ``prob[e]`` as P(edge fails in one Monte-Carlo run). Because the NRI is
annualized, a "run" here is best read as **one year** of exposure at the mapped
risk level. :data:`RISK_CLOSURE` maps the composite Risk Index *rating* to an
illustrative annual failure probability — sweepable, disclosed, and **not** a
FEMA-published rate. The NRI license is planning-only and requires attribution
(see :data:`ATTRIBUTION`); the numbers below are Gravel's, not FEMA's.
"""

from __future__ import annotations

import json as _json
import os
import re
from typing import TYPE_CHECKING
from urllib.error import URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from ._arcgis import query_layer
from ._hazard import edge_probabilities_from_frame
from ._provenance import Provenance

if TYPE_CHECKING:  # pragma: no cover - typing only

    pass


# Composite Risk Index rating -> per-year road-failure probability. ILLUSTRATIVE,
# sweepable defaults keyed on the NRI headline ``RISK_RATNG`` class — NOT
# FEMA-published rates. The NRI is an annualized baseline risk surface, so a
# "run" is read as one year of exposure (see the module docstring). Special
# non-numeric rating flags (see :data:`_NON_RATINGS`) map to no added risk.
RISK_CLOSURE: dict[str, float] = {
    "Very Low": 0.01,
    "Relatively Low": 0.03,
    "Relatively Moderate": 0.07,
    "Relatively High": 0.15,
    "Very High": 0.30,
}

# Non-numeric flags that appear in ``*_RATNG`` in place of a class; the paired
# score may be null/0. Left out of :data:`RISK_CLOSURE` so they take the
# ``default_probability`` (``None`` -> skipped) rather than being coerced.
_NON_RATINGS: frozenset[str] = frozenset(
    {"No Rating", "Insufficient Data", "Not Applicable", "No Expected Annual Losses"}
)

# Geography -> (FeatureServer service name, layer index, ArcGIS item id, page size).
# maxRecordCount is 1000 for counties, 2000 for tracts; page at the cap.
_COUNTY = "National_Risk_Index_Counties"
_TRACT = "National_Risk_Index_Census_Tracts"
_SERVICES: dict[str, tuple[str, int, str, int]] = {
    "county": (_COUNTY, 0, "39485e8035d446a5bff03259508ae355", 1000),
    "tract": (_TRACT, 0, "9da4eeb936544335a6db0cd7a8448a51", 2000),
}

# NRI ArcGIS Online organization base. Override with GRAVEL_NRI_ENDPOINT, or pass
# endpoint=. The service name (per geography) and ``/FeatureServer`` are appended.
ENDPOINT: str = os.environ.get(
    "GRAVEL_NRI_ENDPOINT",
    "https://services.arcgis.com/XG15cJAlne2vxtgt/arcgis/rest/services",
)

# ArcGIS item-JSON host for in-band version resolution (see :func:`_resolve_release`).
_ITEM_JSON = "https://www.arcgis.com/sharing/rest/content/items/{item_id}"

# A scoped default field set: identity/geography + the composite headline fields.
# The county layer carries ~467 fields — never default to ``*`` on a national pull.
# The composite Risk Index exposes only ``RISK_SPCTL`` (state percentile); there is
# no ``RISK_NPCTL`` (national percentile lives on the per-hazard ALR fields), so the
# percentile is left out here rather than substituting the state one. ``CRF_VALUE``
# is the Community Risk Factor value (distinct from ``RESL_VALUE``).
DEFAULT_FIELDS: str = (
    "NRI_ID,NRI_VER,STATE,STATEABBRV,STATEFIPS,COUNTY,COUNTYFIPS,STCOFIPS,"
    "POPULATION,RISK_VALUE,RISK_SCORE,RISK_RATNG,"
    "EAL_VALT,EAL_SCORE,EAL_RATNG,SOVI_SCORE,SOVI_RATNG,"
    "RESL_VALUE,RESL_SCORE,RESL_RATNG,CRF_VALUE"
)

# Required FEMA attribution/disclaimer for any product using the NRI (verbatim
# from the ArcGIS item ``licenseInfo``). Planning purposes only; cite the version.
ATTRIBUTION: str = (
    "This product uses the Federal Emergency Management Agency's National Risk "
    "Index dataset API or downloadable datasets but is not endorsed by FEMA. The "
    "Federal Government or FEMA cannot vouch for the data or analyses derived from "
    "these data after the data have been retrieved from the Agency's website(s)."
)

# Composite Risk Index rating -> RGBA fill for a map risk layer. Illustrative
# sequential ramp (yellow -> dark red) — NOT official NRI symbology.
RISK_COLORS: dict[str, list[int]] = {
    "Very Low": [26, 150, 65, 55],
    "Relatively Low": [166, 217, 106, 90],
    "Relatively Moderate": [255, 195, 77, 110],
    "Relatively High": [244, 109, 67, 140],
    "Very High": [178, 24, 43, 170],
}
_DEFAULT_RISK_COLOR: list[int] = [180, 180, 180, 60]

# Static fallback release label if in-band resolution is unreachable (offline /
# custom endpoint). The live services always serve the current release.
_FALLBACK_RELEASE: str = "December 2025 (1.20.0)"

# ``Version: <label> (<semver>)`` from the item-JSON description first line, e.g.
# "National Risk Index Data Version: December 2025 (1.20.0)".
_VERSION_RE = re.compile(r"Version:\s*([^<(]+?)\s*\(([\d.]+)\)")


def risk_color(rating: str) -> list[int]:
    """RGBA fill for a composite NRI risk rating (see :data:`RISK_COLORS`)."""
    return RISK_COLORS.get(str(rating).strip(), _DEFAULT_RISK_COLOR)


def _resolve_release(item_id: str, frame, *, timeout: float) -> str:
    """Resolve the NRI release label, e.g. ``"December 2025 (1.20.0)"``.

    Prefers the ArcGIS item-JSON description (``Version: <label> (<semver>)``),
    which carries the semantic ``v1.20`` release. Falls back to the in-band
    ``NRI_VER`` column on the returned frame (label only, e.g. "December 2025"),
    then to :data:`_FALLBACK_RELEASE`. Version resolution never fails the fetch.
    """
    if item_id:
        url = f"{_ITEM_JSON.format(item_id=item_id)}?{urlencode({'f': 'json'})}"
        try:
            req = Request(url, headers={"User-Agent": "gravel-fragility"})
            with urlopen(req, timeout=timeout) as resp:  # noqa: S310 - fixed https host
                item = _json.loads(resp.read().decode("utf-8"))
            match = _VERSION_RE.search(item.get("description") or "")
            if match:
                label, semver = match.group(1).strip(), match.group(2).strip()
                return f"{label} ({semver})"
        except (URLError, ValueError, TimeoutError, OSError):
            pass  # fall through to the in-band column
    if frame is not None and len(frame) and "NRI_VER" in frame.columns:
        ver = frame["NRI_VER"].iloc[0]
        if ver is not None and str(ver).strip():
            return str(ver).strip()
    return _FALLBACK_RELEASE


def fetch(
    *,
    geography: str = "county",
    where: str = "1=1",
    endpoint: str | None = None,
    out_fields: str = DEFAULT_FIELDS,
    timeout: float = 120.0,
    page_size: int | None = None,
    max_allowable_offset: float | None = None,
    geometry_precision: int | None = None,
):
    """Fetch NRI risk polygons. Returns ``(GeoDataFrame, Provenance)``.

    ``geography`` is ``"county"`` (3,232 features) or ``"tract"`` (85,154).
    ``where`` is a SQL-92 filter over the layer — e.g. ``"STCOFIPS='37021'"``,
    ``"STATEABBRV='NC'"``, or the default ``"1=1"`` for the whole layer. Polygons
    come back in EPSG:4326 with a ``RISK_RATNG`` column (and the scoped
    :data:`DEFAULT_FIELDS`) — ready for :func:`edge_probabilities` or a map risk
    layer. Requires geopandas.

    The endpoint (the ArcGIS org base; the per-geography service name is appended)
    is overridable via the ``endpoint=`` argument or the ``GRAVEL_NRI_ENDPOINT``
    environment variable. The release version is resolved in-band from the ArcGIS
    item description (e.g. ``"December 2025 (1.20.0)"``) and stamped into
    :attr:`Provenance.resolved_version`; see :func:`_resolve_release`.

    Passing ``out_fields="*"`` pulls every field (~467 for counties) — heavy on a
    national pull; keep it scoped. The NRI is planning-only and requires
    :data:`ATTRIBUTION` on any derived product.
    """
    if geography not in _SERVICES:
        raise ValueError(
            f"unknown geography {geography!r}; expected one of {sorted(_SERVICES)}"
        )
    service, layer, item_id, cap = _SERVICES[geography]
    page = int(page_size) if page_size is not None else cap

    base = (endpoint or ENDPOINT).rstrip("/")
    service_url = f"{base}/{service}/FeatureServer"
    gdf = query_layer(
        service_url, layer, where=where, out_fields=out_fields,
        timeout=timeout, page_size=page,
        max_allowable_offset=max_allowable_offset,
        geometry_precision=geometry_precision,
    )
    release = _resolve_release(item_id, gdf, timeout=timeout)
    prov = Provenance.stamp("nri", f"{service_url}/{layer}/query", release)
    return gdf, prov


def edge_probabilities(
    graph,
    footprint,
    *,
    rating_field: str = "RISK_RATNG",
    rating_probabilities: dict[str, float] | None = None,
    baseline: float = 0.0,
    default_probability: float | None = None,
):
    """Per-edge annual failure probability from an NRI risk ``GeoDataFrame``.

    Maps each polygon's composite Risk Index rating (``RISK_RATNG``) to a
    probability (default :data:`RISK_CLOSURE`) and marks the graph edges inside
    it; where polygons overlap the maximum wins. Ratings absent from the table
    (including the non-numeric flags ``No Rating`` / ``Insufficient Data`` /
    ``Not Applicable`` / ``No Expected Annual Losses``) take
    ``default_probability`` — ``None`` skips them. Returns a float64 array in CSR
    edge order for :func:`gravel.stochastic_fragility`.

    Because the NRI is an **annualized baseline** rather than an event footprint,
    a Monte-Carlo "run" over this array is a year of exposure, not a dated event.
    """
    table = rating_probabilities if rating_probabilities is not None else RISK_CLOSURE
    return edge_probabilities_from_frame(
        graph, footprint,
        code_field=rating_field, code_probabilities=table,
        baseline=baseline, default_probability=default_probability,
    )
