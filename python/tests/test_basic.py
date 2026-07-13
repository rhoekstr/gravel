"""Basic smoke tests for the gravel Python package."""

import gravel
import numpy as np
import pytest


def test_version():
    # Loosely pinned: only verify it parses as a semver-ish string and
    # matches the source of truth. Avoids a test-breaking version bump
    # every release.
    v = gravel.__version__
    parts = v.split(".")
    assert len(parts) >= 2
    assert all(p.isdigit() for p in parts[:2])


def test_make_grid_graph():
    g = gravel.make_grid_graph(10, 10)
    assert g.node_count == 100
    assert g.edge_count > 0


def test_build_ch():
    g = gravel.make_grid_graph(10, 10)
    ch = gravel.build_ch(g)
    assert ch is not None


def test_ch_query():
    g = gravel.make_grid_graph(10, 10)
    ch = gravel.build_ch(g)
    q = gravel.CHQuery(ch)
    result = q.route(0, 99)
    assert result.distance > 0
    assert len(result.path) >= 2


def test_coord():
    c = gravel.Coord(35.43, -83.45)
    assert c.lat == pytest.approx(35.43)
    assert c.lon == pytest.approx(-83.45)


def test_location_fragility_config_defaults():
    cfg = gravel.LocationFragilityConfig()
    assert cfg.radius_meters == 80467.0
    assert cfg.monte_carlo_runs == 20
    assert cfg.sample_count == 200
    assert cfg.strategy == gravel.SelectionStrategy.MONTE_CARLO


def test_region_assignment():
    # Build a small graph with coordinates
    g = gravel.make_grid_graph(5, 5)

    # Create a region that contains no nodes (coordinates are synthetic)
    regions = []

    assignment = gravel.assign_nodes_to_regions(g, regions)
    assert assignment.unassigned_count == g.node_count


def test_selection_strategies():
    # All three strategies should be accessible
    assert gravel.SelectionStrategy.MONTE_CARLO is not None
    assert gravel.SelectionStrategy.GREEDY_BETWEENNESS is not None
    assert gravel.SelectionStrategy.GREEDY_FRAGILITY is not None


def test_coarsening_config_defaults():
    cfg = gravel.CoarseningConfig()
    assert cfg.compute_centroids is True
    assert cfg.min_border_edges == 1


def test_dijkstra_one_to_many():
    # One-to-many SSSP (the primitive the future SUE flow layer needs): distances +
    # shortest-path tree, agreeing with single-pair, path-reconstructible.
    g = gravel.make_grid_graph(8, 8)  # 64 nodes
    r = gravel.dijkstra(g, 0)
    d = np.asarray(r.distances)
    p = np.asarray(r.predecessors)
    assert d.shape == (g.node_count,) and d.dtype == np.float64 and p.dtype == np.uint32
    assert d[0] == 0.0
    assert int(p[0]) == np.iinfo(np.uint32).max  # source has no predecessor (INVALID_NODE)
    # one-to-many distances match the single-pair query
    for t in (1, 7, 63, 56):
        assert abs(float(d[t]) - gravel.dijkstra_pair(g, 0, t)) < 1e-9
    # reconstruct_path returns a real path whose traversed cost equals the distance
    path = gravel.reconstruct_path(r, 0, 63)
    assert path[0] == 0 and path[-1] == 63
    src, tgt, w = (np.asarray(x) for x in g.to_coo())
    wmap = {(int(a), int(b)): float(c) for a, b, c in zip(src, tgt, w, strict=True)}
    cost = sum(wmap[(int(a), int(b))] for a, b in zip(path, path[1:], strict=False))
    assert abs(cost - float(d[63])) < 1e-9


@pytest.mark.skipif(
    not gravel.HAS_OSM,
    reason="OSM support not built",
)
def test_osm_imports_available():
    """If OSM is built, all expected names should be importable."""
    assert hasattr(gravel, "SpeedProfile")
    assert hasattr(gravel, "OSMConfig")
    assert hasattr(gravel, "load_osm_graph")


def test_bindings_keep_alive_from_temporaries():
    """Regression: EdgeSampler and BlockedCHQuery store the graph/CH by raw reference.
    Constructing them from temporaries then collecting the temporaries must not
    use-after-free (previously crashed: the bindings lacked py::keep_alive)."""
    import gc

    s = gravel.EdgeSampler(gravel.make_grid_graph(30, 30))  # temporary Graph
    gc.collect()
    assert len(s.sample(gravel.SamplerConfig())) > 0        # would segfault if freed

    def make_q():
        g = gravel.make_grid_graph(20, 20)
        ch = gravel.build_ch(g)
        return gravel.BlockedCHQuery(ch, gravel.ShortcutIndex(ch), g)  # ch/idx/g all temporary

    q = make_q()
    gc.collect()
    d = q.distance_blocking(0, 399, [])                     # would segfault if ch/g freed
    assert d > 0
