"""Phase 2A research-depth tests: capacity model, capacity-aware betweenness,
stochastic fragility, and Motter-Lai cascade.

The capacity-derivation test needs OSM metadata (EdgeMetadata comes from
load_osm_graph_with_metadata) and is skipped without an OSM build. The rest work
on synthetic graphs with plain arrays."""

from pathlib import Path

import gravel
import pytest

DATA_DIR = Path(__file__).resolve().parents[2] / "tests" / "data"
SWAIN_PBF = DATA_DIR / "swain_county.osm.pbf"

requires_osm = pytest.mark.skipif(not gravel.HAS_OSM, reason="OSM support not built")


# --------------------------------------------------------------------------
# 2A.2 capacity-aware betweenness
# --------------------------------------------------------------------------

def test_capacity_criticality_and_weighted_importance():
    g = gravel.make_grid_graph(6, 6)
    cap = [float(100 * (e + 1)) for e in range(g.edge_count)]

    cfg = gravel.BetweennessConfig()
    cfg.sample_sources = 20
    cfg.edge_capacity = cap
    b = gravel.edge_betweenness(g, cfg)

    assert len(b.criticality) == g.edge_count
    # criticality = betweenness / capacity
    for e in range(g.edge_count):
        expected = b.edge_scores[e] / cap[e] if cap[e] > 0 else 0.0
        assert b.criticality[e] == pytest.approx(expected, abs=1e-9)

    imp = gravel.capacity_weighted_importance(b, cap)
    assert len(imp) == g.edge_count
    for e in range(g.edge_count):
        assert imp[e] == pytest.approx(b.edge_scores[e] * cap[e], abs=1e-6)


def test_criticality_empty_without_capacity():
    g = gravel.make_grid_graph(5, 5)
    b = gravel.edge_betweenness(g)  # no capacity supplied
    assert len(b.criticality) == 0


# --------------------------------------------------------------------------
# 2A.3 stochastic fragility
# --------------------------------------------------------------------------

def _ch(g):
    ch = gravel.build_ch(g)
    return ch, gravel.ShortcutIndex(ch)


def test_stochastic_zero_probability_no_inflation():
    g = gravel.make_grid_graph(6, 6)
    ch, idx = _ch(g)
    cfg = gravel.StochasticFragilityConfig()
    cfg.monte_carlo_runs = 10
    cfg.od_sample_count = 15
    res = gravel.stochastic_fragility(g, ch, idx, [0.0] * g.edge_count, cfg)
    assert res.probe_pairs > 0
    assert res.mean == pytest.approx(1.0, abs=1e-6)
    assert res.mean_disconnected_fraction == pytest.approx(0.0, abs=1e-12)


def test_stochastic_positive_probability():
    g = gravel.make_grid_graph(8, 8)
    ch, idx = _ch(g)
    cfg = gravel.StochasticFragilityConfig()
    cfg.monte_carlo_runs = 20
    cfg.od_sample_count = 20
    cfg.seed = 7
    res = gravel.stochastic_fragility(g, ch, idx, [0.15] * g.edge_count, cfg)
    assert res.runs == 20
    assert res.mean >= 1.0 - 1e-6
    assert len(res.exceedance) == len(cfg.exceedance_thresholds)


def test_stochastic_reproducible():
    g = gravel.make_grid_graph(7, 7)
    ch, idx = _ch(g)
    cfg = gravel.StochasticFragilityConfig()
    cfg.monte_carlo_runs = 12
    cfg.od_sample_count = 12
    cfg.seed = 99
    a = gravel.stochastic_fragility(g, ch, idx, [0.1] * g.edge_count, cfg)
    b = gravel.stochastic_fragility(g, ch, idx, [0.1] * g.edge_count, cfg)
    assert list(a.run_values) == pytest.approx(list(b.run_values), abs=1e-12)


def test_stochastic_target_enum():
    assert gravel.StochasticTarget.OD_DISTANCE_INFLATION is not None
    assert gravel.StochasticTarget.LOCATION_ISOLATION is not None
    assert gravel.StochasticTarget.INTER_REGION is not None


# --------------------------------------------------------------------------
# 2A.4 cascade
# --------------------------------------------------------------------------

def _cascade_cfg(alpha):
    cfg = gravel.CascadeFragilityConfig()
    cfg.alpha = alpha
    cfg.betweenness_config.sample_sources = 0  # exact / reproducible
    return cfg


def test_cascade_tolerance_orders_cascade():
    g = gravel.make_grid_graph(6, 6)
    r_hi = gravel.cascade_fragility(g, _cascade_cfg(100.0))
    r_lo = gravel.cascade_fragility(g, _cascade_cfg(0.01))
    assert r_hi.cascade_size >= r_hi.trigger_size
    assert r_lo.cascade_size >= r_hi.cascade_size  # less tolerance => bigger cascade


def test_cascade_vs_alpha_curve():
    g = gravel.make_grid_graph(6, 6)
    pts = gravel.cascade_vs_alpha(g, _cascade_cfg(0.1), [0.01, 0.5, 100.0])
    assert len(pts) == 3
    assert pts[0].alpha == 0.01
    # most tolerance => smallest cascade fraction
    assert pts[0].cascade_fraction >= pts[-1].cascade_fraction - 1e-12


def test_cascade_experimental_capacity_enum():
    assert gravel.CascadeCapacity.BETWEENNESS_TOLERANCE is not None
    assert gravel.CascadeCapacity.PCE_WEIGHTED is not None


# --------------------------------------------------------------------------
# 2A.1 capacity model (needs OSM metadata)
# --------------------------------------------------------------------------

@requires_osm
def test_estimate_capacity_from_osm():
    assert SWAIN_PBF.exists()
    g, md = gravel.load_osm_graph_with_metadata(str(SWAIN_PBF))
    cap = gravel.estimate_capacity(md, gravel.CapacityConfig.hcm())
    assert len(cap) == g.edge_count
    assert all(c > 0.0 for c in cap)
    assert len(set(cap)) > 1  # capacities vary by road class / lanes
