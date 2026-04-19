"""Basic smoke tests for the gravel Python package."""

import gravel
import pytest


def test_version():
    assert gravel.__version__ == "2.2.0"


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


@pytest.mark.skipif(
    not hasattr(gravel, "load_osm_graph"),
    reason="OSM support not built",
)
def test_osm_imports_available():
    """If OSM is built, all expected names should be importable."""
    assert hasattr(gravel, "SpeedProfile")
    assert hasattr(gravel, "OSMConfig")
    assert hasattr(gravel, "load_osm_graph")
