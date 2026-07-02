"""gravel.viz — fragility results as plot-ready GeoDataFrames and static maps.

**Tier 0 (data bridge):** convert a fragility result into a per-edge column you can hand
straight to ``gdf.plot(...)``, folium, pydeck, or lonboard — :func:`failure_geoframe`,
:func:`edge_failure_round`, :func:`edge_failure_frequency`.

**Tier 1 (static rendering):** :func:`plot_fragility` draws the researcher's *accurate*
artifact — a quantitative, colorblind-safe matplotlib choropleth of the per-edge failure trace,
with an optional hazard "why" layer underneath.

**Tier 2 (interactive):** :func:`interactive_map` returns a lonboard (WebGL) ``Map`` that scales
to county-size networks and exports to standalone HTML — for exploration and public sharing.
Animated failure playback builds on this next.

Two per-edge failure traces are supported, matching the two models that produce an
edge-level outcome:

* **progressive** (:func:`gravel.progressive_fragility`, *greedy* strategies) — the ordinal
  **removal order**. ``failure_round[e]`` is the 1-based step at which edge ``e`` was
  removed, or ``NaN`` if it survived. This is the natural thing to *animate* (scrub rounds).
* **stochastic** (:func:`gravel.stochastic_fragility`) — the empirical **failure frequency**.
  ``failure_frequency[e]`` is the fraction of Monte-Carlo runs in which edge ``e`` failed.
  This is a *static* choropleth (per-edge P(fail)), not an animation: independent draws have
  no intrinsic order, and a single realization is one draw, not "the" answer.

Geometry: by default edges draw as straight node-to-node segments; pass ``edge_geometry`` (from
``simplify_graph(..., emit_geometry=True)``) to :func:`failure_geoframe` / :func:`plot_fragility`
for a faithful map that follows the real road — important over a floodplain, where a straight
chord can misrepresent which roads sit in the hazard.

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


def _edge_index_map(graph: Graph) -> dict[tuple[int, int], int]:
    """Map each directed CSR edge ``(u, v)`` to its edge index."""
    sources, targets, _ = graph.to_coo()
    return {(int(u), int(v)): e for e, (u, v) in enumerate(zip(sources, targets, strict=True))}


def edge_failure_round(graph: Graph, result: ProgressiveFragilityResult) -> np.ndarray:
    """Per-edge removal step from a greedy ``progressive_fragility`` result.

    Returns a ``float64`` array of length ``edge_count`` (CSR order): ``failure_round[e]`` is
    the 1-based step at which edge ``e`` was removed, or ``NaN`` if it was never removed.
    ``NaN`` (not 0) marks survivors so a colormap does not paint them as "failed first".

    The removal sequence is only populated for greedy strategies; for a Monte-Carlo
    progressive run it is empty and every edge comes back ``NaN``.
    """
    edge_index = _edge_index_map(graph)
    rounds = np.full(len(edge_index), np.nan, dtype=np.float64)
    for step, (u, v) in enumerate(result.removal_sequence, start=1):
        e = edge_index.get((int(u), int(v)))
        if e is not None:
            rounds[e] = float(step)
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
        Optional :class:`gravel.EdgeGeometry` (from ``simplify_graph`` with
        ``emit_geometry=True``, CSR-aligned to ``graph``). When given, edges are drawn along
        their true road shape instead of straight chords.

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
    missing_color: str = "lightgray",
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
    missing_color: tuple[int, int, int] = (200, 200, 200),
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
