"""Reusable paginated ArcGIS REST feature query (stdlib HTTP -> GeoDataFrame).

Shared by the ArcGIS-served hazard fetchers (NFHL, USDM, NRI). ShakeMap uses the
USGS ComCat API instead. Standard-library HTTP only — the sole extra dependency
is geopandas, for the returned frame.
"""

from __future__ import annotations

import json
from typing import TYPE_CHECKING
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

if TYPE_CHECKING:  # pragma: no cover - typing only
    import geopandas as gpd

_UA = {"User-Agent": "gravel-fragility"}


def query_layer(
    endpoint: str,
    layer: int,
    *,
    bbox: tuple[float, float, float, float] | None = None,
    where: str = "1=1",
    out_fields: str = "*",
    timeout: float = 60.0,
    page_size: int = 100,
    extra_params: dict | None = None,
) -> gpd.GeoDataFrame:
    """Fetch an ArcGIS feature layer as an EPSG:4326 ``GeoDataFrame`` (paginated).

    ``bbox`` = ``(min_lon, min_lat, max_lon, max_lat)`` envelope filter, or ``None``
    for the whole layer subject to ``where``. Pagination follows
    ``exceededTransferLimit``; on HTTP 500 (large-geometry responses) the page size
    is halved and the offset retried. ``extra_params`` merges additional ArcGIS
    query knobs (e.g. a date filter). Requires geopandas.
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
        return gpd.GeoDataFrame(geometry=[], crs="EPSG:4326")
    return gpd.GeoDataFrame(pd.concat(frames, ignore_index=True), crs="EPSG:4326")
