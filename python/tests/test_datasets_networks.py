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


# --- GTFS major-city presets (no network; fetch() is stubbed) ---


def test_gtfs_cities_registry_and_aliases():
    from gravel.datasets import gtfs

    c = gtfs.cities()
    assert set(c) == {"nyc", "dc", "chicago", "bart", "boston"}
    assert c["dc"]["needs_key"] is True and c["nyc"]["needs_key"] is False
    assert c["bart"]["needs_key"] is False and c["boston"]["needs_key"] is False
    assert gtfs._resolve_city("New York") == "nyc"
    assert gtfs._resolve_city("WMATA") == "dc" and gtfs._resolve_city("cta") == "chicago"
    assert gtfs._resolve_city("SF") == "bart" and gtfs._resolve_city("mbta") == "boston"
    with pytest.raises(KeyError):
        gtfs._resolve_city("atlantis")


def test_gtfs_fetch_city_wires_url_and_headers(monkeypatch, tmp_path):
    from gravel.datasets import gtfs

    seen = {}

    def fake_fetch(dest, *, feed_url=None, extra_headers=None, timeout=120.0):
        seen.update(dest=dest, feed_url=feed_url, extra_headers=extra_headers)
        return "feed_dir", object()

    monkeypatch.setattr(gtfs, "fetch", fake_fetch)
    gtfs.fetch_city("new york", str(tmp_path))
    assert "rrgtfsfeeds" in seen["feed_url"] and seen["extra_headers"] is None
    gtfs.fetch_city("dc", str(tmp_path), apikey="SECRET")
    assert seen["feed_url"].endswith("rail-gtfs-static.zip")
    assert seen["extra_headers"] == {"api_key": "SECRET"}


def test_gtfs_fetch_city_requires_key_for_dc(monkeypatch, tmp_path):
    from gravel.datasets import gtfs

    monkeypatch.delenv("GRAVEL_WMATA_APIKEY", raising=False)
    with pytest.raises(ValueError, match="API key"):
        gtfs.fetch_city("dc", str(tmp_path))
