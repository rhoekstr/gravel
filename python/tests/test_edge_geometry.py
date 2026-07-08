"""Tests for Phase 2B edge geometry: degree-2 contraction preserving each edge's
real polyline, and ``interop.to_geodataframe(..., edge_geometry=...)``.

The core simplify tests need only numpy; the GeoDataFrame tests are skipped
without geopandas/shapely; the Swain test is skipped without an OSM build."""

from pathlib import Path

import gravel
import numpy as np
import pytest

DATA_DIR = Path(__file__).resolve().parents[2] / "tests" / "data"
SWAIN_PBF = DATA_DIR / "swain_county.osm.pbf"


def _maybe_import(name):
    try:
        return __import__(name)
    except ImportError:
        return None


geopandas = _maybe_import("geopandas")
requires_geopandas = pytest.mark.skipif(geopandas is None, reason="geopandas not installed")
requires_osm = pytest.mark.skipif(
    not gravel.HAS_OSM or not SWAIN_PBF.exists(),
    reason="OSM support not built or Swain fixture absent (e.g. sdist without tests/data)",
)


def _theta_graph():
    """Two junctions (0, 1) joined by three parallel chains through degree-2
    mids (2, 3, 4). The chains form cycles, so no edge is a bridge and the mids
    are contractible even under the default bridge protection."""
    und = [(0, 2), (2, 1), (0, 3), (3, 1), (0, 4), (4, 1)]
    edges = und + [(b, a) for a, b in und]
    s = np.array([a for a, b in edges], np.uint32)
    t = np.array([b for a, b in edges], np.uint32)
    w = np.ones(len(edges))
    coords = np.array([[0, 0], [2, 0], [1, 1], [1, 0], [1, -1]], float)
    return gravel.Graph.from_coo(5, s, t, w, coords)


def _simplify(g, emit_geometry):
    cfg = gravel.SimplificationConfig()
    cfg.emit_geometry = emit_geometry
    cfg.estimate_degradation = False
    return gravel.simplify_graph(g, None, None, cfg)


def test_emit_geometry_off_is_empty():
    res = _simplify(_theta_graph(), emit_geometry=False)
    assert res.edge_geometry.empty
    assert res.edge_geometry.edge_count == 0


def test_degree2_contraction_preserves_polylines():
    res = _simplify(_theta_graph(), emit_geometry=True)
    geom = res.edge_geometry

    # Three chains collapse to two junctions with six directed merged edges.
    assert res.simplified_nodes == 2
    assert res.simplified_edges == 6
    assert not geom.empty
    assert geom.edge_count == res.simplified_edges
    assert geom.offsets[0] == 0
    assert geom.offsets[-1] == geom.points.shape[0]
    # Every merged edge keeps its 3-point chain (junction, mid, junction).
    assert [len(geom.polyline(e)) for e in range(geom.edge_count)] == [3] * 6


def test_polyline_endpoints_match_node_coords():
    res = _simplify(_theta_graph(), emit_geometry=True)
    geom = res.edge_geometry
    sources, targets, _ = res.graph.to_coo()
    node_xy = res.graph.node_coordinates()  # (N, 2) [lat, lon]
    for e in range(geom.edge_count):
        line = geom.polyline(e)  # (k, 2) [lat, lon]
        np.testing.assert_allclose(line[0], node_xy[sources[e]])
        np.testing.assert_allclose(line[-1], node_xy[targets[e]])


@requires_geopandas
def test_to_geodataframe_uses_real_geometry():
    res = _simplify(_theta_graph(), emit_geometry=True)
    gdf = gravel.interop.to_geodataframe(res.graph, edge_geometry=res.edge_geometry)
    assert len(gdf) == res.simplified_edges
    # Every drawn edge is a bent 3-vertex line, not a straight chord.
    assert all(len(line.coords) == 3 for line in gdf.geometry)
    # Coordinates are (x=lon, y=lat): mid node 4 is (lat=1, lon=-1) -> (-1, 1).
    verts = {tuple(c) for line in gdf.geometry for c in line.coords}
    assert (-1.0, 1.0) in verts


@requires_geopandas
def test_to_geodataframe_falls_back_to_straight():
    res = _simplify(_theta_graph(), emit_geometry=True)
    gdf = gravel.interop.to_geodataframe(res.graph)  # no edge_geometry
    assert all(len(line.coords) == 2 for line in gdf.geometry)


@requires_geopandas
def test_edge_geometry_length_mismatch_raises():
    g = _theta_graph()
    res = _simplify(g, emit_geometry=True)  # 6 edges of geometry
    # Applying it to the unsimplified graph (12 edges) must be rejected.
    with pytest.raises(ValueError):
        gravel.interop.to_geodataframe(g, edge_geometry=res.edge_geometry)


@requires_osm
@requires_geopandas
def test_swain_real_geometry():
    g, _ = gravel.datasets.osm.load_with_metadata(str(SWAIN_PBF))
    res = _simplify(g, emit_geometry=True)
    geom = res.edge_geometry
    assert geom.edge_count == res.simplified_edges
    # Real roads have curvature: at least some collapsed edges carry >2 points.
    assert any(len(geom.polyline(e)) > 2 for e in range(geom.edge_count))
    gdf = gravel.interop.to_geodataframe(res.graph, edge_geometry=geom)
    assert len(gdf) == res.simplified_edges
    assert gdf.geometry.is_valid.all()


def test_ch_pruning_clears_geometry():
    # CH-level pruning rebuilds the edge set; degree-2 geometry would misalign, so the
    # pipeline drops it. Assert: present without pruning, empty with pruning (never stale).
    g = _theta_graph()
    ch = gravel.build_ch(g)
    idx = gravel.ShortcutIndex(ch)
    cfg = gravel.SimplificationConfig()
    cfg.emit_geometry = True
    cfg.estimate_degradation = False
    assert not gravel.simplify_graph(g, None, None, cfg).edge_geometry.empty
    cfg.ch_level_keep_fraction = 0.5
    assert gravel.simplify_graph(g, ch, idx, cfg).edge_geometry.empty


def test_simplify_edge_geometry_reduces_points():
    # Theta graph -> 3-point bent merged edges; Douglas-Peucker with a large tolerance drops the
    # off-chord midpoints down to straight 2-point edges; tolerance 0 leaves it unchanged.
    from gravel import _gravel

    res = _simplify(_theta_graph(), emit_geometry=True)
    geom = res.edge_geometry
    before = geom.points.shape[0]

    collapsed = _gravel.simplify_edge_geometry(geom, 5.0)
    assert collapsed.edge_count == geom.edge_count
    assert collapsed.points.shape[0] < before
    assert all(len(collapsed.polyline(e)) == 2 for e in range(collapsed.edge_count))

    # Retention path: a tolerance below the off-chord midpoint distance (1.0 on the bent chains)
    # keeps those bends while still dropping the one collinear midpoint — so it lands strictly
    # between full collapse and the untouched geometry, and at least one polyline keeps its 3rd point.
    retained = _gravel.simplify_edge_geometry(geom, 0.5)
    assert collapsed.points.shape[0] < retained.points.shape[0] < before
    assert any(len(retained.polyline(e)) == 3 for e in range(retained.edge_count))

    unchanged = _gravel.simplify_edge_geometry(geom, 0.0)
    assert unchanged.points.shape[0] == before
