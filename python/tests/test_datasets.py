"""Tests for gravel.datasets — catalog / info-pull + hazard fetchers.

The catalog + provenance tests need only the compiled extension. The hazard
edge-probability and fetch tests are skipped when geopandas / shapely are absent.
Network is always mocked (``_arcgis.urlopen``); no test hits a live endpoint.
"""

import importlib
import json as _json

import gravel
import numpy as np
import pytest
from gravel import datasets
from gravel.datasets import _arcgis, _hazard, nfhl
from gravel.datasets._provenance import Provenance


def _maybe_import(name):
    try:
        return importlib.import_module(name)
    except ImportError:
        return None


geopandas = _maybe_import("geopandas")
shapely = _maybe_import("shapely")
requires_geopandas = pytest.mark.skipif(
    geopandas is None or shapely is None, reason="geopandas/shapely not installed"
)


def _square_graph():
    """4 nodes on a unit square (lat, lon): 0=(0,0) 1=(0,1) 2=(1,0) 3=(1,1)."""
    coords = np.array([[0.0, 0.0], [0.0, 1.0], [1.0, 0.0], [1.0, 1.0]], dtype=np.float64)
    src = np.array([0, 1, 0, 2, 1, 3, 2, 3], dtype=np.uint32)
    tgt = np.array([1, 0, 2, 0, 3, 1, 3, 2], dtype=np.uint32)
    w = np.ones(8, dtype=np.float64)
    return gravel.Graph.from_coo(4, src, tgt, w, coords)


def _edge_lookup(g):
    src, tgt, _ = g.to_coo()
    return {(int(u), int(v)): e for e, (u, v) in enumerate(zip(src, tgt, strict=True))}


# --- catalog / info-pull ----------------------------------------------------


def test_catalog_lists_expected_datasets():
    ids = [d.id for d in datasets.list()]
    assert set(ids) == {
        "osm", "tiger", "nfhl", "shakemap", "usdm", "nri",
        "gridsfm", "opfdata", "caida", "openflights", "gtfs", "t100",
    }


def test_info_returns_dataset_and_raises_on_unknown():
    d = datasets.info("shakemap")
    assert d.id == "shakemap"
    assert d.kind == gravel.DatasetKind.HAZARD_OVERLAY
    assert d.domain == gravel.Domain.EARTHQUAKE
    with pytest.raises(KeyError):
        datasets.info("does-not-exist")


def test_dataset_feature_and_temporal_decomposition():
    osm = datasets.info("osm")
    assert osm.has_feature(gravel.Feature.CAPACITY)
    assert "CAPACITY" in osm.feature_names()
    assert not osm.has_feature(gravel.Feature.SEVERITY)
    sm = datasets.info("shakemap")
    assert set(sm.temporal_names()) == {"SNAPSHOT", "HISTORICAL"}
    assert datasets.info("nri").temporal_names() == ["ANNUALIZED"]


def test_dataset_to_dict_and_json_roundtrip():
    d = datasets.info("nfhl")
    payload = _json.loads(d.to_json())
    assert payload["id"] == "nfhl"
    assert payload["kind"] == "HAZARD_OVERLAY"
    assert payload["features"] == d.feature_names()
    assert isinstance(payload["available"], bool)


def test_availability_reflects_deps():
    # osm availability tracks HAS_OSM; tiger (BYO, no special deps) is always available.
    assert datasets.info("osm").available == gravel.HAS_OSM
    assert datasets.info("tiger").available is True
    # a fetcher is available iff geopandas is importable.
    expect = importlib.util.find_spec("geopandas") is not None
    assert datasets.info("nfhl").available == expect


def test_summary_prints_and_returns_str(capsys):
    text = datasets.summary()
    out = capsys.readouterr().out
    assert "features" in text and text in out
    for i in ("osm", "tiger", "nfhl", "shakemap", "usdm", "nri"):
        assert i in text


# --- provenance -------------------------------------------------------------


def test_provenance_stamp_shape():
    p = Provenance.stamp("nfhl", "https://x/28/query", "effective")
    d = p.to_dict()
    assert set(d) == {"dataset_id", "endpoint", "resolved_version", "pulled_at"}
    assert d["dataset_id"] == "nfhl" and d["resolved_version"] == "effective"
    assert d["pulled_at"].endswith("+00:00")  # UTC
    assert _json.loads(p.to_json())["endpoint"] == "https://x/28/query"
    assert "nfhl" in p.summary()


# --- hazard core: edge_probabilities via polygons ---------------------------


def test_core_marks_only_both_endpoints_inside():
    g = _square_graph()
    idx = _edge_lookup(g)
    zone = gravel.Polygon()
    zone.vertices = [gravel.Coord(-0.5, -0.5), gravel.Coord(-0.5, 1.5),
                     gravel.Coord(0.5, 1.5), gravel.Coord(0.5, -0.5)]
    probs = _hazard.hazard_edge_probabilities(g, [(zone, 0.7)], baseline=0.01)
    assert probs.shape == (g.edge_count,) and probs.dtype == np.float64
    assert probs[idx[(0, 1)]] == 0.7 and probs[idx[(1, 0)]] == 0.7
    assert probs[idx[(0, 2)]] == 0.01  # straddles the boundary -> baseline


def test_core_overlapping_zones_take_max():
    g = _square_graph()
    idx = _edge_lookup(g)
    zone = gravel.Polygon()
    zone.vertices = [gravel.Coord(-0.5, -0.5), gravel.Coord(-0.5, 1.5),
                     gravel.Coord(0.5, 1.5), gravel.Coord(0.5, -0.5)]
    probs = _hazard.hazard_edge_probabilities(g, [(zone, 0.3), (zone, 0.8)])
    assert probs[idx[(0, 1)]] == 0.8


# --- NFHL edge_probabilities ------------------------------------------------


def _flood_gdf(zone_code, *, crs="EPSG:4326"):
    from shapely.geometry import Polygon as ShapelyPolygon

    poly = ShapelyPolygon([(-0.5, -0.5), (1.5, -0.5), (1.5, 0.5), (-0.5, 0.5)])
    gdf = geopandas.GeoDataFrame({"FLD_ZONE": [zone_code]}, geometry=[poly], crs="EPSG:4326")
    return gdf.to_crs(crs) if crs != "EPSG:4326" else gdf


@requires_geopandas
def test_nfhl_event_closure_default_and_annual_table():
    g = _square_graph()
    idx = _edge_lookup(g)
    probs = nfhl.edge_probabilities(g, _flood_gdf("AE"))
    assert probs[idx[(0, 1)]] == nfhl.EVENT_CLOSURE["AE"]
    assert probs[idx[(2, 3)]] == 0.0
    annual = nfhl.edge_probabilities(g, _flood_gdf("AE"), zone_probabilities=nfhl.ANNUAL_PROBABILITY)
    assert annual[idx[(0, 1)]] == 0.01


@requires_geopandas
def test_nfhl_reprojects_and_honors_default_probability():
    g = _square_graph()
    idx = _edge_lookup(g)
    probs = nfhl.edge_probabilities(g, _flood_gdf("VE", crs="EPSG:3857"))
    assert probs[idx[(0, 1)]] == nfhl.EVENT_CLOSURE["VE"]
    unknown = nfhl.edge_probabilities(g, _flood_gdf("ZZZ"), default_probability=0.4, baseline=0.0)
    assert unknown[idx[(0, 1)]] == 0.4


def test_nfhl_zone_color_ramp():
    assert nfhl.zone_color("AE")[0] > nfhl.zone_color("X")[0]
    assert nfhl.zone_color("__nope__") == nfhl._DEFAULT_ZONE_COLOR


# --- NFHL fetch (network mocked at _arcgis.urlopen) -------------------------


class _FakeResp:
    def __init__(self, body): self._b = body
    def __enter__(self): return self
    def __exit__(self, *a): return False
    def read(self): return self._b


def _feature(zone):
    return {"type": "Feature", "properties": {"FLD_ZONE": zone},
            "geometry": {"type": "Polygon",
                         "coordinates": [[[0, 0], [0, 1], [1, 1], [1, 0], [0, 0]]]}}


@requires_geopandas
def test_nfhl_fetch_paginates_and_returns_provenance(monkeypatch):
    pages = [
        {"features": [_feature("AE"), _feature("X")], "exceededTransferLimit": True},
        {"features": [_feature("AE")], "exceededTransferLimit": False},
    ]
    calls = []

    def fake_urlopen(req, timeout=None):
        calls.append(req.full_url)
        return _FakeResp(_json.dumps(pages[len(calls) - 1]).encode())

    monkeypatch.setattr(_arcgis, "urlopen", fake_urlopen)
    gdf, prov = nfhl.fetch(
        (-82.6, 35.55, -82.52, 35.64), endpoint="https://x/MapServer", page_size=2
    )
    assert list(gdf["FLD_ZONE"]) == ["AE", "X", "AE"]
    assert len(calls) == 2
    assert "https://x/MapServer/28/query" in calls[0]
    assert "resultOffset=2" in calls[1]
    assert isinstance(prov, Provenance) and prov.dataset_id == "nfhl"


@requires_geopandas
def test_nfhl_fetch_halves_page_on_500(monkeypatch):
    import urllib.parse as up
    from urllib.error import HTTPError

    sizes = []

    def fake_urlopen(req, timeout=None):
        q = dict(up.parse_qsl(req.full_url.split("?", 1)[1]))
        sizes.append(int(q["resultRecordCount"]))
        if int(q["resultRecordCount"]) > 20:
            raise HTTPError(req.full_url, 500, "err", {}, None)
        return _FakeResp(_json.dumps({"features": [_feature("AE")],
                                      "exceededTransferLimit": False}).encode())

    monkeypatch.setattr(_arcgis, "urlopen", fake_urlopen)
    gdf, _prov = nfhl.fetch((0, 0, 1, 1), page_size=100)
    assert len(gdf) == 1
    assert sizes[0] == 100 and min(sizes) <= 25


@requires_geopandas
def test_query_layer_shrinks_page_on_truncation(monkeypatch):
    """A mid-stream truncation (IncompleteRead) on a too-big page is self-healed by
    halving the page and retrying that offset — not raised — like the HTTP 500 path."""
    import urllib.parse as up
    from http.client import IncompleteRead

    sizes = []

    def fake_urlopen(req, timeout=None):
        q = dict(up.parse_qsl(req.full_url.split("?", 1)[1]))
        n = int(q["resultRecordCount"])
        sizes.append(n)
        if n > 25:  # a big page's payload gets cut off before it finishes
            raise IncompleteRead(b"", 999)
        return _FakeResp(_json.dumps({"features": [_feature("AE")],
                                      "exceededTransferLimit": False}).encode())

    monkeypatch.setattr(_arcgis, "urlopen", fake_urlopen)
    gdf = _arcgis.query_layer("https://x/FeatureServer", 0, page_size=100)
    assert len(gdf) == 1
    assert sizes[0] == 100 and min(sizes) <= 25


@requires_geopandas
def test_query_layer_forwards_geometry_simplification(monkeypatch):
    """max_allowable_offset / geometry_precision reach the ArcGIS query as
    maxAllowableOffset / geometryPrecision (server-side polygon simplification)."""
    seen = {}

    def fake_urlopen(req, timeout=None):
        seen["url"] = req.full_url
        return _FakeResp(_json.dumps({"features": [_feature("AE")],
                                      "exceededTransferLimit": False}).encode())

    monkeypatch.setattr(_arcgis, "urlopen", fake_urlopen)
    _arcgis.query_layer(
        "https://x/FeatureServer", 3,
        max_allowable_offset=0.006, geometry_precision=5,
    )
    assert "maxAllowableOffset=0.006" in seen["url"]
    assert "geometryPrecision=5" in seen["url"]


@requires_geopandas
def test_query_layer_pages_full_pages_without_transfer_flag(monkeypatch):
    """A full page keeps paging even when exceededTransferLimit is absent/False —
    servers set that flag only when THEIR transfer limit (not our resultRecordCount)
    capped the page, so relying on it truncates large results to one page."""
    import urllib.parse as up

    def fake_urlopen(req, timeout=None):
        q = dict(up.parse_qsl(req.full_url.split("?", 1)[1]))
        offset, page = int(q["resultOffset"]), int(q["resultRecordCount"])
        remaining = 5 - offset  # 5 total features, no exceededTransferLimit ever
        n = max(0, min(page, remaining))
        return _FakeResp(_json.dumps({"features": [_feature("AE")] * n}).encode())

    monkeypatch.setattr(_arcgis, "urlopen", fake_urlopen)
    gdf = _arcgis.query_layer("https://x/FeatureServer", 0, page_size=2)
    assert len(gdf) == 5  # 2 + 2 + 1, not truncated at the first page of 2


# --- ShakeMap / USDM / NRI edge_probabilities (severity mapping) -------------


def _severity_gdf(field, value):
    from shapely.geometry import Polygon as ShapelyPolygon

    poly = ShapelyPolygon([(-0.5, -0.5), (1.5, -0.5), (1.5, 0.5), (-0.5, 0.5)])
    return geopandas.GeoDataFrame({field: [value]}, geometry=[poly], crs="EPSG:4326")


@requires_geopandas
def test_shakemap_edge_probabilities_bands_mmi():
    from gravel.datasets import shakemap

    g = _square_graph()
    idx = _edge_lookup(g)
    probs = shakemap.edge_probabilities(g, _severity_gdf("mmi", 8.3))
    assert probs[idx[(0, 1)]] == shakemap.MMI_CLOSURE[8]  # floor(8.3) -> band 8
    assert probs[idx[(2, 3)]] == 0.0


@requires_geopandas
def test_usdm_edge_probabilities_by_category():
    from gravel.datasets import usdm

    g = _square_graph()
    idx = _edge_lookup(g)
    probs = usdm.edge_probabilities(g, _severity_gdf("DM", 4))
    assert probs[idx[(0, 1)]] == usdm.DROUGHT_FAILURE["4"]


@requires_geopandas
def test_nri_edge_probabilities_by_rating():
    from gravel.datasets import nri

    g = _square_graph()
    idx = _edge_lookup(g)
    probs = nri.edge_probabilities(g, _severity_gdf("RISK_RATNG", "Very High"))
    assert probs[idx[(0, 1)]] == nri.RISK_CLOSURE["Very High"]
