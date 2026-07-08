"""BTS T-100 airline capacity overlay (``gravel.datasets.t100``).

BTS T-100 Segment reports per-segment ``(origin, destination)`` seats, passengers,
and departures by carrier and month. This module turns it into a **per-edge
capacity** for an OpenFlights air network via a key-join on the ordered IATA
airport pair. It is an *attribute overlay* (a tabular capacity join), not a graph
loader — pair it with :mod:`gravel.datasets.openflights`.

Bring-your-own CSV: download a T-100 Segment extract from BTS TranStats
(https://www.transtats.bts.gov/, "Air Carriers : T-100 Segment"), then::

    graph, _, codes = gravel.datasets.openflights.load(airports, routes, with_codes=True)
    seats = gravel.datasets.t100.load("T_T100_SEGMENT.csv")     # (origin, dest) -> seats
    capacity = gravel.datasets.t100.edge_capacity(graph, codes, seats)  # CSR-aligned
"""

from __future__ import annotations

import csv

import numpy as np


def load(csv_path, *, origin_field="ORIGIN", dest_field="DEST", value_field="SEATS"):
    """Read a BTS T-100 Segment CSV into an ordered-pair → value table.

    Sums ``value_field`` (default ``"SEATS"``; use ``"PASSENGERS"`` or
    ``"DEPARTURES_PERFORMED"`` for a different throughput measure) over all
    carriers and months for each ``(origin, destination)`` IATA pair. Returns a
    ``dict[(origin, dest)] -> float``.
    """
    table: dict[tuple[str, str], float] = {}
    with open(csv_path, newline="") as fh:
        reader = csv.DictReader(fh)
        for row in reader:
            origin = (row.get(origin_field) or "").strip().upper()
            dest = (row.get(dest_field) or "").strip().upper()
            if not origin or not dest:
                continue
            try:
                value = float(row.get(value_field) or 0.0)
            except (TypeError, ValueError):
                continue
            table[(origin, dest)] = table.get((origin, dest), 0.0) + value
    return table


def edge_capacity(graph, node_iata, table, *, default=0.0):
    """Per-edge capacity for an OpenFlights graph from a T-100 pair→value table.

    ``node_iata`` is the node→IATA list from
    ``gravel.datasets.openflights.load(..., with_codes=True)``. Returns a float64
    array in CSR edge order where ``capacity[e] = table[(iata[src], iata[dst])]``,
    or ``default`` when the pair is absent (or an endpoint has no IATA code). Feed
    it to a capacity-aware analysis just like any other per-edge capacity array.
    """
    src, tgt, _ = graph.to_coo()
    cap = np.full(len(src), float(default), dtype=np.float64)
    for edge, (u, v) in enumerate(zip(src, tgt, strict=True)):
        origin = node_iata[int(u)]
        dest = node_iata[int(v)]
        if origin and dest:
            cap[edge] = table.get((origin, dest), float(default))
    return cap
