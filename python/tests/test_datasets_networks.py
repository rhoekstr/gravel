"""Parse tests for the 2.7 network-substrate loaders (against small fixtures).

Each loader is exercised against a tiny representative fixture under tests/data/;
no network access. Verifies node/edge counts, capacity presence, and coordinates.
"""

import pathlib

import gravel
import numpy as np
import pytest

DATA = pathlib.Path(__file__).resolve().parents[2] / "tests" / "data"


def test_gridsfm_loads_grid_with_capacity():
    g, cap = gravel.datasets.gridsfm.load(str(DATA / "net_gridsfm_sample.json"))
    assert g.edge_count == 3
    assert cap.shape == (3,) and cap.dtype == np.float64
    assert cap.sum() == pytest.approx(1100.0)  # 412.5 + 440.0 + 247.5 MVA
    assert g.has_coordinates


def test_opfdata_loads_capacity_without_coords():
    g, cap = gravel.datasets.opfdata.load(str(DATA / "net_opfdata_sample.json"))
    assert g.edge_count == 6 and cap.shape == (6,)  # 3 branches, forward+reverse
    assert not g.has_coordinates


def test_openflights_loads_air_network():
    g, cap = gravel.datasets.openflights.load(
        str(DATA / "net_openflights_airports.dat"),
        str(DATA / "net_openflights_routes.dat"),
    )
    assert g.edge_count == 4 and cap.shape == (0,)  # no native capacity
    assert g.has_coordinates


def test_caida_loads_router_topology():
    g, cap = gravel.datasets.caida.load(
        str(DATA / "net_caida_sample" / "midar-iff.nodes"),
        str(DATA / "net_caida_sample" / "midar-iff.links"),
        nodes_geo_path=str(DATA / "net_caida_sample" / "midar-iff.nodes.geo"),
    )
    assert g.edge_count == 10 and cap.shape == (0,)  # ITDK carries no capacity


def test_gtfs_loads_transit_with_capacity():
    g, cap = gravel.datasets.gtfs.load(str(DATA / "net_gtfs_sample"))
    assert g.edge_count == 2 and cap.shape == (2,)
    assert g.has_coordinates


# --- T-100 airline capacity overlay ---


def test_t100_load_sums_seats_by_pair(tmp_path):
    p = tmp_path / "t100.csv"
    p.write_text("ORIGIN,DEST,SEATS\nSFO,LAX,100\nSFO,LAX,50\nJFK,LAX,200\n")
    table = gravel.datasets.t100.load(str(p))
    assert table[("SFO", "LAX")] == 150.0  # summed across rows
    assert table[("JFK", "LAX")] == 200.0


def test_t100_edge_capacity_joins_by_iata():
    graph, _, codes = gravel.datasets.openflights.load(
        str(DATA / "net_openflights_airports.dat"),
        str(DATA / "net_openflights_routes.dat"),
        with_codes=True,
    )
    src, tgt, _ = graph.to_coo()
    # pick an edge whose endpoints both carry an IATA code, then join a value onto it
    e = next((i for i in range(len(src)) if codes[int(src[i])] and codes[int(tgt[i])]), None)
    assert e is not None
    table = {(codes[int(src[e])], codes[int(tgt[e])]): 999.0}
    cap = gravel.datasets.t100.edge_capacity(graph, codes, table)
    assert cap.shape == (len(src),) and cap[e] == 999.0
