"""Reusable paginated ArcGIS REST feature query (stdlib HTTP -> GeoDataFrame).

Shared by the ArcGIS-served hazard fetchers (NFHL, USDM, NRI). ShakeMap uses the
USGS ComCat API instead. Standard-library HTTP only — the sole extra dependency
is geopandas, for the returned frame.
"""

from __future__ import annotations

import json
from http.client import IncompleteRead
from typing import TYPE_CHECKING
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

if TYPE_CHECKING:  # pragma: no cover - typing only
    import geopandas as gpd

_UA = {"User-Agent": "gravel-fragility"}

# The response failures that a smaller page fixes: HTTP 500 (server chokes rendering
# a large-geometry page), and mid-stream truncation / timeout when a single page's
# payload is too big to deliver (survey-grade county polygons run ~2 MB each, so a
# 100-feature page can exceed 200 MB and get cut off).
_SHRINKABLE = (IncompleteRead, TimeoutError, URLError)
_MIN_PAGE = 10


def query_layer(
    endpoint: str,
    layer: int,
    *,
    bbox: tuple[float, float, float, float] | None = None,
    where: str = "1=1",
    out_fields: str = "*",
    timeout: float = 60.0,
    page_size: int = 100,
    max_allowable_offset: float | None = None,
    geometry_precision: int | None = None,
    extra_params: dict | None = None,
) -> gpd.GeoDataFrame:
    """Fetch an ArcGIS feature layer as an EPSG:4326 ``GeoDataFrame`` (paginated).

    ``bbox`` = ``(min_lon, min_lat, max_lon, max_lat)`` envelope filter, or ``None``
    for the whole layer subject to ``where``. Pagination follows
    ``exceededTransferLimit``; when a page is too large to deliver — HTTP 500, or a
    truncated / timed-out read — the page size is halved (down to 10) and that
    offset retried, so a heavy-geometry layer self-heals instead of erroring out.

    ``max_allowable_offset`` (in ``outSR`` units — degrees here) asks the server to
    Douglas–Peucker-simplify geometry before sending (e.g. ``0.005`` ≈ 500 m, plenty
    for a regional choropleth and 10–100× smaller than survey-grade polygons);
    ``geometry_precision`` caps coordinate decimal places. ``extra_params`` merges
    additional ArcGIS query knobs (e.g. a date filter). Requires geopandas.
    """
    try:
        import geopandas as gpd
        import pandas as pd
    except ImportError as exc:  # pragma: no cover - environment dependent
        raise ImportError(
            "ArcGIS fetch needs geopandas: pip install gravel-fragility[datasets]"
        ) from exc

    base = str(endpoint).rstrip("/")
    query_url = f"{base}/{layer}/query"
    frames = []
    offset = 0
    page = int(page_size)
    while True:
        params = {
            "where": where,
            "outFields": out_fields,
            "returnGeometry": "true",
            "outSR": "4326",
            "resultOffset": str(offset),
            "resultRecordCount": str(page),
            "f": "geojson",
        }
        if max_allowable_offset is not None:
            params["maxAllowableOffset"] = str(float(max_allowable_offset))
        if geometry_precision is not None:
            params["geometryPrecision"] = str(int(geometry_precision))
        if bbox is not None:
            minx, miny, maxx, maxy = (float(v) for v in bbox)
            params.update(
                {
                    "geometry": f"{minx},{miny},{maxx},{maxy}",
                    "geometryType": "esriGeometryEnvelope",
                    "inSR": "4326",
                    "spatialRel": "esriSpatialRelIntersects",
                }
            )
        if extra_params:
            params.update(extra_params)
        req = Request(f"{query_url}?{urlencode(params)}", headers=_UA)
        try:
            with urlopen(req, timeout=timeout) as resp:  # noqa: S310 - fixed https endpoint
                payload = json.loads(resp.read().decode("utf-8"))
        except HTTPError as exc:
            if exc.code == 500 and page > _MIN_PAGE:
                page = max(_MIN_PAGE, page // 2)
                continue
            raise
        except _SHRINKABLE:
            if page > _MIN_PAGE:
                page = max(_MIN_PAGE, page // 2)
                continue
            raise
        features = payload.get("features", [])
        got = len(features)
        if got:
            frames.append(gpd.GeoDataFrame.from_features(features, crs="EPSG:4326"))
            offset += got
        # Keep paging while the server hands back a FULL page — that means more rows
        # may remain. `exceededTransferLimit` is only a hint: a server returns it False
        # when the cap was our `resultRecordCount` (not its transfer/size limit), so a
        # full page must continue even when the flag is unset (otherwise a small
        # `page_size`, or simplified small-geometry responses, silently truncate the
        # result). A short or empty page is the real end-of-data signal.
        if got < page:
            break

    if not frames:
        return gpd.GeoDataFrame(geometry=[], crs="EPSG:4326")
    return gpd.GeoDataFrame(pd.concat(frames, ignore_index=True), crs="EPSG:4326")
