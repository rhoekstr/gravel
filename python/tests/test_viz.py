"""Tests for gravel.viz — fragility results -> plot-ready per-edge failure traces.

Array helpers need only numpy; the GeoDataFrame path is skipped without geopandas.
"""

import importlib

import gravel
import numpy as np
import pytest
from gravel import viz


def _maybe_import(name):
    try:
        return importlib.import_module(name)
    except ImportError:
        return None


geopandas = _maybe_import("geopandas")
requires_geopandas = pytest.mark.skipif(geopandas is None, reason="geopandas not installed")


def _square_graph():
    coords = np.array([[0.0, 0.0], [0.0, 1.0], [1.0, 0.0], [1.0, 1.0]], dtype=np.float64)
    src = np.array([0, 1, 0, 2, 1, 3, 2, 3], dtype=np.uint32)
    tgt = np.array([1, 0, 2, 0, 3, 1, 3, 2], dtype=np.uint32)
    return gravel.Graph.from_coo(4, src, tgt, np.ones(8), coords)


def _edge_lookup(g):
    s, t, _ = g.to_coo()
    return {(int(u), int(v)): e for e, (u, v) in enumerate(zip(s, t, strict=True))}


def _coord_grid(n):
    coords = np.array(
        [[r / (n - 1), c / (n - 1)] for r in range(n) for c in range(n)], dtype=np.float64
    )
    src, tgt = [], []
    for r in range(n):
        for c in range(n):
            for dr, dc in ((0, 1), (1, 0)):
                rr, cc = r + dr, c + dc
                if rr < n and cc < n:
                    a, b = r * n + c, rr * n + cc
                    src += [a, b]
                    tgt += [b, a]
    return gravel.Graph.from_coo(
        n * n, np.array(src, dtype=np.uint32), np.array(tgt, dtype=np.uint32),
        np.ones(len(src)), coords,
    )


def _progressive_greedy(g):
    ch = gravel.build_ch(g)
    idx = gravel.ShortcutIndex(ch)
    cfg = gravel.ProgressiveFragilityConfig()
    box = gravel.Polygon()
    box.vertices = [
        gravel.Coord(-0.1, -0.1), gravel.Coord(-0.1, 1.1),
        gravel.Coord(1.1, 1.1), gravel.Coord(1.1, -0.1),
    ]
    bc = cfg.base_config
    bc.boundary = box
    bc.od_sample_count = 10
    cfg.base_config = bc
    cfg.selection_strategy = gravel.SelectionStrategy.GREEDY_BETWEENNESS
    cfg.k_max = 3
    return gravel.progressive_fragility(g, ch, idx, cfg)


# --- stochastic: per-edge failure frequency ---


def test_stochastic_emits_edge_failure_frequency():
    g = _square_graph()
    ch = gravel.build_ch(g)
    idx = gravel.ShortcutIndex(ch)
    emap = _edge_lookup(g)
    m = g.edge_count
    probs = np.zeros(m)
    probs[emap[(0, 1)]] = 1.0  # always fails
    probs[emap[(1, 0)]] = 0.0  # never fails

    cfg = gravel.StochasticFragilityConfig()
    cfg.monte_carlo_runs = 25
    cfg.od_sample_count = 4
    cfg.seed = 3
    res = gravel.stochastic_fragility(g, ch, idx, probs, cfg)

    f = viz.edge_failure_frequency(res)
    assert f.shape == (m,)
    assert f[emap[(0, 1)]] == 1.0
    assert f[emap[(1, 0)]] == 0.0
    assert (f >= 0.0).all() and (f <= 1.0).all()


def test_stochastic_frequency_thread_count_invariant():
    g = _square_graph()
    ch = gravel.build_ch(g)
    idx = gravel.ShortcutIndex(ch)
    probs = np.full(g.edge_count, 0.5)
    cfg = gravel.StochasticFragilityConfig()
    cfg.monte_carlo_runs = 40
    cfg.od_sample_count = 4
    cfg.seed = 7

    gravel.set_max_threads(1)
    a = np.asarray(gravel.stochastic_fragility(g, ch, idx, probs, cfg).edge_failure_frequency)
    gravel.set_max_threads(4)
    b = np.asarray(gravel.stochastic_fragility(g, ch, idx, probs, cfg).edge_failure_frequency)
    assert np.array_equal(a, b)


# --- progressive: removal order -> failure_round ---


def test_progressive_greedy_failure_round():
    g = _coord_grid(6)
    res = _progressive_greedy(g)
    rounds = viz.edge_failure_round(g, res)

    assert rounds.shape == (g.edge_count,)
    finite = np.where(~np.isnan(rounds))[0]
    # one finite round per removed edge, and the rounds are exactly 1..k
    assert len(finite) == len(res.removal_sequence)
    assert sorted(rounds[finite].tolist()) == [float(i) for i in range(1, len(finite) + 1)]

    # the first removed edge maps to round 1
    if res.removal_sequence:
        emap = _edge_lookup(g)
        u, v = res.removal_sequence[0]
        assert rounds[emap[(int(u), int(v))]] == 1.0


def test_progressive_survivors_are_nan():
    g = _coord_grid(6)
    res = _progressive_greedy(g)
    rounds = viz.edge_failure_round(g, res)
    # far more edges survive than are removed
    assert np.isnan(rounds).sum() == g.edge_count - len(res.removal_sequence)


# --- failure_geoframe dispatch ---


@requires_geopandas
def test_failure_geoframe_stochastic_column():
    g = _square_graph()
    ch = gravel.build_ch(g)
    idx = gravel.ShortcutIndex(ch)
    cfg = gravel.StochasticFragilityConfig()
    cfg.monte_carlo_runs = 10
    cfg.od_sample_count = 4
    cfg.seed = 1
    res = gravel.stochastic_fragility(g, ch, idx, np.full(g.edge_count, 0.3), cfg)

    gdf = viz.failure_geoframe(g, res)
    assert "failure_frequency" in gdf.columns
    assert len(gdf) == g.edge_count


@requires_geopandas
def test_failure_geoframe_progressive_column():
    g = _coord_grid(6)
    res = _progressive_greedy(g)
    gdf = viz.failure_geoframe(g, res)
    assert "failure_round" in gdf.columns
    assert len(gdf) == g.edge_count


def test_failure_geoframe_rejects_other_types():
    g = _square_graph()
    with pytest.raises(TypeError):
        viz.failure_geoframe(g, "not a fragility result")
