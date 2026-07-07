"""USGS ShakeMap MMI shaking-intensity polygons (``gravel.datasets.shakemap``).

Fetch Modified Mercalli Intensity (MMI) contour-band polygons from a USGS
ShakeMap product — addressed through the ANSS Comprehensive Catalog (ComCat) /
FDSN event web service — and map MMI bands to per-edge closure probabilities for
``gravel.stochastic_fragility``.

Unlike the ArcGIS-served fetchers (NFHL, USDM, NRI), ShakeMap is reached over the
USGS ComCat API with standard-library HTTP: resolve/search an event, pick a
ShakeMap product version, download its ``shape.zip``, and read the MMI polygons
from ``mi.shp`` (intensity in the ``PARAMVALUE`` column).

Probability semantics (read before trusting output): ``stochastic_fragility``
treats ``prob[e]`` as P(edge fails in one Monte-Carlo run). :data:`MMI_CLOSURE`
(default) = P(road impassable | shaking of this MMI band occurs) — an
**illustrative, sweepable** default, **NOT** a USGS-published or engineering
damage rate. Shaking is a *model* of the field, not a unique solution; a ShakeMap
is provided as-is (USGS disclaimer). Bands map from the MMI value, so higher
intensity -> higher illustrative closure probability.
"""

from __future__ import annotations

import io
import json
import os
import shutil
import tempfile
import time
import zipfile
from typing import TYPE_CHECKING
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from ._hazard import edge_probabilities_from_frame
from ._provenance import Provenance

if TYPE_CHECKING:  # pragma: no cover - typing only
    import geopandas as gpd  # noqa: F401 - used in annotations
    import numpy as np  # noqa: F401 - used in annotations


# Modified Mercalli Intensity band (floor of the polygon's MMI value) -> per-event
# road-closure probability when shaking of that intensity occurs. ILLUSTRATIVE,
# sweepable defaults — NOT USGS-published rates and NOT an engineering fragility
# curve. MMI runs ~1 (not felt) to ~10+ (extreme); banding rises monotonically.
MMI_CLOSURE: dict[int, float] = {
    1: 0.00,   # I    — not felt
    2: 0.00,   # II   — weak
    3: 0.01,   # III  — weak
    4: 0.03,   # IV   — light
    5: 0.08,   # V    — moderate
    6: 0.20,   # VI   — strong (nonstructural damage begins)
    7: 0.45,   # VII  — very strong (moderate damage)
    8: 0.70,   # VIII — severe (heavy damage)
    9: 0.88,   # IX   — violent
    10: 0.95,  # X+   — extreme
}

_UA = {"User-Agent": "gravel-fragility"}

# USGS ComCat / FDSN event service. Override with GRAVEL_SHAKEMAP_ENDPOINT, or
# pass endpoint=. This is the FDSN event query used for both event search
# (bbox/time) and event-detail (products) lookups.
ENDPOINT: str = os.environ.get(
    "GRAVEL_SHAKEMAP_ENDPOINT",
    "https://earthquake.usgs.gov/fdsnws/event/1/query",
)

# The shapefile inside shape.zip that carries the MMI polygons, and its columns.
_SHAPE_ZIP_KEY: str = "download/shape.zip"
_MI_SHAPEFILE: str = "mi.shp"
_MMI_FIELD: str = "PARAMVALUE"  # MMI intensity value on each polygon (float).

# MMI band -> RGBA fill for a map shaking layer. Illustrative ramp (USGS-ish
# green->red intensity scale) — NOT the official ShakeMap symbology.
MMI_COLORS: dict[int, list[int]] = {
    1: [255, 255, 255, 40], 2: [191, 204, 255, 60], 3: [160, 230, 255, 70],
    4: [128, 255, 255, 80], 5: [122, 255, 147, 90], 6: [255, 255, 0, 110],
    7: [255, 200, 0, 130], 8: [255, 145, 0, 150], 9: [255, 0, 0, 170],
    10: [200, 0, 0, 190],
}
_DEFAULT_MMI_COLOR: list[int] = [180, 180, 180, 60]


def _mmi_band(mmi) -> int:
    """The integer MMI band (floor, clamped to 1..10) for a float intensity."""
    try:
        band = int(float(mmi))
    except (TypeError, ValueError):
        return 1
    return max(1, min(10, band))


def mmi_color(mmi) -> list[int]:
    """RGBA fill for an MMI intensity value (see :data:`MMI_COLORS`)."""
    return MMI_COLORS.get(_mmi_band(mmi), _DEFAULT_MMI_COLOR)


def _get(url: str, *, timeout: float, retries: int = 4, backoff: float = 1.0) -> bytes:
    """HTTP GET with simple exponential backoff on 429 / transient errors.

    ComCat rate-limits automated use with HTTP 429; retry a handful of times with
    growing sleeps (honoring ``Retry-After`` when present) before giving up.
    """
    last_exc: Exception | None = None
    for attempt in range(retries):
        req = Request(url, headers=_UA)
        try:
            with urlopen(req, timeout=timeout) as resp:  # noqa: S310 - fixed https endpoint
                return resp.read()
        except HTTPError as exc:
            last_exc = exc
            if exc.code == 429 and attempt < retries - 1:
                retry_after = exc.headers.get("Retry-After") if exc.headers else None
                try:
                    wait = float(retry_after) if retry_after else backoff * (2 ** attempt)
                except ValueError:
                    wait = backoff * (2 ** attempt)
                time.sleep(wait)
                continue
            raise
        except URLError as exc:
            last_exc = exc
            if attempt < retries - 1:
                time.sleep(backoff * (2 ** attempt))
                continue
            raise
    if last_exc is not None:  # pragma: no cover - loop always raises or returns
        raise last_exc
    raise RuntimeError("unreachable")  # pragma: no cover


def _query(endpoint: str, params: dict, *, timeout: float) -> dict:
    """GET the FDSN event service and parse the GeoJSON response."""
    url = f"{endpoint}?{urlencode(params)}"
    return json.loads(_get(url, timeout=timeout).decode("utf-8"))


def _detail(endpoint: str, event_id: str, *, timeout: float) -> dict:
    """The event-detail GeoJSON for ``event_id`` (carries ``properties.products``)."""
    return _query(
        endpoint, {"eventid": event_id, "format": "geojson"}, timeout=timeout
    )


def _shakemap_products(detail: dict) -> list[dict]:
    """The non-DELETE ShakeMap product entries from an event-detail feed.

    Reads the product array **defensively**: a missing ``shakemap`` key (an event
    with no ShakeMap) yields an empty list rather than raising ``KeyError``.
    """
    products = detail.get("properties", {}).get("products", {})
    shakemaps = products.get("shakemap", []) if isinstance(products, dict) else []
    return [p for p in shakemaps if p.get("status") != "DELETE"]


def _select_shakemap(detail: dict, *, version=None, source: str | None = None) -> dict:
    """Pick one ShakeMap product: preferred by default, or a specific version/source.

    ``version=None`` -> the preferred/latest product (highest ``preferredWeight``,
    which ComCat sorts first). ``source=`` filters to one contributing network
    (``ci``, ``us``, ``atlas``, ...). ``version=`` filters on the per-source
    ``properties.version`` string (ambiguous without a ``source`` — ties break by
    ``preferredWeight``). Raises ``ValueError`` if nothing matches.
    """
    candidates = _shakemap_products(detail)
    if not candidates:
        raise ValueError("no ShakeMap product for this event")
    if source is not None:
        candidates = [p for p in candidates if p.get("source") == source]
    if version is not None:
        candidates = [
            p
            for p in candidates
            if p.get("properties", {}).get("version") == str(version)
        ]
    if not candidates:
        raise ValueError(
            f"no ShakeMap product matching version={version!r}, source={source!r}"
        )
    return max(candidates, key=lambda p: p.get("preferredWeight", 0))


def available_versions(event_id: str, *, endpoint: str | None = None,
                       timeout: float = 60.0) -> list[dict]:
    """List a ComCat event's ShakeMap versions (for choosing ``version=``/``source=``).

    Returns one dict per non-DELETE product with ``source``, ``version``
    (per-source string), ``preferred_weight``, ``update_time`` (epoch ms), and
    ``status`` — sorted preferred-first, matching what :func:`fetch` selects.
    """
    ep = endpoint or ENDPOINT
    products = _shakemap_products(_detail(ep, event_id, timeout=timeout))
    products.sort(key=lambda p: p.get("preferredWeight", 0), reverse=True)
    return [
        {
            "source": p.get("source"),
            "version": p.get("properties", {}).get("version"),
            "preferred_weight": p.get("preferredWeight"),
            "update_time": p.get("updateTime"),
            "status": p.get("status"),
        }
        for p in products
    ]


def _read_mi_polygons(zip_bytes: bytes) -> gpd.GeoDataFrame:
    """Extract ``shape.zip`` to a unique temp dir and read ``mi.shp`` as EPSG:4326.

    Returns a two-column frame ``[mmi, geometry]`` in WGS84. The temp dir is
    unique per call (``tempfile.mkdtemp``) and cleaned up before returning, so
    concurrent fetches never collide on a fixed path.
    """
    import geopandas as gpd

    tmp = tempfile.mkdtemp(prefix="gravel_shakemap_")
    try:
        with zipfile.ZipFile(io.BytesIO(zip_bytes)) as archive:
            archive.extractall(tmp)
        gdf = gpd.read_file(os.path.join(tmp, _MI_SHAPEFILE))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    gdf = gdf.rename(columns={_MMI_FIELD: "mmi"})
    keep = [c for c in ("mmi", "geometry") if c in gdf.columns]
    gdf = gdf[keep]
    # mi.prj is GCS_WGS_1984, but stamp it explicitly in case the .prj is absent.
    if gdf.crs is None:
        gdf = gdf.set_crs(4326)
    elif gdf.crs.to_epsg() != 4326:
        gdf = gdf.to_crs(4326)
    return gdf


def _fetch_one(endpoint: str, event_id: str, *, version, source: str | None,
               timeout: float) -> tuple[gpd.GeoDataFrame, dict]:
    """Fetch MMI polygons for a single resolved event. Returns ``(gdf, product)``."""
    product = _select_shakemap(
        _detail(endpoint, event_id, timeout=timeout), version=version, source=source
    )
    contents = product.get("contents", {})
    if _SHAPE_ZIP_KEY not in contents:
        raise ValueError(
            f"ShakeMap product for {event_id} has no {_SHAPE_ZIP_KEY} "
            "(MMI polygons unavailable for this version)"
        )
    # Use the exact URL ComCat gives us — hand-built product URLs 403.
    url = contents[_SHAPE_ZIP_KEY]["url"]
    gdf = _read_mi_polygons(_get(url, timeout=timeout))
    gdf.attrs["shakemap"] = {
        "event_id": event_id,
        "source": product.get("source"),
        "version": product.get("properties", {}).get("version"),
        "update_time": product.get("updateTime"),
    }
    return gdf, product


def _resolved_version(event_id: str, product: dict) -> str:
    """A citable ``event_id/source/version`` string for the Provenance stamp."""
    src = product.get("source", "?")
    ver = product.get("properties", {}).get("version", "?")
    return f"{event_id}/{src}/v{ver}"


def fetch(
    event_id: str | None = None,
    *,
    version=None,
    source: str | None = None,
    bbox: tuple[float, float, float, float] | None = None,
    starttime: str | None = None,
    endtime: str | None = None,
    minmagnitude: float | None = None,
    endpoint: str | None = None,
    limit: int | None = None,
    timeout: float = 60.0,
):
    """Fetch USGS ShakeMap MMI polygons. Returns ``(GeoDataFrame, Provenance)``.

    Two modes:

    * **By event** — ``fetch(event_id="ci38457511")`` resolves one ComCat event.
      ``version=None`` (default) takes the preferred/latest ShakeMap product;
      pass ``source=`` (e.g. ``"us"``) and/or ``version=`` for a specific one (see
      :func:`available_versions`). Returns the MMI polygons plus a
      :class:`Provenance` whose ``resolved_version`` is
      ``"{event_id}/{source}/v{version}"``.

    * **By search** — ``fetch(bbox=(min_lon, min_lat, max_lon, max_lat),
      starttime=..., endtime=..., minmagnitude=...)`` finds events (FDSN has no
      ``bbox`` param — this maps to ``min/max`` ``latitude``/``longitude``) and
      returns ``(dict[event_id -> GeoDataFrame], Provenance)``. Events without a
      usable ShakeMap are **skipped**, not fatal. At least one of ``bbox`` /
      ``starttime`` / ``endtime`` must be given; ``bbox`` may be omitted for a
      time-only search.

    Polygons come back in EPSG:4326 with an ``mmi`` column — ready for
    :func:`edge_probabilities` or a map shaking layer. Requires geopandas.
    Endpoint is overridable via ``endpoint=`` or ``GRAVEL_SHAKEMAP_ENDPOINT``.
    """
    ep = endpoint or ENDPOINT

    if event_id is not None:
        gdf, product = _fetch_one(
            ep, event_id, version=version, source=source, timeout=timeout
        )
        prov = Provenance.stamp("shakemap", ep, _resolved_version(event_id, product))
        return gdf, prov

    # Search mode: at least one temporal or spatial constraint is required.
    if bbox is None and starttime is None and endtime is None:
        raise ValueError(
            "fetch() needs event_id=, or a search constraint "
            "(bbox= and/or starttime=/endtime=)"
        )

    params: dict[str, str] = {"format": "geojson", "orderby": "time"}
    if bbox is not None:
        min_lon, min_lat, max_lon, max_lat = bbox
        params.update(
            {
                "minlatitude": str(min_lat),
                "maxlatitude": str(max_lat),
                "minlongitude": str(min_lon),
                "maxlongitude": str(max_lon),
            }
        )
    if starttime is not None:
        params["starttime"] = str(starttime)
    if endtime is not None:
        params["endtime"] = str(endtime)
    if minmagnitude is not None:
        params["minmagnitude"] = str(minmagnitude)
    if limit is not None:
        params["limit"] = str(limit)

    summary = _query(ep, params, timeout=timeout)
    event_ids = [f.get("id") for f in summary.get("features", []) if f.get("id")]

    results: dict[str, gpd.GeoDataFrame] = {}
    resolved: list[str] = []
    for eid in event_ids:
        try:
            gdf, product = _fetch_one(
                ep, eid, version=version, source=source, timeout=timeout
            )
        except (ValueError, KeyError, HTTPError, URLError, zipfile.BadZipFile, OSError):
            # No ShakeMap / no shape.zip / transient fetch failure / corrupt or
            # unreadable shape.zip for this event — skip it rather than crashing
            # the whole batch.
            continue
        results[eid] = gdf
        resolved.append(_resolved_version(eid, product))

    resolved_version = ";".join(resolved) if resolved else "none"
    prov = Provenance.stamp("shakemap", ep, resolved_version)
    return results, prov


def edge_probabilities(
    graph,
    footprint,
    *,
    mmi_field: str = "mmi",
    mmi_probabilities: dict[int, float] | None = None,
    baseline: float = 0.0,
    default_probability: float | None = None,
):
    """Per-edge closure probability from a ShakeMap MMI ``GeoDataFrame``.

    Bands each polygon's MMI value (``mmi_field``, floored to an integer 1..10)
    through the intensity->probability table (default :data:`MMI_CLOSURE`;
    illustrative, sweepable, **not** authoritative) and marks the graph edges
    inside it. Returns a float64 array in CSR edge order for
    :func:`gravel.stochastic_fragility`. Requires geopandas.

    Overlapping bands take the **maximum** probability (higher shaking wins).
    Polygons whose MMI band is absent from the table take ``default_probability``
    (``None`` skips them).
    """
    table = mmi_probabilities if mmi_probabilities is not None else MMI_CLOSURE
    # _hazard maps by string code; band the float MMI into a "1".."10" key so the
    # shared frame-overlay path can look it up.
    band_table = {str(int(k)): float(v) for k, v in table.items()}

    footprint = footprint.copy()
    footprint["_mmi_band"] = [
        str(_mmi_band(v)) for v in footprint[mmi_field]
    ]
    return edge_probabilities_from_frame(
        graph,
        footprint,
        code_field="_mmi_band",
        code_probabilities=band_table,
        baseline=baseline,
        default_probability=default_probability,
    )
