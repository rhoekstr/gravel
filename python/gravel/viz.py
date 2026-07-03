"""gravel.viz — fragility results as plot-ready GeoDataFrames and static maps.

**Tier 0 (data bridge):** convert a fragility result into a per-edge column you can hand
straight to ``gdf.plot(...)``, folium, pydeck, or lonboard — :func:`failure_geoframe`,
:func:`edge_failure_round`, :func:`edge_failure_frequency`.

**Tier 1 (static rendering):** :func:`plot_fragility` draws the researcher's *accurate*
artifact — a quantitative, colorblind-safe matplotlib choropleth of the per-edge failure trace,
with an optional hazard "why" layer underneath.

**Tier 2 (interactive):** :func:`interactive_map` returns a lonboard (WebGL) ``Map`` that scales
to county-size networks and exports to standalone HTML; :func:`animate_failure` returns a
Play/slider widget that scrubs the progressive removal order (failed edges recede to grey) — both
for exploration and public sharing.

Two per-edge failure traces are supported, matching the two models that produce an
edge-level outcome:

* **progressive** (:func:`gravel.progressive_fragility`, *greedy* strategies) — the ordinal
  **removal order**. ``failure_round[e]`` is the 1-based step at which edge ``e`` was
  removed, or ``NaN`` if it survived. This is the natural thing to *animate* (scrub rounds).
* **stochastic** (:func:`gravel.stochastic_fragility`) — the empirical **failure frequency**.
  ``failure_frequency[e]`` is the fraction of Monte-Carlo runs in which edge ``e`` failed.
  This is a *static* choropleth (per-edge P(fail)), not an animation: independent draws have
  no intrinsic order, and a single realization is one draw, not "the" answer.

Geometry: by default edges draw as straight node-to-node segments; pass ``edge_geometry`` (a
:class:`gravel.EdgeGeometry` from ``simplify_graph`` with ``emit_geometry`` set on its
``SimplificationConfig``) to :func:`failure_geoframe` / :func:`plot_fragility` for a faithful
map that follows the real road — important over a floodplain, where a straight chord can
misrepresent which roads sit in the hazard.

The GeoDataFrame path needs geopandas (``pip install gravel-fragility[interop]``); static
rendering also needs matplotlib (``gravel-fragility[viz]``); the raw per-edge array helpers need
only numpy.
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Any

import numpy as np

from ._gravel import ProgressiveFragilityResult, StochasticFragilityResult

if TYPE_CHECKING:  # pragma: no cover - typing only
    import geopandas as gpd

    from ._gravel import Graph


def edge_failure_round(graph: Graph, result: ProgressiveFragilityResult) -> np.ndarray:
    """Per-edge removal step from a greedy ``progressive_fragility`` result.

    Returns a ``float64`` array of length ``edge_count`` (CSR order): ``failure_round[e]`` is
    the 1-based step at which edge ``e`` was removed, or ``NaN`` if it was never removed.
    ``NaN`` (not 0) marks survivors so a colormap does not paint them as "failed first".

    Parallel edges (e.g. several degree-2 chains contracted between the same two junctions)
    share an ``(u, v)`` key; each successive removal of that pair consumes the next such edge,
    so counts and lengths stay aligned to ``edge_count``.

    The removal sequence is only populated for greedy strategies; for a Monte-Carlo
    progressive run it is empty and every edge comes back ``NaN``.
    """
    from collections import defaultdict, deque

    sources, targets, _ = graph.to_coo()
    edge_count = len(sources)
    # (u, v) -> queue of edge indices, so parallel edges each get their own round.
    buckets: dict[tuple[int, int], deque] = defaultdict(deque)
    for e, (u, v) in enumerate(zip(sources, targets, strict=True)):
        buckets[(int(u), int(v))].append(e)

    rounds = np.full(edge_count, np.nan, dtype=np.float64)
    for step, (u, v) in enumerate(result.removal_sequence, start=1):
        q = buckets.get((int(u), int(v)))
        if q:
            rounds[q.popleft()] = float(step)
    return rounds


def edge_failure_frequency(result: StochasticFragilityResult) -> np.ndarray:
    """Per-edge empirical failure probability from a ``stochastic_fragility`` result.

    Thin wrapper over ``result.edge_failure_frequency`` (fraction of runs each edge failed,
    CSR order) as a ``float64`` numpy array, for symmetry with :func:`edge_failure_round`.
    """
    return np.asarray(result.edge_failure_frequency, dtype=np.float64)


def failure_sequence_from_probabilities(
    edge_probabilities,
    *,
    limit: int | None = None,
    stages: int | None = None,
    seed: int | None = 0,
    exposure_order: bool = False,
) -> np.ndarray:
    """Build a per-edge ``failure_round`` from per-edge failure probabilities (CSR order).

    Turns a hazard probability array (e.g. from :func:`gravel.hazards.flood_edge_probabilities`)
    into an animatable removal order — so a flood/hazard scenario can drive
    :func:`animate_failure_html`, :func:`animate_failure`, or :func:`dashboard_html`.

    By default this is **one stochastic realization**: each edge fails with its own
    probability (RNG seeded by ``seed``), and the failed edges are ordered worst-exposure
    first ("as the flood rises, the most-exposed roads close first"). Set
    ``exposure_order=True`` for the deterministic variant (order every positive-probability
    edge by probability, ignoring the draw).

    Parameters
    ----------
    edge_probabilities : array-like
        Per-edge failure probability in CSR edge order (length ``edge_count``).
    limit : int, optional
        Cap the number of edges removed (keep the highest-exposure ``limit``).
    stages : int, optional
        Bucket the order into this many rounds (a watchable animation); default one round
        per removed edge.
    seed : int or None, optional
        RNG seed for the stochastic draw (default ``0`` for reproducibility). Ignored when
        ``exposure_order=True``.
    exposure_order : bool, optional
        Deterministic worst-exposure ordering instead of a stochastic realization.

    Returns
    -------
    numpy.ndarray
        ``float64`` ``failure_round`` (CSR order); ``NaN`` where an edge is never removed.
    """
    probs = np.asarray(edge_probabilities, dtype=np.float64)
    m = probs.shape[0]
    if exposure_order:
        candidates = [e for e in range(m) if probs[e] > 0.0]
    else:
        draws = np.random.default_rng(seed).random(m)
        candidates = [e for e in range(m) if probs[e] > 0.0 and draws[e] < probs[e]]
    candidates.sort(key=lambda e: (-probs[e], e))  # worst-exposure first
    if limit is not None:
        candidates = candidates[: int(limit)]

    rounds = np.full(m, np.nan, dtype=np.float64)
    n = len(candidates)
    if n:
        nstages = int(stages) if stages else n
        for rank, e in enumerate(candidates):
            rounds[e] = 1 + (rank * nstages) // n if stages else rank + 1
    return rounds


def connectivity_curve(graph: Graph, failure_round: np.ndarray) -> list[float]:
    """Fraction of node pairs disconnected at each removal stage (the dashboard metric).

    For each stage ``k`` (0 = nothing removed), removes every edge whose ``failure_round``
    is ``<= k`` and reports **the share of ordered node pairs that can no longer reach each
    other** — ``1 - Σ(component_size²) / n²`` — computed by union-find over the surviving
    edges. Returns a list of length ``max_round + 1`` (``curve[k]`` for stage ``k``); it is
    non-decreasing in ``k`` since removals only sever.

    ``failure_round`` is CSR-aligned (``NaN`` = never removed), e.g. from
    :func:`edge_failure_round` or :func:`failure_sequence_from_probabilities`.
    """
    fr = np.asarray(failure_round, dtype=np.float64)
    sources, targets, _ = graph.to_coo()
    n = int(graph.node_count)
    finite = fr[~np.isnan(fr)]
    max_round = int(finite.max()) if finite.size else 0

    parent = list(range(n))
    size = [1] * n

    def find(a: int) -> int:
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    def union(a: int, b: int) -> None:
        ra, rb = find(a), find(b)
        if ra == rb:
            return
        if size[ra] < size[rb]:
            ra, rb = rb, ra
        parent[rb] = ra
        size[ra] += size[rb]

    # Edges never removed are present at every stage; bucket the rest by round.
    by_round: dict[int, list[int]] = {}
    for e in range(len(sources)):
        r = fr[e]
        if np.isnan(r):
            union(int(sources[e]), int(targets[e]))
        else:
            by_round.setdefault(int(r), []).append(e)

    def severed_fraction() -> float:
        total, seen = 0, set()
        for v in range(n):
            root = find(v)
            if root not in seen:
                seen.add(root)
                total += size[root] * size[root]
        return 1.0 - total / (n * n) if n else 0.0

    # Walk stages high->low, adding edges back (incremental union): at stage k the edges
    # with round == k+1 are present again.
    curve = [0.0] * (max_round + 1)
    curve[max_round] = severed_fraction()
    for k in range(max_round - 1, -1, -1):
        for e in by_round.get(k + 1, ()):
            union(int(sources[e]), int(targets[e]))
        curve[k] = severed_fraction()
    return curve


def _failure_column(graph: Graph, result: Any) -> tuple[str, np.ndarray]:
    """Resolve ``(column_name, per-edge values)`` for a result or a raw failure_round array."""
    if isinstance(result, np.ndarray):
        arr = np.asarray(result, dtype=np.float64)
        if arr.shape != (graph.edge_count,):
            raise ValueError(
                f"failure_round array has {arr.shape} entries but the graph has "
                f"{graph.edge_count} edges."
            )
        return "failure_round", arr
    if isinstance(result, ProgressiveFragilityResult):
        return "failure_round", edge_failure_round(graph, result)
    if isinstance(result, StochasticFragilityResult):
        return "failure_frequency", edge_failure_frequency(result)
    raise TypeError(
        "expected a ProgressiveFragilityResult, StochasticFragilityResult, or a "
        f"failure_round numpy array; got {type(result).__name__}"
    )


def failure_geoframe(
    graph: Graph,
    result: Any,
    *,
    metadata: Any | None = None,
    edge_geometry: Any | None = None,
    crs: str = "EPSG:4326",
) -> gpd.GeoDataFrame:
    """Build an edge ``GeoDataFrame`` carrying the result's per-edge failure trace.

    Dispatches on ``result``:

    * :class:`ProgressiveFragilityResult` → a ``failure_round`` column (see
      :func:`edge_failure_round`).
    * :class:`StochasticFragilityResult` → a ``failure_frequency`` column (see
      :func:`edge_failure_frequency`).

    The frame is ready for ``gdf.plot(column=..., cmap="viridis")`` (use a colorblind-safe
    sequential colormap; avoid red→green). Needs geopandas.

    Parameters
    ----------
    edge_geometry:
        Optional :class:`gravel.EdgeGeometry` (from ``simplify_graph`` with ``emit_geometry``
        set on its ``SimplificationConfig``), CSR-aligned to ``graph``. When given, edges are
        drawn along their true road shape instead of straight chords.

    Raises
    ------
    TypeError
        If ``result`` is not a progressive or stochastic fragility result.
    ImportError
        If geopandas is not installed (``pip install gravel-fragility[interop]``).
    """
    from . import interop  # lazy: pulls geopandas only when a frame is requested

    column, values = _failure_column(graph, result)
    return interop.to_geodataframe(
        graph,
        metadata=metadata,
        edge_values={column: values},
        edge_geometry=edge_geometry,
        crs=crs,
    )


def plot_fragility(
    graph: Graph,
    result: Any,
    *,
    edge_geometry: Any | None = None,
    hazard: Any | None = None,
    hazard_column: str | None = None,
    ax: Any | None = None,
    cmap: str = "viridis",
    linewidth: float = 0.8,
    legend: bool = True,
    title: str | None = None,
    missing_color: str = "#b4b4b4",  # muted grey (180,180,180), consistent across tiers
    metadata: Any | None = None,
    crs: str = "EPSG:4326",
) -> Any:
    """Render a static fragility map (Tier 1) and return the matplotlib ``Axes``.

    Edges are colored by the result's per-edge failure trace — ``failure_round`` for a greedy
    :class:`ProgressiveFragilityResult`, ``failure_frequency`` for a
    :class:`StochasticFragilityResult`. This is the *researcher's accurate artifact*: a
    quantitative choropleth on a colorblind-safe sequential colormap (default ``viridis``;
    avoid red→green). Progressive survivors (``NaN`` round) are drawn in ``missing_color`` so a
    colormap does not paint them as "failed first".

    Parameters
    ----------
    edge_geometry:
        Optional :class:`gravel.EdgeGeometry`; when given, edges follow the real road shape.
    hazard:
        Optional geopandas ``GeoDataFrame`` of risk geometry (e.g. a floodplain) drawn as the
        base "why" layer under the network — the causal input behind the failure pattern.
    hazard_column:
        Optional column of ``hazard`` to shade severity by (sequential ``OrRd``); a uniform
        translucent fill is used when omitted.
    ax:
        Existing matplotlib ``Axes`` to draw on; a new figure is created when ``None``.
    cmap, linewidth, legend, title, missing_color:
        Standard styling knobs passed through to the edge plot.

    Returns
    -------
    matplotlib.axes.Axes

    Raises
    ------
    ImportError
        If matplotlib is not installed (``pip install gravel-fragility[viz]``).
    """
    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:  # pragma: no cover - environment dependent
        raise ImportError(
            "plot_fragility needs matplotlib: pip install gravel-fragility[viz]"
        ) from exc

    column, _ = _failure_column(graph, result)
    gdf = failure_geoframe(
        graph, result, metadata=metadata, edge_geometry=edge_geometry, crs=crs
    )

    if ax is None:
        _, ax = plt.subplots(figsize=(9, 9))

    # Base "why" layer: the hazard geometry that drives the failure pattern.
    if hazard is not None:
        if hazard_column is not None:
            hazard.plot(ax=ax, column=hazard_column, cmap="OrRd", alpha=0.35, zorder=1)
        else:
            hazard.plot(ax=ax, color="#d9a441", alpha=0.25, edgecolor="none", zorder=1)

    plot_kwargs: dict[str, Any] = {
        "ax": ax,
        "column": column,
        "cmap": cmap,
        "linewidth": linewidth,
        "legend": legend,
        "zorder": 2,
    }
    if column == "failure_round":
        # Survivors are NaN; grey them rather than mapping to the "failed first" end.
        plot_kwargs["missing_kwds"] = {"color": missing_color, "linewidth": linewidth}
    gdf.plot(**plot_kwargs)

    if title:
        ax.set_title(title)
    return ax


def _cmap_rgb(values: np.ndarray, cmap: str, missing_color) -> np.ndarray:
    """Map per-edge values to an (N, 3) uint8 RGB array; non-finite → ``missing_color``."""
    from lonboard.colormap import apply_continuous_cmap
    from matplotlib import colormaps, colors

    finite = np.isfinite(values)
    if finite.any():
        lo = float(np.min(values[finite]))
        hi = float(np.max(values[finite]))
        norm = colors.Normalize(vmin=lo, vmax=hi if hi > lo else lo + 1.0)
        scaled = np.where(finite, norm(np.where(finite, values, lo)), 0.0)
    else:
        scaled = np.zeros_like(values)
    rgb = apply_continuous_cmap(scaled, colormaps[cmap])
    rgb[~finite] = list(missing_color)  # survivors / undefined → grey
    return rgb


def interactive_map(
    graph: Graph,
    result: Any,
    *,
    edge_geometry: Any | None = None,
    hazard: Any | None = None,
    cmap: str = "viridis",
    width_min_pixels: float = 1.5,
    missing_color: tuple[int, int, int] = (180, 180, 180),
    metadata: Any | None = None,
    crs: str = "EPSG:4326",
) -> Any:
    """Interactive WebGL fragility map (Tier 2) — returns a lonboard ``Map``.

    Renders the per-edge failure trace (``failure_round`` for greedy progressive,
    ``failure_frequency`` for stochastic) as GPU-drawn paths on a pan/zoom basemap. Scales to
    county-size networks via GeoArrow transport. Display it in a notebook, or call
    ``m.to_html("map.html")`` for a shareable standalone file.

    Parameters mirror :func:`plot_fragility`: ``edge_geometry`` draws real road shape, ``hazard``
    (a geopandas ``GeoDataFrame``) is drawn as a translucent base "why" layer, ``cmap`` is a
    colorblind-safe sequential colormap, and non-failing edges use ``missing_color``.

    Returns
    -------
    lonboard.Map

    Raises
    ------
    ImportError
        If lonboard is not installed (``pip install gravel-fragility[viz]``).
    """
    try:
        from lonboard import Map, PathLayer, PolygonLayer
    except ImportError as exc:  # pragma: no cover - environment dependent
        raise ImportError(
            "interactive_map needs lonboard: pip install gravel-fragility[viz]"
        ) from exc

    column, _ = _failure_column(graph, result)
    gdf = failure_geoframe(
        graph, result, metadata=metadata, edge_geometry=edge_geometry, crs=crs
    )
    rgb = _cmap_rgb(np.asarray(gdf[column], dtype=float), cmap, missing_color)

    layers = []
    if hazard is not None:  # translucent base "why" layer, drawn underneath
        layers.append(
            PolygonLayer.from_geopandas(
                hazard, get_fill_color=[217, 164, 65, 70], get_line_color=[217, 164, 65, 160]
            )
        )
    edges = PathLayer.from_geopandas(gdf, width_min_pixels=width_min_pixels)
    edges.get_color = rgb
    layers.append(edges)

    return Map(layers)


def _failure_colors(
    failure_round: np.ndarray,
    k: int,
    *,
    active_color=(31, 119, 180),
    failed_color=(180, 180, 180),
) -> np.ndarray:
    """Binary per-edge (N, 3) uint8 colors for animation round ``k``.

    Edges whose ``failure_round <= k`` are ``failed_color`` (grey, receding); everything else
    — including survivors (``NaN``) — is ``active_color``. Pure function (the animation's core).
    """
    fr = np.asarray(failure_round, dtype=float)
    failed = np.isfinite(fr) & (fr <= k)
    rgb = np.tile(np.array(active_color, dtype=np.uint8), (fr.shape[0], 1))
    rgb[failed] = failed_color
    return rgb


def animate_failure(
    graph: Graph,
    result: Any,
    *,
    edge_geometry: Any | None = None,
    hazard: Any | None = None,
    active_color: tuple[int, int, int] = (31, 119, 180),
    failed_color: tuple[int, int, int] = (180, 180, 180),
    width_min_pixels: float = 1.5,
    interval_ms: int = 400,
    metadata: Any | None = None,
    crs: str = "EPSG:4326",
) -> Any:
    """Animated failure playback (Tier 2) — returns an ipywidgets widget over a lonboard map.

    Scrubs the **progressive removal order**: at round *k*, edges removed by then go grey
    (receding) while the still-active network stays ``active_color``; survivors never grey. Each
    frame only updates the color array (data is sent once via GeoArrow), so it stays smooth at
    county scale. Display the returned widget in a notebook and press play, or drag the slider.

    Requires a **greedy** :class:`ProgressiveFragilityResult` — stochastic results have no failure
    order (use :func:`interactive_map` for their static P(fail) choropleth).

    Returns
    -------
    ipywidgets.VBox
        Play/slider controls above an interactive lonboard ``Map``.

    Raises
    ------
    TypeError
        If ``result`` is not a progressive fragility result.
    ImportError
        If lonboard / ipywidgets are not installed (``pip install gravel-fragility[viz]``).
    """
    if not isinstance(result, (ProgressiveFragilityResult, np.ndarray)):
        raise TypeError(
            "animate_failure needs a ProgressiveFragilityResult (greedy) or a failure_round "
            "array (e.g. from failure_sequence_from_probabilities); a stochastic result has "
            "no order — use interactive_map for its static P(fail) map."
        )
    try:
        import ipywidgets as widgets
        from lonboard import Map, PathLayer, PolygonLayer
    except ImportError as exc:  # pragma: no cover - environment dependent
        raise ImportError(
            "animate_failure needs lonboard + ipywidgets: pip install gravel-fragility[viz]"
        ) from exc

    gdf = failure_geoframe(
        graph, result, metadata=metadata, edge_geometry=edge_geometry, crs=crs
    )
    fr = np.asarray(gdf["failure_round"], dtype=float)
    finite = np.isfinite(fr)
    max_round = int(np.max(fr[finite])) if finite.any() else 0

    layers = []
    if hazard is not None:
        layers.append(
            PolygonLayer.from_geopandas(
                hazard, get_fill_color=[217, 164, 65, 70], get_line_color=[217, 164, 65, 160]
            )
        )
    edges = PathLayer.from_geopandas(gdf, width_min_pixels=width_min_pixels)
    edges.get_color = _failure_colors(
        fr, 0, active_color=active_color, failed_color=failed_color
    )
    layers.append(edges)
    m = Map(layers)

    slider = widgets.IntSlider(min=0, max=max_round, value=0, description="round")
    play = widgets.Play(min=0, max=max_round, value=0, interval=interval_ms)
    widgets.jslink((play, "value"), (slider, "value"))

    def _on_round(change):
        edges.get_color = _failure_colors(
            fr, change["new"], active_color=active_color, failed_color=failed_color
        )

    slider.observe(_on_round, names="value")
    return widgets.VBox([widgets.HBox([play, slider]), m])


_DECK_HTML_TEMPLATE = """<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Gravel — failure animation</title>
<script src="https://unpkg.com/deck.gl@__DECKVER__/dist.min.js"></script>
<style>
  html, body { margin: 0; height: 100%; font-family: system-ui, sans-serif; }
  #deck-canvas { width: 100vw; height: 100vh; background: #0b0b0f; }
  #controls { position: fixed; left: 12px; bottom: 12px; z-index: 1; display: flex;
    gap: 10px; align-items: center; background: rgba(20,20,28,.85); color: #eee;
    padding: 10px 14px; border-radius: 8px; }
  #controls button { cursor: pointer; }
  #round { width: 300px; }
</style>
</head>
<body>
<canvas id="deck-canvas"></canvas>
<div id="controls">
  <button id="play">&#9654; play</button>
  <input id="round" type="range" min="0" max="__MAXROUND__" value="0" step="1">
  <span id="label">round 0 / __MAXROUND__</span>
</div>
<script>
const EDGES = __EDGES__;
const HAZARD = __HAZARD__;
const ACTIVE = __ACTIVE__;
const FAILED = __FAILED__;
const MAXROUND = __MAXROUND__;
const INTERVAL = __INTERVAL__;
const WIDTH = __WIDTH__;
const VIEW = __VIEW__;
let current = 0, timer = null;

function makeLayers(round) {
  const layers = [];
  if (HAZARD) {
    layers.push(new deck.GeoJsonLayer({
      id: 'hazard', data: HAZARD, filled: true, stroked: true, lineWidthMinPixels: 1,
      getFillColor: [217, 164, 65, 70], getLineColor: [217, 164, 65, 160]
    }));
  }
  layers.push(new deck.PathLayer({
    id: 'edges', data: EDGES, getPath: d => d.path,
    getColor: d => (d.round !== null && d.round <= round) ? FAILED : ACTIVE,
    widthMinPixels: WIDTH, capRounded: true, jointRounded: true,
    updateTriggers: { getColor: round }
  }));
  return layers;
}

const deckgl = new deck.Deck({
  canvas: document.getElementById('deck-canvas'),
  initialViewState: VIEW, controller: true, layers: makeLayers(0)
});

const slider = document.getElementById('round');
const label = document.getElementById('label');
const playBtn = document.getElementById('play');

function setRound(r) {
  current = r;
  slider.value = r;
  label.textContent = 'round ' + r + ' / ' + MAXROUND;
  deckgl.setProps({ layers: makeLayers(r) });
}
slider.addEventListener('input', () => setRound(+slider.value));
playBtn.addEventListener('click', () => {
  if (timer) { clearInterval(timer); timer = null; playBtn.innerHTML = '&#9654; play'; return; }
  playBtn.innerHTML = '&#10073;&#10073; pause';
  timer = setInterval(() => setRound(current + 1 > MAXROUND ? 0 : current + 1), INTERVAL);
});
</script>
</body>
</html>
"""


def animate_failure_html(
    graph: Graph,
    result: Any,
    path: str,
    *,
    edge_geometry: Any | None = None,
    hazard: Any | None = None,
    active_color: tuple[int, int, int] = (31, 119, 180),
    failed_color: tuple[int, int, int] = (180, 180, 180),
    width_min_pixels: float = 1.5,
    interval_ms: int = 400,
    deckgl_version: str = "9",
    metadata: Any | None = None,
    crs: str = "EPSG:4326",
) -> str:
    """Write a **self-contained animated HTML** of the progressive removal order and return ``path``.

    Unlike :func:`animate_failure` (a notebook widget needing a live kernel), this bakes a
    standalone file: deck.gl (loaded from a CDN) plays/scrubs the failure sequence entirely
    client-side — at round *k*, edges removed by then turn ``failed_color`` while the active
    network stays ``active_color`` (survivors never change). Geometry is embedded once and each
    frame only re-evaluates the color accessor (``updateTriggers`` keyed to the round), so it
    stays responsive. Open the file in any browser and press play — no server, no kernel.

    Requires a **greedy** :class:`ProgressiveFragilityResult`. ``edge_geometry`` embeds real road
    shape; ``hazard`` (a geopandas ``GeoDataFrame``) is drawn as a translucent base layer.

    Note: geometry is embedded as JSON, so a county-scale network makes a large HTML file — this
    is a deliberate share/export artifact, not a live analysis view.

    Returns
    -------
    str
        The ``path`` written.

    Raises
    ------
    TypeError
        If ``result`` is not a progressive fragility result.
    ImportError
        If geopandas is not installed (``pip install gravel-fragility[interop]``).
    """
    import json
    import math
    from pathlib import Path as _Path

    if not isinstance(result, (ProgressiveFragilityResult, np.ndarray)):
        raise TypeError(
            "animate_failure_html needs a ProgressiveFragilityResult (greedy) or a "
            "failure_round array (e.g. from failure_sequence_from_probabilities); a "
            "stochastic result has no order — use interactive_map for its static P(fail) map."
        )

    gdf = failure_geoframe(
        graph, result, metadata=metadata, edge_geometry=edge_geometry, crs=crs
    )
    edges = []
    for geom, r in zip(gdf.geometry, gdf["failure_round"], strict=True):
        rnd = None if (r is None or (isinstance(r, float) and math.isnan(r))) else int(r)
        edges.append({"path": [[float(x), float(y)] for x, y in geom.coords], "round": rnd})

    rounds = [e["round"] for e in edges if e["round"] is not None]
    max_round = max(rounds) if rounds else 0

    minx, miny, maxx, maxy = (float(v) for v in gdf.total_bounds)
    span = max(maxx - minx, maxy - miny) or 1e-3
    zoom = max(1.0, min(18.0, math.log2(360.0 / span) - 1.0))
    view = {
        "longitude": (minx + maxx) / 2, "latitude": (miny + maxy) / 2,
        "zoom": zoom, "pitch": 0, "bearing": 0,
    }
    hazard_json = "null"
    if hazard is not None:
        hazard_json = hazard.to_crs(crs).to_json()

    html = (
        _DECK_HTML_TEMPLATE
        .replace("__DECKVER__", str(deckgl_version))
        .replace("__EDGES__", json.dumps(edges))
        .replace("__HAZARD__", hazard_json)
        .replace("__ACTIVE__", json.dumps(list(active_color)))
        .replace("__FAILED__", json.dumps(list(failed_color)))
        .replace("__MAXROUND__", str(max_round))
        .replace("__INTERVAL__", str(int(interval_ms)))
        .replace("__WIDTH__", json.dumps(width_min_pixels))
        .replace("__VIEW__", json.dumps(view))
    )
    _Path(path).write_text(html, encoding="utf-8")
    return path


_DASHBOARD_HTML_TEMPLATE = """<!doctype html>
<html>
<head>
<meta charset="utf-8" />
<title>__TITLE__</title>
<script src="https://unpkg.com/deck.gl@__DECKVER__/dist.min.js"></script>
<style>
  html, body { margin: 0; height: 100%; font-family: system-ui, sans-serif; }
  #wrap { display: flex; flex-direction: column; height: 100vh; }
  #main { flex: 1; position: relative; }
  #map { position: absolute; inset: 0; }
  #panel { height: 200px; border-top: 1px solid #ddd; padding: 8px 14px 12px;
           box-sizing: border-box; background: #fafafa; }
  #head { display: flex; align-items: baseline; gap: 12px; margin-bottom: 4px; }
  #title { font-weight: 600; }
  #readout { color: #c0392b; font-variant-numeric: tabular-nums; }
  #controls { display: flex; align-items: center; gap: 10px; margin: 6px 0; }
  #slider { flex: 1; }
  #chart { width: 100%; height: 120px; display: block; }
  button { font-size: 14px; padding: 2px 12px; cursor: pointer; }
</style>
</head>
<body>
<div id="wrap">
  <div id="main"><canvas id="map"></canvas></div>
  <div id="panel">
    <div id="head"><span id="title">__TITLE__</span><span id="readout"></span></div>
    <div id="controls">
      <button id="play">&#9654; play</button>
      <input id="slider" type="range" min="0" max="__MAXROUND__" value="0" step="1" />
    </div>
    <svg id="chart" viewBox="0 0 800 120" preserveAspectRatio="none"></svg>
  </div>
</div>
<script>
const EDGES = __EDGES__, HAZARD = __HAZARD__, CURVE = __CURVE__, VIEW = __VIEW__;
const ACTIVE = __ACTIVE__, FAILED = __FAILED__;
const MAXROUND = __MAXROUND__, INTERVAL = __INTERVAL__, WIDTH = __WIDTH__;
const { Deck, PathLayer, GeoJsonLayer } = deck;

function layers(round) {
  const ls = [];
  if (HAZARD) {
    ls.push(new GeoJsonLayer({
      id: "hazard", data: HAZARD, filled: true, stroked: true,
      getFillColor: f => (f.properties && f.properties._color) || [217, 164, 65, 70],
      getLineColor: [120, 90, 20, 110], lineWidthMinPixels: 0.5,
    }));
  }
  ls.push(new PathLayer({
    id: "edges", data: EDGES, getPath: d => d.path,
    getColor: d => (d.round !== null && d.round <= round) ? FAILED : ACTIVE,
    widthMinPixels: WIDTH, updateTriggers: { getColor: round },
  }));
  return ls;
}
const deckgl = new Deck({ canvas: "map", initialViewState: VIEW, controller: true, layers: layers(0) });

const NS = "http://www.w3.org/2000/svg", chart = document.getElementById("chart");
const W = 800, H = 120, PAD = 8;
const sx = i => PAD + i * (W - 2 * PAD) / Math.max(1, MAXROUND);
const sy = v => (H - PAD) - v * (H - 2 * PAD);
function svg(tag, attrs) {
  const el = document.createElementNS(NS, tag);
  for (const k in attrs) el.setAttribute(k, attrs[k]);
  return el;
}
[0.25, 0.5, 0.75, 1.0].forEach(g => {
  chart.appendChild(svg("line", { x1: PAD, x2: W - PAD, y1: sy(g), y2: sy(g),
    stroke: "#e2e2e2", "stroke-width": 1 }));
});
chart.appendChild(svg("polyline", {
  points: CURVE.map((v, i) => sx(i) + "," + sy(v)).join(" "),
  fill: "none", stroke: "#c0392b", "stroke-width": 2,
}));
const mline = svg("line", { stroke: "#333", "stroke-width": 1 });
const dot = svg("circle", { r: 3.5, fill: "#c0392b" });
chart.appendChild(mline); chart.appendChild(dot);
const slider = document.getElementById("slider"), readout = document.getElementById("readout");

let current = 0;
function setRound(k) {
  current = k;
  deckgl.setProps({ layers: layers(k) });
  const sev = CURVE[k] !== undefined ? CURVE[k] : 0;
  readout.textContent = "stage " + k + " / " + MAXROUND + " \\u00b7 " + (sev * 100).toFixed(1) + "% of trips severed";
  mline.setAttribute("x1", sx(k)); mline.setAttribute("x2", sx(k));
  mline.setAttribute("y1", PAD); mline.setAttribute("y2", H - PAD);
  dot.setAttribute("cx", sx(k)); dot.setAttribute("cy", sy(sev));
  slider.value = k;
}
slider.addEventListener("input", e => setRound(+e.target.value));
let playing = false, timer = null;
const btn = document.getElementById("play");
btn.addEventListener("click", () => {
  playing = !playing;
  btn.innerHTML = playing ? "&#10073;&#10073; pause" : "&#9654; play";
  if (playing) timer = setInterval(() => setRound(current + 1 > MAXROUND ? 0 : current + 1), INTERVAL);
  else clearInterval(timer);
});
setRound(0);
</script>
</body>
</html>
"""


def dashboard_html(
    graph: Graph,
    result: Any,
    path: str,
    *,
    edge_geometry: Any | None = None,
    hazard: Any | None = None,
    hazard_zone_field: str | None = None,
    title: str = "Road fragility under progressive failure",
    active_color: tuple[int, int, int] = (31, 119, 180),
    failed_color: tuple[int, int, int] = (180, 180, 180),
    width_min_pixels: float = 1.6,
    interval_ms: int = 350,
    deckgl_version: str = "9",
    metadata: Any | None = None,
    crs: str = "EPSG:4326",
) -> str:
    """Write a **self-contained fragility dashboard** (map + synced impact chart) and return ``path``.

    Two panels in one standalone HTML file (deck.gl from a CDN, no server/kernel): a map that
    plays/scrubs the removal sequence (failed roads recede to ``failed_color``) and, below it, an
    inline chart of **% of trips severed vs stage** (:func:`connectivity_curve`) with a marker and
    readout locked to the same play/slider.

    ``result`` is a greedy :class:`ProgressiveFragilityResult` **or** a ``failure_round`` array
    (e.g. a flood order from :func:`failure_sequence_from_probabilities`) — a stochastic result
    has no order and is rejected. ``edge_geometry`` draws real road shape; ``hazard`` is a base
    layer, and ``hazard_zone_field`` (e.g. ``"FLD_ZONE"``) colors it by
    :func:`gravel.hazards.nfhl_zone_color` severity instead of a flat fill.

    Returns
    -------
    str
        The ``path`` written.
    """
    import json
    import math
    from pathlib import Path as _Path

    column, values = _failure_column(graph, result)
    if column != "failure_round":
        raise TypeError(
            "dashboard_html needs a removal ORDER — a greedy ProgressiveFragilityResult or a "
            "failure_round array (e.g. failure_sequence_from_probabilities). A stochastic result "
            "has no order; use interactive_map for its static P(fail) map."
        )
    failure_round = values
    curve = connectivity_curve(graph, failure_round)
    max_round = len(curve) - 1

    gdf = failure_geoframe(
        graph, failure_round, metadata=metadata, edge_geometry=edge_geometry, crs=crs
    )
    edges = []
    for geom, r in zip(gdf.geometry, gdf["failure_round"], strict=True):
        rnd = None if (r is None or (isinstance(r, float) and math.isnan(r))) else int(r)
        edges.append({"path": [[float(x), float(y)] for x, y in geom.coords], "round": rnd})

    minx, miny, maxx, maxy = (float(v) for v in gdf.total_bounds)
    span = max(maxx - minx, maxy - miny) or 1e-3
    view = {
        "longitude": (minx + maxx) / 2, "latitude": (miny + maxy) / 2,
        "zoom": max(1.0, min(18.0, math.log2(360.0 / span) - 1.0)), "pitch": 0, "bearing": 0,
    }

    hazard_json = "null"
    if hazard is not None:
        hz = hazard.to_crs(crs)
        if hazard_zone_field and hazard_zone_field in hz.columns:
            from .hazards import nfhl_zone_color
            hz = hz.copy()
            hz["_color"] = [nfhl_zone_color(z) for z in hz[hazard_zone_field]]
        hazard_json = hz.to_json()

    html = (
        _DASHBOARD_HTML_TEMPLATE
        .replace("__TITLE__", title)
        .replace("__DECKVER__", str(deckgl_version))
        .replace("__EDGES__", json.dumps(edges))
        .replace("__HAZARD__", hazard_json)
        .replace("__CURVE__", json.dumps([round(float(c), 5) for c in curve]))
        .replace("__ACTIVE__", json.dumps(list(active_color)))
        .replace("__FAILED__", json.dumps(list(failed_color)))
        .replace("__MAXROUND__", str(max_round))
        .replace("__INTERVAL__", str(int(interval_ms)))
        .replace("__WIDTH__", json.dumps(width_min_pixels))
        .replace("__VIEW__", json.dumps(view))
    )
    _Path(path).write_text(html, encoding="utf-8")
    return path
