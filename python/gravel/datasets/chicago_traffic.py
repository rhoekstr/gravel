"""Chicago Traffic Tracker — live arterial speeds as a fragility substrate (``gravel.datasets.chicago_traffic``).

The City of Chicago publishes near-real-time **estimated congestion by segment** for ~1,250 arterial
segments, derived from GPS traces of the CTA bus fleet, plus a multi-year historical archive. This
adapter turns that open feed into a routable :class:`gravel.Graph` and provides the two things the flow
layer's real-data calibration needs: normal-condition congestion profiles and **road-closure detection**.

Two Socrata datasets (``data.cityofchicago.org``, no key required for light use):

* **Segments** (``n4j6-wkkf``) — the ~1,250 segment definitions: endpoints (lat/lon), street, direction,
  length, and the live ``_traffic`` speed (mph).
* **Historical congestion** (``4g9f-3jbs``) — per-segment speed every ~10 min, 2018→present.

**Read this before trusting the numbers.** The speed is of vehicles *moving through* a segment (bus
probes), not a volume. The no-data sentinel is **``-1``** (no bus traversed it that interval) — distinct
from ``0`` (present but stopped). A genuine road **closure therefore reads as a sustained run of ``-1``**
(the segment goes dark), *not* as speed 0. Coverage is bus-served arterials only — no expressways, and
segments with no bus service are chronically ``-1`` (a coverage gap, not news). :func:`detect_closures`
uses this: a segment going dark **while a geographic neighbor slows** is the closure signature that
separates a real closure from a coverage hole.

Free-flow speed is not published; :func:`free_flow_speeds` derives it per segment as the mean off-peak
(overnight) observed speed, so ``congestion ratio = free_flow / observed`` (i.e. ``t/t0``) is comparable
across segments. Everything here is open data under the City's terms; attribution is polite
(:data:`ATTRIBUTION`).
"""

from __future__ import annotations

import json as _json
from dataclasses import dataclass, field
from typing import TYPE_CHECKING
from urllib.error import URLError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from ._provenance import Provenance

if TYPE_CHECKING:  # pragma: no cover - typing only
    pass

DOMAIN = "https://data.cityofchicago.org/resource"
SEGMENTS_ID = "n4j6-wkkf"
HISTORY_ID = "4g9f-3jbs"
NO_DATA = -1.0  # the Traffic Tracker sentinel for "no probe this interval" (distinct from 0 = stopped)
ATTRIBUTION = "Congestion data: City of Chicago (Chicago Data Portal, Traffic Tracker)."

# Environment override so tests / mirrors can redirect the endpoint (matches GRAVEL_NFHL_ENDPOINT).
import os as _os  # noqa: E402

_ENDPOINT = _os.environ.get("GRAVEL_CHICAGO_ENDPOINT", DOMAIN)


@dataclass(frozen=True)
class Segment:
    """One Traffic Tracker segment (a directed arterial block)."""

    segment_id: str
    street: str
    direction: str
    from_street: str
    to_street: str
    length_mi: float
    start: tuple[float, float]  # (lat, lon)
    end: tuple[float, float]


@dataclass
class ClosureEvent:
    """A detected closure: a segment that went dark while a neighbor slowed."""

    segment_id: str
    street: str
    direction: str
    start_day: str            # ISO date the dark run began
    end_day: str              # ISO date it ended (inclusive)
    days: int
    normal_records_per_day: float
    neighbors: list[dict] = field(default_factory=list)  # [{segment_id, street, baseline_mph, during_mph}]


def _get(dataset: str, params: dict, timeout: float = 180.0, retries: int = 2) -> list:
    """One Socrata SoQL GET, returning the parsed JSON rows.

    The historical dataset is large and its grouped aggregations can be slow; a ``TimeoutError`` is not
    a ``URLError`` subclass, so both are caught and retried before giving up.
    """
    url = f"{_ENDPOINT}/{dataset}.json?" + urlencode(params)
    last = None
    for _attempt in range(retries + 1):
        try:
            with urlopen(Request(url, headers={"Accept": "application/json"}), timeout=timeout) as resp:
                payload = _json.loads(resp.read().decode("utf-8"))
        except (URLError, TimeoutError) as exc:  # pragma: no cover - network dependent
            last = exc
            continue
        if isinstance(payload, dict) and "error" in payload:  # Socrata returns HTTP 200 with an error body
            raise RuntimeError(f"Chicago Traffic Tracker query error ({dataset}): {payload}")
        return payload
    raise RuntimeError(f"Chicago Traffic Tracker request failed ({dataset}) after "
                       f"{retries + 1} attempts: {last}")


def load_segments(bbox: tuple[float, float, float, float] | None = None):
    """Load the arterial segment network as a routable graph.

    Parameters
    ----------
    bbox : (min_lat, min_lon, max_lat, max_lon), optional
        Restrict to segments whose midpoint falls in this box (a corridor). ``None`` loads the city.

    Returns
    -------
    (graph, segments, provenance) : (:class:`gravel.Graph`, list[:class:`Segment`], :class:`Provenance`)
        ``graph`` edges are in the same order as ``segments``; edge weight is free-flow travel time in
        seconds (length / 25 mph). Node ids are assigned by coordinate. Congestion is applied separately
        via :func:`free_flow_speeds` / the flow layer, keeping the graph itself topology-only.
    """
    from .._gravel import Graph  # local import: core is always present, keeps import cheap

    rows = _get(SEGMENTS_ID, {"$limit": 2000})
    node_of: dict[tuple[float, float], int] = {}

    def nid(lat: float, lon: float) -> int:
        key = (round(lat, 5), round(lon, 5))
        return node_of.setdefault(key, len(node_of))

    src, tgt, wt, segs = [], [], [], []
    for r in rows:
        try:
            s_lat, s_lon = float(r["_lif_lat"]), float(r["start_lon"])
            e_lat, e_lon = float(r["_lit_lat"]), float(r["_lit_lon"])
            length = float(r["_length"])
        except (KeyError, TypeError, ValueError):
            continue
        mid = ((s_lat + e_lat) / 2, (s_lon + e_lon) / 2)
        if bbox and not (bbox[0] <= mid[0] <= bbox[2] and bbox[1] <= mid[1] <= bbox[3]):
            continue
        if length <= 0:
            continue
        u, v = nid(s_lat, s_lon), nid(e_lat, e_lon)
        if u == v:
            continue
        src.append(u)
        tgt.append(v)
        wt.append(length / 25.0 * 3600.0)  # free-flow seconds at 25 mph
        segs.append(Segment(
            segment_id=str(r.get("segmentid", "")), street=r.get("street", ""),
            direction=r.get("_direction", ""), from_street=r.get("_fromst", ""),
            to_street=r.get("_tost", ""), length_mi=length, start=(s_lat, s_lon), end=(e_lat, e_lon)))

    import numpy as np
    n = len(node_of)
    # Graph.from_coo reorders edges (counting sort), so re-align `segs` to the graph's CSR order.
    g = Graph.from_coo(n, np.array(src, np.uint32), np.array(tgt, np.uint32), np.array(wt, float))
    gs, gt, _ = (np.asarray(a) for a in g.to_coo())
    by_uv = {}
    for i, (u, v) in enumerate(zip(src, tgt, strict=True)):
        by_uv.setdefault((u, v), []).append(i)
    csr_segs, used = [], {}
    for k in range(len(gs)):
        key = (int(gs[k]), int(gt[k]))
        j = used.get(key, 0)
        idxs = by_uv.get(key, [])
        csr_segs.append(segs[idxs[j]] if j < len(idxs) else None)
        used[key] = j + 1
    prov = Provenance.stamp("chicago_traffic_tracker", f"{_ENDPOINT}/{SEGMENTS_ID}",
                            f"live/{len(csr_segs)}segments")
    return g, csr_segs, prov


def free_flow_speeds(segment_ids: list[str],
                     hours: tuple[int, ...] = (1, 2, 3, 4, 5)) -> dict[str, float]:
    """Per-segment free-flow speed (mph): the mean off-peak (overnight) observed speed.

    Overnight hours are the least congested, so their mean is a robust free-flow proxy that avoids the
    sparse-night noise a raw max would pick up. Segments with no valid overnight data are omitted. (The
    historical ``speed`` column is text on Socrata, hence the ``::number`` casts throughout.)
    """
    ids = "(" + ",".join(f"'{s}'" for s in segment_ids) + ")"
    hrs = "(" + ",".join(str(h) for h in hours) + ")"
    rows = _get(HISTORY_ID, {
        "$select": "segment_id, avg(speed::number) as ff",
        "$where": f"speed::number > 0 and hour in {hrs} and segment_id in {ids}",
        "$group": "segment_id", "$limit": 5000})
    out = {}
    for r in rows:
        try:
            out[r["segment_id"]] = float(r["ff"])
        except (KeyError, TypeError, ValueError):
            continue
    return out


def congestion_profile(segment_ids: list[str], start: str, end: str,
                       weekdays_only: bool = True) -> dict[str, dict[int, float]]:
    """Normal speed by hour over ``[start, end)`` for each segment: ``{segment_id: {hour: mean_mph}}``.

    The demand-baseline target: what normal congestion looks like on the corridor, by time of day. The
    date window is required — the historical dataset spans years, and an unbounded group-by times out;
    the calibration always has a specific baseline period anyway (``start``/``end`` are ISO dates).
    """
    ids = "(" + ",".join(f"'{s}'" for s in segment_ids) + ")"
    where = (f"speed::number > 0 and segment_id in {ids} "
             f"and time >= '{start}T00:00:00' and time < '{end}T00:00:00'")
    if weekdays_only:
        where += " and day_of_week in (2,3,4,5,6)"
    rows = _get(HISTORY_ID, {
        "$select": "segment_id, hour, avg(speed::number) as v",
        "$where": where, "$group": "segment_id, hour", "$limit": 50000})
    out: dict[str, dict[int, float]] = {}
    for r in rows:
        try:
            out.setdefault(r["segment_id"], {})[int(r["hour"])] = float(r["v"])
        except (KeyError, TypeError, ValueError):
            continue
    return out


def detect_closures(start: str, end: str, min_days: int = 3, min_normal: float = 15.0,
                    neighbor_slowdown: float = 0.10):
    """Find road closures as natural experiments, from the speed data alone.

    A closure reads as a segment going **dark** (a sustained run of the ``-1`` no-data sentinel against
    its own normal coverage) **while a geographic neighbor slows** below its matched baseline. The
    go-dark alone is ambiguous (a segment can read ``-1`` simply because no bus ran it); the corroborating
    neighbor slowdown is what separates a real closure from a coverage gap.

    Parameters
    ----------
    start, end : ISO dates bounding the search window.
    min_days : minimum length of a dark run to consider (filters incident-scale blips).
    min_normal : minimum normal records/day for a segment to be "well-covered" (else its ``-1`` is noise).
    neighbor_slowdown : fractional speed drop on a neighbor to count as corroboration.

    Returns
    -------
    list[:class:`ClosureEvent`], newest closures first, only those with >=1 corroborating neighbor.

    Notes
    -----
    Network-heavy: aggregates the historical dataset day-by-month over the window. See
    :func:`load_segments` for the segment geometry used to find neighbors.
    """

    import numpy as np

    g, segs, _ = load_segments()
    by_id = {s.segment_id: s for s in segs if s}
    # adjacency by shared endpoint (a neighbor shares a node with the segment)
    node_segs: dict[tuple[float, float], list[str]] = {}
    for s in by_id.values():
        for pt in (s.start, s.end):
            node_segs.setdefault((round(pt[0], 4), round(pt[1], 4)), []).append(s.segment_id)

    def neighbors_of(sid: str) -> list[str]:
        s = by_id.get(sid)
        if not s:
            return []
        out: set[str] = set()
        for pt in (s.start, s.end):
            out.update(node_segs.get((round(pt[0], 4), round(pt[1], 4)), []))
        out.discard(sid)
        return list(out)

    # per (segment, day) valid-record counts + mean speed, month by month (bounded queries).
    def months(a: str, b: str):
        y, m = int(a[:4]), int(a[5:7])
        while f"{y:04d}-{m:02d}" <= b[:7]:
            y2, m2 = (y + 1, 1) if m == 12 else (y, m + 1)
            yield f"{y:04d}-{m:02d}-01", f"{y2:04d}-{m2:02d}-01"
            y, m = y2, m2

    series: dict[str, dict[str, tuple[int, float]]] = {}
    for a, b in months(start, end):
        rows = _get(HISTORY_ID, {
            "$select": "segment_id, date_trunc_ymd(time) as d, count(1) as nv, avg(speed::number) as v",
            "$where": f"speed::number > 0 and time >= '{a}T00:00:00' and time < '{b}T00:00:00'",
            "$group": "segment_id, d", "$limit": 50000})
        for r in rows:
            try:
                series.setdefault(r["segment_id"], {})[r["d"][:10]] = (int(r["nv"]), float(r["v"]))
            except (KeyError, TypeError, ValueError):
                continue

    all_days = sorted({d for perseg in series.values() for d in perseg})
    events: list[ClosureEvent] = []
    for sid, perseg in series.items():
        base = np.median([nv for nv, _ in perseg.values()]) if perseg else 0
        if base < min_normal:
            continue
        # find maximal runs of days with no valid data (dark) within the window
        dark = [d for d in all_days if d not in perseg]
        run: list[str] = []
        runs = []
        for d in all_days:
            if d in dark:
                run.append(d)
            else:
                if len(run) >= min_days:
                    runs.append((run[0], run[-1], len(run)))
                run = []
        if len(run) >= min_days:
            runs.append((run[0], run[-1], len(run)))
        for d0, d1, ln in runs:
            win = {d for d in all_days if d0 <= d <= d1}
            corrob = []
            for nb in neighbors_of(sid):
                dur = [v for d, (_, v) in series.get(nb, {}).items() if d in win]
                bl = [v for d, (_, v) in series.get(nb, {}).items() if d not in win]
                if not dur or not bl:
                    continue
                bmean = float(np.mean(bl))
                if bmean > 0 and float(np.mean(dur)) < (1 - neighbor_slowdown) * bmean:
                    nseg = by_id.get(nb)
                    corrob.append({"segment_id": nb, "street": nseg.street if nseg else "",
                                   "baseline_mph": bmean, "during_mph": float(np.mean(dur))})
            if corrob:
                s = by_id[sid]
                events.append(ClosureEvent(
                    segment_id=sid, street=s.street, direction=s.direction,
                    start_day=d0, end_day=d1, days=ln, normal_records_per_day=float(base),
                    neighbors=sorted(corrob, key=lambda c: c["during_mph"] / c["baseline_mph"])))
    events.sort(key=lambda e: e.start_day, reverse=True)
    return events
