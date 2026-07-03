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

matplotlib = _maybe_import("matplotlib")
requires_matplotlib = pytest.mark.skipif(matplotlib is None, reason="matplotlib not installed")
if matplotlib is not None:
    matplotlib.use("Agg")  # headless backend for tests

lonboard = _maybe_import("lonboard")
requires_lonboard = pytest.mark.skipif(lonboard is None, reason="lonboard not installed")


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


# --- Tier 1: static rendering (plot_fragility) ---


def _stochastic(g):
    ch = gravel.build_ch(g)
    idx = gravel.ShortcutIndex(ch)
    sc = gravel.StochasticFragilityConfig()
    sc.monte_carlo_runs = 30
    sc.seed = 1
    sc.od_sample_count = 10
    return gravel.stochastic_fragility(g, ch, idx, [0.15] * g.edge_count, sc)


@requires_geopandas
@requires_matplotlib
def test_plot_fragility_stochastic_returns_axes():
    g = _coord_grid(5)
    ax = viz.plot_fragility(g, _stochastic(g), title="P(fail)")
    assert hasattr(ax, "collections")
    assert len(ax.collections) >= 1  # the edge LineCollection
    assert ax.get_title() == "P(fail)"


@requires_geopandas
@requires_matplotlib
def test_plot_fragility_progressive_greyed_survivors():
    g = _coord_grid(5)
    ax = viz.plot_fragility(g, _progressive_greedy(g))
    assert len(ax.collections) >= 1  # failure_round path with missing_kwds


@requires_geopandas
@requires_matplotlib
def test_plot_fragility_hazard_overlay_adds_layer():
    from shapely.geometry import Polygon

    g = _coord_grid(5)
    plain = viz.plot_fragility(g, _stochastic(g))
    haz = geopandas.GeoDataFrame(
        {"sev": [1.0]},
        geometry=[Polygon([(0, 0), (1, 0), (1, 1), (0, 1)])],
        crs="EPSG:4326",
    )
    with_haz = viz.plot_fragility(g, _stochastic(g), hazard=haz, hazard_column="sev")
    assert len(with_haz.collections) > len(plain.collections)


@requires_geopandas
@requires_matplotlib
def test_plot_fragility_uses_edge_geometry():
    # Theta graph -> simplify (real polylines) -> stochastic -> render along real shape.
    und = [(0, 2), (2, 1), (0, 3), (3, 1), (0, 4), (4, 1)]
    edges = und + [(b, a) for a, b in und]
    s = np.array([a for a, b in edges], np.uint32)
    t = np.array([b for a, b in edges], np.uint32)
    coords = np.array([[0, 0], [2, 0], [1, 1], [1, 0], [1, -1]], float)
    tg = gravel.Graph.from_coo(5, s, t, np.ones(len(edges)), coords)
    cfg = gravel.SimplificationConfig()
    cfg.estimate_degradation = False
    sres = gravel.simplify_graph(tg, None, None, cfg)

    res = _stochastic(sres.graph)
    gdf = viz.failure_geoframe(sres.graph, res, edge_geometry=sres.edge_geometry)
    assert any(len(line.coords) > 2 for line in gdf.geometry)  # bent, not chords
    ax = viz.plot_fragility(sres.graph, res, edge_geometry=sres.edge_geometry)
    assert len(ax.collections) >= 1


@requires_matplotlib
def test_plot_fragility_rejects_other_type():
    g = _square_graph()
    with pytest.raises(TypeError):
        viz.plot_fragility(g, object())


# --- Tier 2: interactive lonboard map (interactive_map) ---


@requires_geopandas
@requires_matplotlib
@requires_lonboard
def test_interactive_map_returns_lonboard_map():
    g = _coord_grid(6)
    m = viz.interactive_map(g, _stochastic(g))
    assert type(m).__name__ == "Map"
    assert len(m.layers) == 1  # edge PathLayer only


@requires_geopandas
@requires_matplotlib
@requires_lonboard
def test_interactive_map_hazard_adds_base_layer():
    from shapely.geometry import Polygon

    g = _coord_grid(6)
    haz = geopandas.GeoDataFrame(
        {"x": [1]}, geometry=[Polygon([(0, 0), (1, 0), (1, 1), (0, 1)])], crs="EPSG:4326"
    )
    m = viz.interactive_map(g, _stochastic(g), hazard=haz)
    assert len(m.layers) == 2  # hazard polygon + edges


@requires_geopandas
@requires_matplotlib
@requires_lonboard
def test_interactive_map_progressive_html_export(tmp_path):
    g = _coord_grid(6)
    m = viz.interactive_map(g, _progressive_greedy(g))  # failure_round path
    out = tmp_path / "map.html"
    m.to_html(str(out))
    assert out.exists() and out.stat().st_size > 0


@requires_lonboard
def test_interactive_map_rejects_other_type():
    g = _square_graph()
    with pytest.raises(TypeError):
        viz.interactive_map(g, object())


# --- Tier 2: animation (animate_failure) ---


def test_failure_colors_binary_and_monotonic():
    # Pure animation core — no optional deps, so this runs everywhere.
    fr = np.array([1.0, 2.0, 3.0, np.nan])  # last edge survives
    grey = (180, 180, 180)

    c0 = viz._failure_colors(fr, 0)
    assert (c0 == c0[0]).all()  # nothing failed at round 0

    c2 = viz._failure_colors(fr, 2)
    assert tuple(c2[0]) == grey and tuple(c2[1]) == grey  # failed by round 2
    assert tuple(c2[2]) != grey and tuple(c2[3]) != grey  # not-yet + survivor stay active

    grey_counts = [
        int(np.sum(np.all(viz._failure_colors(fr, k) == grey, axis=1))) for k in range(5)
    ]
    assert grey_counts == sorted(grey_counts)  # monotonic non-decreasing
    assert grey_counts[-1] == 3  # all finite edges eventually grey; survivor never


@requires_geopandas
@requires_lonboard
def test_animate_failure_returns_widget():
    import ipywidgets

    g = _coord_grid(6)
    w = viz.animate_failure(g, _progressive_greedy(g))
    assert isinstance(w, ipywidgets.VBox)


def test_animate_failure_rejects_stochastic():
    # The progressive-only guard runs before any optional import.
    g = _coord_grid(5)
    with pytest.raises(TypeError):
        viz.animate_failure(g, _stochastic(g))


# --- Tier 2: self-contained animated HTML (animate_failure_html) ---


@requires_geopandas
def test_animate_failure_html_writes_selfcontained(tmp_path):
    g = _coord_grid(6)
    out = tmp_path / "anim.html"
    p = viz.animate_failure_html(g, _progressive_greedy(g), str(out))
    assert p == str(out) and out.exists()
    html = out.read_text()
    assert "unpkg.com/deck.gl@" in html  # standalone deck.gl from CDN
    assert "PathLayer" in html and "getColor" in html
    assert 'id="round"' in html and 'id="play"' in html  # scrubber + play controls
    assert html.count('"path"') == g.edge_count  # one embedded edge per graph edge
    assert "const MAXROUND" in html


@requires_geopandas
def test_animate_failure_html_embeds_real_geometry(tmp_path):
    # Theta graph -> simplify (bent polylines) -> the HTML embeds >2-point paths.
    und = [(0, 2), (2, 1), (0, 3), (3, 1), (0, 4), (4, 1)]
    edges = und + [(b, a) for a, b in und]
    s = np.array([a for a, b in edges], np.uint32)
    t = np.array([b for a, b in edges], np.uint32)
    coords = np.array([[0, 0], [2, 0], [1, 1], [1, 0], [1, -1]], float)
    tg = gravel.Graph.from_coo(5, s, t, np.ones(len(edges)), coords)
    cfg = gravel.SimplificationConfig()
    cfg.estimate_degradation = False
    sres = gravel.simplify_graph(tg, None, None, cfg)
    # A progressive run on the simplified graph, drawn along real geometry.
    ch = gravel.build_ch(sres.graph)
    idx = gravel.ShortcutIndex(ch)
    pcfg = gravel.ProgressiveFragilityConfig()
    box = gravel.Polygon()
    box.vertices = [
        gravel.Coord(-1, -2), gravel.Coord(-1, 2),
        gravel.Coord(3, 2), gravel.Coord(3, -2),
    ]
    bc = pcfg.base_config
    bc.boundary = box
    bc.od_sample_count = 4
    pcfg.base_config = bc
    pcfg.selection_strategy = gravel.SelectionStrategy.GREEDY_BETWEENNESS
    pcfg.k_max = 2
    prog = gravel.progressive_fragility(sres.graph, ch, idx, pcfg)

    out = tmp_path / "geom.html"
    viz.animate_failure_html(
        sres.graph, prog, str(out), edge_geometry=sres.edge_geometry
    )
    html = out.read_text()
    import json
    import re

    edges_json = re.search(r"const EDGES = (\[.*?\]);", html, re.S).group(1)
    embedded = json.loads(edges_json)
    assert any(len(e["path"]) > 2 for e in embedded)  # real bent shape, not chords


def test_animate_failure_html_rejects_stochastic(tmp_path):
    g = _coord_grid(5)
    with pytest.raises(TypeError):
        viz.animate_failure_html(g, _stochastic(g), str(tmp_path / "x.html"))


@requires_geopandas
def test_failure_geoframe_row_order_matches_csr():
    # The GeoDataFrame rows must be in CSR (to_coo) order, or positional per-edge
    # colors line up with the wrong edges and every render is silently mislabeled.
    g = _coord_grid(6)
    gdf = viz.failure_geoframe(g, _stochastic(g))
    src, tgt, _ = g.to_coo()
    assert list(gdf["source"]) == list(src)
    assert list(gdf["target"]) == list(tgt)


class _FakeProgressive:
    """Minimal stand-in: edge_failure_round only reads .removal_sequence."""

    def __init__(self, removal_sequence):
        self.removal_sequence = removal_sequence


def test_edge_failure_round_parallel_edges_get_distinct_rounds():
    # Regression for the per-(u,v) queue: three parallel 1->2 edges plus one 0->1 edge.
    # Removing (1,2) twice must consume two DIFFERENT parallel edges — not overwrite one
    # index (mis-attribution) or pop an empty queue (IndexError). (CSR reorders by source,
    # so resolve the parallel indices from to_coo rather than assuming input order.)
    s = np.array([1, 1, 1, 0], np.uint32)
    t = np.array([2, 2, 2, 1], np.uint32)
    g = gravel.Graph.from_coo(3, s, t, np.ones(4))

    src, tgt, _ = g.to_coo()
    parallel = [e for e in range(g.edge_count) if src[e] == 1 and tgt[e] == 2]
    other = [e for e in range(g.edge_count) if e not in parallel]
    assert len(parallel) == 3

    rounds = viz.edge_failure_round(g, _FakeProgressive([(1, 2), (1, 2)]))
    got = [rounds[e] for e in parallel if not np.isnan(rounds[e])]
    assert sorted(got) == [1.0, 2.0]  # two distinct parallel edges, two distinct rounds
    assert sum(np.isnan(rounds[e]) for e in parallel) == 1  # third parallel untouched
    assert all(np.isnan(rounds[e]) for e in other)  # unrelated edges untouched


def test_edge_failure_round_unknown_pair_is_ignored():
    # A removal whose (u,v) isn't in the graph (e.g. wrong graph passed) must be skipped,
    # never raise — the `if q:` guard covers the missing-bucket case.
    s = np.array([0], np.uint32)
    t = np.array([1], np.uint32)
    g = gravel.Graph.from_coo(2, s, t, np.ones(1))
    rounds = viz.edge_failure_round(g, _FakeProgressive([(7, 9), (0, 1)]))
    # (7,9) is step 1 but ignored (not in graph); (0,1) is step 2 and lands. No raise.
    assert rounds[0] == 2.0
    assert rounds.shape == (1,)


# --- F2: hazard-ordered sequence + renderers accepting a failure_round array ---


def test_failure_sequence_exposure_order():
    probs = np.array([0.0, 0.9, 0.3, 0.6])
    fr = viz.failure_sequence_from_probabilities(probs, exposure_order=True)
    assert np.isnan(fr[0])                       # zero probability -> never removed
    assert fr[1] == 1.0 and fr[3] == 2.0 and fr[2] == 3.0  # worst-exposure first


def test_failure_sequence_limit_and_stages():
    probs = np.linspace(0.05, 1.0, 20)
    fr = viz.failure_sequence_from_probabilities(probs, exposure_order=True, limit=10, stages=5)
    finite = fr[~np.isnan(fr)]
    assert len(finite) == 10                     # limit honored
    assert set(finite.tolist()) <= set(range(1, 6)) and finite.max() == 5  # bucketed


def test_failure_sequence_stochastic_reproducible():
    probs = np.full(60, 0.5)
    a = viz.failure_sequence_from_probabilities(probs, seed=7)
    b = viz.failure_sequence_from_probabilities(probs, seed=7)
    c = viz.failure_sequence_from_probabilities(probs, seed=8)

    def fill(x):
        return np.nan_to_num(x, nan=-1.0)

    assert np.array_equal(fill(a), fill(b))      # same seed -> identical realization
    assert not np.array_equal(fill(a), fill(c))  # different seed -> different draw


@requires_geopandas
def test_failure_geoframe_accepts_array():
    g = _coord_grid(5)
    fr = np.full(g.edge_count, np.nan)
    fr[0], fr[1] = 1.0, 2.0
    gdf = viz.failure_geoframe(g, fr)
    assert "failure_round" in gdf.columns and len(gdf) == g.edge_count


@requires_geopandas
def test_failure_round_array_length_mismatch_raises():
    g = _coord_grid(4)
    with pytest.raises(ValueError):
        viz.failure_geoframe(g, np.zeros(g.edge_count + 3))


@requires_geopandas
def test_animate_failure_html_accepts_failure_round_array(tmp_path):
    g = _coord_grid(5)
    probs = np.random.default_rng(0).random(g.edge_count)
    fr = viz.failure_sequence_from_probabilities(probs, seed=1, limit=8, stages=4)
    out = tmp_path / "flood.html"
    viz.animate_failure_html(g, fr, str(out))
    assert out.exists() and "unpkg.com/deck.gl@" in out.read_text()


# --- F3: connectivity_curve + dashboard_html ---


def test_connectivity_curve_severs_a_component():
    # Path 0-1-2 (bidirectional); remove both 1<->2 edges at stage 1 -> node 2 isolated.
    s = np.array([0, 1, 1, 2], np.uint32)
    t = np.array([1, 0, 2, 1], np.uint32)
    g = gravel.Graph.from_coo(3, s, t, np.ones(4))
    s2, t2, _ = g.to_coo()
    fr = np.full(4, np.nan)
    for e in range(4):
        if {int(s2[e]), int(t2[e])} == {1, 2}:
            fr[e] = 1.0
    curve = viz.connectivity_curve(g, fr)
    assert curve[0] == 0.0                       # fully connected
    assert abs(curve[1] - (1 - 5 / 9)) < 1e-9    # {0,1} + {2}: 1 - (4+1)/9
    assert curve == sorted(curve)                # non-decreasing


@requires_geopandas
def test_dashboard_html_writes_map_and_chart(tmp_path):
    g = _coord_grid(6)
    probs = np.random.default_rng(0).random(g.edge_count)
    fr = viz.failure_sequence_from_probabilities(probs, seed=1, limit=30, stages=10)
    out = tmp_path / "dash.html"
    viz.dashboard_html(g, fr, str(out))
    html = out.read_text()
    assert "unpkg.com/deck.gl@" in html          # map
    assert "CURVE = " in html and "<svg" in html  # synced impact chart
    assert "of trips severed" in html
    import json
    import re
    curve = json.loads(re.search(r"CURVE = (\[.*?\])", html, re.S).group(1))
    assert len(curve) == 11 and curve == sorted(curve)  # stages+1, monotonic


@requires_geopandas
def test_dashboard_html_rejects_stochastic(tmp_path):
    g = _coord_grid(5)
    with pytest.raises(TypeError):
        viz.dashboard_html(g, _stochastic(g), str(tmp_path / "x.html"))
