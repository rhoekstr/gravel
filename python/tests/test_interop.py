"""Tests for the interop keystone: coordinate/COO accessors, OSM edge metadata,
GeoJSON/JSONL export, and the NetworkX / GeoPandas adapters (gravel.interop).

OSM-dependent tests are skipped unless the extension was built with libosmium
(gravel.HAS_OSM). Adapter tests are skipped when networkx / geopandas are not
installed (the optional `interop` extra)."""

import json
from pathlib import Path

import gravel
import numpy as np  # core dependency — always available
import pytest

DATA_DIR = Path(__file__).resolve().parents[2] / "tests" / "data"
SWAIN_PBF = DATA_DIR / "swain_county.osm.pbf"


def _maybe_import(name):
    try:
        return __import__(name)
    except ImportError:
        return None


networkx = _maybe_import("networkx")
geopandas = _maybe_import("geopandas")

requires_networkx = pytest.mark.skipif(networkx is None, reason="networkx not installed")
requires_geopandas = pytest.mark.skipif(geopandas is None, reason="geopandas not installed")
requires_osm = pytest.mark.skipif(
    not gravel.HAS_OSM or not SWAIN_PBF.exists(),
    reason="OSM support not built or Swain fixture absent (e.g. sdist without tests/data)",
)


# --------------------------------------------------------------------------
# Core accessors: coordinates + COO
# --------------------------------------------------------------------------

def test_has_coordinates_false_for_synthetic():
    g = gravel.make_grid_graph(5, 5)
    assert g.has_coordinates is False


def test_node_coordinates_empty_when_absent():
    g = gravel.make_grid_graph(5, 5)
    coords = g.node_coordinates()
    assert coords.shape == (0, 2)


def test_to_coo_shapes_and_dtypes():
    g = gravel.make_grid_graph(6, 6)
    s, t, w = g.to_coo()
    assert len(s) == len(t) == len(w) == g.edge_count
    assert str(s.dtype) == "uint32"
    assert str(t.dtype) == "uint32"
    assert str(w.dtype) == "float64"


def test_from_coo_roundtrip_no_coords():
    g = gravel.make_grid_graph(7, 7)
    s, t, w = g.to_coo()
    g2 = gravel.Graph.from_coo(g.node_count, s, t, w)
    assert g2.node_count == g.node_count
    assert g2.edge_count == g.edge_count
    s2, t2, w2 = g2.to_coo()
    assert np.array_equal(s, s2)
    assert np.array_equal(t, t2)
    assert np.allclose(w, w2)


def test_from_coo_with_coords():
    # Triangle with explicit coordinates.
    sources = np.array([0, 1, 2], dtype="uint32")
    targets = np.array([1, 2, 0], dtype="uint32")
    weights = np.array([1.0, 2.0, 3.0], dtype="float64")
    coords = np.array([[35.0, -83.0], [35.1, -83.1], [35.2, -83.2]], dtype="float64")
    g = gravel.Graph.from_coo(3, sources, targets, weights, coords)
    assert g.node_count == 3
    assert g.edge_count == 3
    assert g.has_coordinates is True
    assert np.allclose(g.node_coordinates(), coords)


def test_from_coo_rejects_bad_coords_shape():
    sources = np.array([0], dtype="uint32")
    targets = np.array([1], dtype="uint32")
    weights = np.array([1.0], dtype="float64")
    bad = np.zeros((1, 2), dtype="float64")  # only 1 row, need 2 nodes
    with pytest.raises(RuntimeError):
        gravel.Graph.from_coo(2, sources, targets, weights, bad)


# --------------------------------------------------------------------------
# GeoJSON / JSONL export
# --------------------------------------------------------------------------

def test_route_to_geojson_is_valid_linestring():
    g = gravel.make_grid_graph(10, 10)
    ch = gravel.build_ch(g)
    q = gravel.CHQuery(ch)
    r = q.route(0, 99)
    gj = json.loads(gravel.route_to_geojson(g, r.path))
    assert gj["type"] == "Feature"
    assert gj["geometry"]["type"] == "LineString"


def test_has_arrow_is_bool():
    assert isinstance(gravel.HAS_ARROW, bool)


def test_write_fragility_jsonl(tmp_path):
    g = gravel.make_grid_graph(8, 8)
    ch = gravel.build_ch(g)
    idx = gravel.ShortcutIndex(ch)
    pairs = [(0, 63), (1, 60)]
    results = gravel.batch_fragility(ch, idx, g, pairs)
    out = tmp_path / "frag.jsonl"
    gravel.write_fragility_jsonl(results, pairs, str(out))
    lines = out.read_text().strip().splitlines()
    assert len(lines) == len(results)
    for line in lines:
        json.loads(line)  # each line is valid JSON


# --------------------------------------------------------------------------
# NetworkX adapters
# --------------------------------------------------------------------------

@requires_networkx
def test_to_networkx_directed_and_undirected():
    from gravel import interop

    g = gravel.make_grid_graph(5, 5)
    di = interop.to_networkx(g, directed=True)
    un = interop.to_networkx(g, directed=False)
    assert di.is_directed() is True
    assert un.is_directed() is False
    assert di.number_of_nodes() == g.node_count
    # Every edge carries a weight attribute.
    _, _, data = next(iter(di.edges(data=True)))
    assert "weight" in data


@requires_networkx
def test_networkx_roundtrip_counts():
    from gravel import interop

    g = gravel.make_grid_graph(6, 6)
    back = interop.from_networkx(interop.to_networkx(g, directed=True))
    assert back.node_count == g.node_count
    assert back.edge_count == g.edge_count


@requires_networkx
def test_from_networkx_string_labels_and_default_weight():
    from gravel import interop

    nx = networkx
    G = nx.DiGraph()
    G.add_edge("a", "b", weight=2.5)
    G.add_edge("b", "c")  # no weight -> defaults to 1.0
    g = interop.from_networkx(G)
    assert g.node_count == 3
    assert g.edge_count == 2


# --------------------------------------------------------------------------
# GeoPandas adapters
# --------------------------------------------------------------------------

@requires_geopandas
def test_geodataframe_roundtrip_small():
    from gravel import interop

    sources = np.array([0, 1, 2], dtype="uint32")
    targets = np.array([1, 2, 0], dtype="uint32")
    weights = np.array([10.0, 20.0, 30.0], dtype="float64")
    coords = np.array([[35.0, -83.0], [35.1, -83.1], [35.2, -83.05]], dtype="float64")
    g = gravel.Graph.from_coo(3, sources, targets, weights, coords)

    gdf = interop.to_geodataframe(g)
    assert len(gdf) == g.edge_count
    assert {"source", "target", "weight", "geometry"} <= set(gdf.columns)
    assert str(gdf.crs).upper().endswith("4326")
    assert gdf.geometry.iloc[0].geom_type == "LineString"

    back = interop.from_geodataframe(gdf, weight="weight", directed=True)
    assert back.node_count == g.node_count
    assert back.edge_count == g.edge_count
    assert back.has_coordinates is True


@requires_geopandas
def test_to_geodataframe_requires_coordinates():
    from gravel import interop

    g = gravel.make_grid_graph(4, 4)  # no coordinates
    with pytest.raises(ValueError):
        interop.to_geodataframe(g)


# --------------------------------------------------------------------------
# OSM metadata (real data)
# --------------------------------------------------------------------------

@requires_osm
def test_osm_metadata_alignment():
    assert SWAIN_PBF.exists(), f"missing test data: {SWAIN_PBF}"
    g, md = gravel.datasets.osm.load_with_metadata(str(SWAIN_PBF))
    assert g.node_count > 0
    assert g.has_coordinates is True
    assert "highway" in md
    highway = md.get("highway")
    assert len(highway) == g.edge_count
    # Highway classes are non-trivial (residential/service/etc. present).
    assert any(highway)


@requires_osm
@requires_geopandas
def test_osm_to_geodataframe_carries_metadata():
    from gravel import interop

    g, md = gravel.datasets.osm.load_with_metadata(str(SWAIN_PBF))
    gdf = interop.to_geodataframe(g, metadata=md)
    assert len(gdf) == g.edge_count
    for col in ("highway", "lanes", "maxspeed"):
        assert col in gdf.columns
