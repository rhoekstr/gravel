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


def _failure_column(graph: Graph, result: Any) -> tuple[str, np.ndarray]:
    """Resolve ``(column_name, per-edge values)`` for a progressive/stochastic result."""
    if isinstance(result, ProgressiveFragilityResult):
        return "failure_round", edge_failure_round(graph, result)
    if isinstance(result, StochasticFragilityResult):
        return "failure_frequency", edge_failure_frequency(result)
    raise TypeError(
        "expected a ProgressiveFragilityResult or StochasticFragilityResult, "
        f"got {type(result).__name__}"
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
    if not isinstance(result, ProgressiveFragilityResult):
        raise TypeError(
            "animate_failure needs a ProgressiveFragilityResult from a greedy "
            "progressive_fragility run; stochastic results have no failure order — "
            "use interactive_map for their static P(fail) map."
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

    if not isinstance(result, ProgressiveFragilityResult):
        raise TypeError(
            "animate_failure_html needs a ProgressiveFragilityResult from a greedy "
            "progressive_fragility run; stochastic results have no failure order — "
            "use interactive_map for their static P(fail) map."
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
