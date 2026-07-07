"""GTFS Schedule static transit feed (``gravel.datasets.gtfs``).

Load a GTFS static feed (a directory of extracted ``.txt`` files) into a transit
network: stops become nodes (lat/lon), consecutive stop-time hops become edges,
and each edge carries a schedule-derived **capacity** = a persons/hour throughput
proxy (per-hop vehicle frequency × a per-mode vehicle-capacity assumption). The
capacity model is disclosed and adjustable; GTFS has no native capacity field.
Bring-your-own feed: download + unzip it (e.g. from Transitland or the agency),
then pass the directory path to :func:`load`.
"""

from __future__ import annotations

import numpy as np

from .. import _gravel


def load(directory, *, window_hours=18.0, capacity_model=None):
    """Load a GTFS feed directory into a transit graph.

    Returns ``(Graph, capacity)`` — the graph carries stop coordinates and
    ``capacity`` is the persons/hour throughput proxy (CSR-aligned float64).
    ``window_hours`` is the frequency denominator (default 18h service span).
    ``capacity_model`` is an optional :class:`gravel.GtfsCapacityModel` overriding
    the per-mode vehicle-capacity assumptions.
    """
    cfg = _gravel.GtfsConfig()
    cfg.dir = directory
    cfg.window_hours = float(window_hours)
    if capacity_model is not None:
        cfg.capacity_model = capacity_model
    graph, capacity = _gravel.load_gtfs_network(cfg)
    return graph, np.asarray(capacity, dtype=np.float64)
