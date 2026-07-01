"""gravel.viz — turn fragility results into plot-ready GeoDataFrames (the data bridge).

This is Tier 0 of Gravel's visualization support: it converts a fragility result into a
per-edge column you can hand straight to ``gdf.plot(...)``, folium, pydeck, or lonboard.
The *rendering* helpers — static publication-quality figures for researchers, and
interactive animated maps for exploration and public communication — land in a later
release. Today this module gives you the frame; you bring the renderer.

Two per-edge failure traces are supported, matching the two models that produce an
edge-level outcome:

* **progressive** (:func:`gravel.progressive_fragility`, *greedy* strategies) — the ordinal
  **removal order**. ``failure_round[e]`` is the 1-based step at which edge ``e`` was
  removed, or ``NaN`` if it survived. This is the natural thing to *animate* (scrub rounds).
* **stochastic** (:func:`gravel.stochastic_fragility`) — the empirical **failure frequency**.
  ``failure_frequency[e]`` is the fraction of Monte-Carlo runs in which edge ``e`` failed.
  This is a *static* choropleth (per-edge P(fail)), not an animation: independent draws have
  no intrinsic order, and a single realization is one draw, not "the" answer.

Geometry caveat (inherited from :func:`gravel.interop.to_geodataframe`): edges are drawn as
straight segments between node coordinates, so a map is *schematic* until real per-edge road
geometry lands. Over a floodplain that can misrepresent which roads sit in the hazard —
read the picture accordingly.

The GeoDataFrame path needs geopandas (``pip install gravel-fragility[interop]``); the raw
per-edge array helpers need only numpy.
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


def failure_geoframe(
    graph: Graph,
    result: Any,
    *,
    metadata: Any | None = None,
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

    Raises
    ------
    TypeError
        If ``result`` is not a progressive or stochastic fragility result.
    ImportError
        If geopandas is not installed (``pip install gravel-fragility[interop]``).
    """
    from . import interop  # lazy: pulls geopandas only when a frame is requested

    if isinstance(result, ProgressiveFragilityResult):
        column, values = "failure_round", edge_failure_round(graph, result)
    elif isinstance(result, StochasticFragilityResult):
        column, values = "failure_frequency", edge_failure_frequency(result)
    else:
        raise TypeError(
            "failure_geoframe expects a ProgressiveFragilityResult or "
            f"StochasticFragilityResult, got {type(result).__name__}"
        )

    return interop.to_geodataframe(graph, metadata=metadata, edge_values={column: values}, crs=crs)
