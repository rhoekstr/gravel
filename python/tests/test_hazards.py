"""Tests for gravel.hazards — hazard-footprint -> per-edge failure probabilities.

The core (hazard_edge_probabilities) needs only numpy and runs everywhere. The
flood_edge_probabilities tests are skipped when geopandas / shapely are absent.
"""

import importlib

import gravel
import numpy as np
import pytest
from gravel import hazards


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
    """4 nodes on a unit square (lat, lon): 0=(0,0) 1=(0,1) 2=(1,0) 3=(1,1).

    Undirected edges 0-1, 0-2, 1-3, 2-3 as directed COO pairs.
    """
    coords = np.array([[0.0, 0.0], [0.0, 1.0], [1.0, 0.0], [1.0, 1.0]], dtype=np.float64)
    src = np.array([0, 1, 0, 2, 1, 3, 2, 3], dtype=np.uint32)
    tgt = np.array([1, 0, 2, 0, 3, 1, 3, 2], dtype=np.uint32)
    w = np.ones(8, dtype=np.float64)
    return gravel.Graph.from_coo(4, src, tgt, w, coords)


def _edge_lookup(g):
    src, tgt, _ = g.to_coo()
    return {(int(u), int(v)): e for e, (u, v) in enumerate(zip(src, tgt, strict=True))}


def _box(lat0, lat1, lon0, lon1):
    """gravel.Polygon rectangle (lat, lon corners)."""
    p = gravel.Polygon()
    p.vertices = [
        gravel.Coord(lat0, lon0),
        gravel.Coord(lat0, lon1),
        gravel.Coord(lat1, lon1),
        gravel.Coord(lat1, lon0),
    ]
    return p


# --- core: hazard_edge_probabilities ---------------------------------------


def test_core_marks_only_both_endpoints_inside():
    g = _square_graph()
    idx = _edge_lookup(g)
    # box covers the lat~0 row (nodes 0, 1); nodes 2, 3 at lat~1 are outside.
    zone = _box(-0.5, 0.5, -0.5, 1.5)
    probs = hazards.hazard_edge_probabilities(g, [(zone, 0.7)], baseline=0.01)

    assert probs.shape == (g.edge_count,)
    assert probs.dtype == np.float64
    # edge between the two inside nodes: flooded
    assert probs[idx[(0, 1)]] == 0.7
    assert probs[idx[(1, 0)]] == 0.7
    # edges straddling the boundary (one endpoint at lat~1): baseline
    assert probs[idx[(0, 2)]] == 0.01
    assert probs[idx[(2, 3)]] == 0.01


def test_core_baseline_when_no_zone():
    g = _square_graph()
    probs = hazards.hazard_edge_probabilities(g, [], baseline=0.03)
    assert np.allclose(probs, 0.03)


def test_core_overlapping_zones_take_max():
    g = _square_graph()
    idx = _edge_lookup(g)
    zone = _box(-0.5, 0.5, -0.5, 1.5)
    # same footprint at two probabilities, listed low-then-high and high-then-low
    probs = hazards.hazard_edge_probabilities(g, [(zone, 0.3), (zone, 0.8)])
    assert probs[idx[(0, 1)]] == 0.8
    probs2 = hazards.hazard_edge_probabilities(g, [(zone, 0.8), (zone, 0.3)])
    assert probs2[idx[(0, 1)]] == 0.8


def test_core_output_feeds_stochastic_fragility():
    g = _square_graph()
    ch = gravel.build_ch(g)
    sidx = gravel.ShortcutIndex(ch)
    zone = _box(-0.5, 0.5, -0.5, 1.5)
    probs = hazards.hazard_edge_probabilities(g, [(zone, 0.5)], baseline=0.0)

    cfg = gravel.StochasticFragilityConfig()
    cfg.monte_carlo_runs = 20
    cfg.od_sample_count = 4
    cfg.seed = 7
    # the numpy array is accepted directly (no .tolist() needed)
    res = gravel.stochastic_fragility(g, ch, sidx, probs, cfg)
    assert res.runs == 20


# --- NFHL wrapper: flood_edge_probabilities --------------------------------


def _flood_gdf(zone_code, *, crs="EPSG:4326"):
    from shapely.geometry import Polygon as ShapelyPolygon

    # shapely uses (x=lon, y=lat); cover the lat~0 row (nodes 0, 1).
    poly = ShapelyPolygon([(-0.5, -0.5), (1.5, -0.5), (1.5, 0.5), (-0.5, 0.5)])
    gdf = geopandas.GeoDataFrame({"FLD_ZONE": [zone_code]}, geometry=[poly], crs="EPSG:4326")
    return gdf.to_crs(crs) if crs != "EPSG:4326" else gdf


@requires_geopandas
def test_flood_maps_event_closure_by_default():
    g = _square_graph()
    idx = _edge_lookup(g)
    probs = hazards.flood_edge_probabilities(g, _flood_gdf("AE"))
    assert probs[idx[(0, 1)]] == hazards.NFHL_EVENT_CLOSURE["AE"]
    assert probs[idx[(2, 3)]] == 0.0


@requires_geopandas
def test_flood_annual_table_uses_zone_exceedance():
    g = _square_graph()
    idx = _edge_lookup(g)
    probs = hazards.flood_edge_probabilities(
        g, _flood_gdf("AE"), zone_probabilities=hazards.NFHL_ANNUAL_PROBABILITY
    )
    assert probs[idx[(0, 1)]] == 0.01


@requires_geopandas
def test_flood_reprojects_non_wgs84():
    g = _square_graph()
    idx = _edge_lookup(g)
    # supply the layer in Web Mercator; the function must reproject to WGS84.
    probs = hazards.flood_edge_probabilities(g, _flood_gdf("VE", crs="EPSG:3857"))
    assert probs[idx[(0, 1)]] == hazards.NFHL_EVENT_CLOSURE["VE"]


@requires_geopandas
def test_flood_unknown_zone_skipped_by_default():
    g = _square_graph()
    probs = hazards.flood_edge_probabilities(g, _flood_gdf("ZZZ"), baseline=0.0)
    assert float(probs.max()) == 0.0


@requires_geopandas
def test_flood_unknown_zone_honors_default_probability():
    g = _square_graph()
    idx = _edge_lookup(g)
    probs = hazards.flood_edge_probabilities(
        g, _flood_gdf("ZZZ"), default_probability=0.4, baseline=0.0
    )
    assert probs[idx[(0, 1)]] == 0.4
