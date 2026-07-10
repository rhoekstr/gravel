"""gravel.flow — demand-driven traffic assignment (User Equilibrium).

A consumer layer built ON TOP of Gravel, outside the core's DD-6 "topology, not flow" boundary
(see ``docs/FLOW_LAYER.md``). It adds an origin-destination demand matrix and BPR congestion delay
and solves for the equilibrium flow pattern travelers settle into — congestion as *slow-down*, not
blockage. Gravel supplies the fast routing engine; this module supplies demand + equilibrium.

Phase F1 ships **deterministic User Equilibrium** (Wardrop) via Frank-Wolfe: every traveler ends up
on a shortest path under congested costs. It reproduces the standard Sioux Falls benchmark. The
stochastic (logit) generalization — some travelers taking a longer path to dodge a jam, with the
dispersion ``theta`` calibrated from real diversion data — is a later phase (``docs/FLOW_LAYER.md``,
DD-F5); ``theta = None`` here means the deterministic limit.

The solver only needs numpy (a core dependency) and the Gravel one-to-many ``dijkstra``. Heavier
calibration / realtime-data dependencies are gated behind the ``[sue]`` extra
(``pip install gravel-fragility[sue]``) and lazy-imported where used.

Example::

    import gravel
    from gravel import flow

    graph, capacity, demand = flow.load_tntp("SiouxFalls_net.tntp", "SiouxFalls_trips.tntp")
    result = flow.assign(graph, capacity, demand, flow.FlowConfig())
    result.edge_flows          # equilibrium flow per edge (CSR order, aligned to Graph.to_coo())
    result.total_travel_time   # TSTT = sum(flow * congested_time)
"""
from __future__ import annotations

import re
from dataclasses import dataclass

import numpy as np

from ._gravel import Graph, dijkstra

_INVALID = np.iinfo(np.uint32).max  # dijkstra predecessor sentinel (no predecessor)

# A demand matrix is a mapping origin_node -> {dest_node: trips}. Zero-volume pairs may be omitted.
Demand = dict


@dataclass
class FlowConfig:
    """Assignment parameters.

    ``alpha`` / ``beta`` are the BPR volume-delay coefficients (standard 0.15 / 4). ``theta`` is the
    logit route-choice dispersion for stochastic UE; ``None`` selects deterministic UE (Frank-Wolfe),
    the only mode implemented in phase F1.
    """

    alpha: float = 0.15
    beta: float = 4.0
    max_iterations: int = 200
    gap_tol: float = 1e-4          # stop when the relative gap falls below this
    theta: float | None = None     # None => deterministic UE; finite => stochastic UE (future phase)


@dataclass
class FlowResult:
    """Equilibrium flow pattern. Arrays are per-edge in ``Graph.to_coo()`` (CSR) order."""

    edge_flows: np.ndarray       # x_a: equilibrium volume on each edge
    edge_times: np.ndarray       # t_a(x_a): congested travel time on each edge
    total_travel_time: float     # TSTT = sum(x_a * t_a)
    relative_gap: float          # convergence measure at the final iteration
    iterations: int


def bpr(free_flow_time, capacity, flow, alpha=0.15, beta=4.0):
    """BPR volume-delay: ``t = t0 * (1 + alpha * (x / c) ** beta)``, vectorized.

    Edges with non-positive capacity are treated as uncongestible (cost stays at free-flow).
    """
    t0 = np.asarray(free_flow_time, dtype=float)
    cap = np.asarray(capacity, dtype=float)
    x = np.asarray(flow, dtype=float)
    ratio = np.zeros_like(t0)
    np.divide(x, cap, out=ratio, where=cap > 0)
    return t0 * (1.0 + alpha * ratio**beta)


def assign(graph, capacity, demand, config=None):
    """Solve for the User-Equilibrium flow pattern.

    Parameters
    ----------
    graph : gravel.Graph
        Network whose edge weights are free-flow travel times (``t0``).
    capacity : array-like
        Per-edge capacity aligned with ``graph.to_coo()`` (CSR order). An empty array means every
        edge is uncongestible (the result is then a single all-or-nothing loading).
    demand : dict
        ``{origin_node: {dest_node: trips}}``.
    config : FlowConfig, optional

    Returns
    -------
    FlowResult

    Notes
    -----
    Frank-Wolfe: each iteration rebuilds the graph with BPR-updated costs, loads every O-D pair
    all-or-nothing onto current shortest paths (one-to-many ``dijkstra`` per origin), then takes a
    line-searched convex step toward that auxiliary flow. Convergence is the standard relative gap.
    The CH is deliberately not used — congested weights change every iteration and one-to-many
    loading visits the whole graph anyway, where plain Dijkstra is already optimal (DD-F2).
    """
    config = config or FlowConfig()
    if config.theta is not None:
        raise NotImplementedError(
            "stochastic UE (finite theta) is a later phase; use theta=None for deterministic UE"
        )

    src, tgt, t0 = (np.asarray(a) for a in graph.to_coo())
    m = int(src.size)
    n = int(graph.node_count)
    cap = np.asarray(capacity, dtype=float)
    if cap.size == 0:
        cap = np.full(m, np.inf)
    if cap.size != m:
        raise ValueError(f"capacity length {cap.size} != edge count {m}")

    src32 = src.astype(np.uint32)
    tgt32 = tgt.astype(np.uint32)
    lidx = {(int(src[k]), int(tgt[k])): k for k in range(m)}

    def all_or_nothing(cost):
        g = Graph.from_coo(n, src32, tgt32, cost)
        y = np.zeros(m)
        for o, dests in demand.items():
            o = int(o)
            pred = np.asarray(dijkstra(g, o).predecessors)
            for d, vol in dests.items():
                v = int(d)
                while v != o:
                    u = int(pred[v])
                    if u == _INVALID:
                        break  # unreachable O-D pair: nothing to load
                    y[lidx[(u, v)]] += vol
                    v = u
        return y

    x = all_or_nothing(t0)  # initialize at free-flow all-or-nothing
    rgap = float("inf")
    it = 0
    while it < config.max_iterations:
        it += 1
        cost = bpr(t0, cap, x, config.alpha, config.beta)
        y = all_or_nothing(cost)
        tstt = float(cost @ x)
        rgap = (tstt - float(cost @ y)) / tstt if tstt > 0 else 0.0
        if rgap < config.gap_tol:
            break
        direction = y - x
        # Exact line search: minimize the Beckmann objective along x + lam*(y-x), lam in [0, 1].
        # dZ/dlam = bpr(x + lam*d) . d is monotone increasing in lam; bisect for its root.
        lo, hi = 0.0, 1.0
        for _ in range(40):
            mid = 0.5 * (lo + hi)
            if float(bpr(t0, cap, x + mid * direction, config.alpha, config.beta) @ direction) > 0:
                hi = mid
            else:
                lo = mid
        x = x + 0.5 * (lo + hi) * direction

    cost = bpr(t0, cap, x, config.alpha, config.beta)
    return FlowResult(
        edge_flows=x,
        edge_times=cost,
        total_travel_time=float(cost @ x),
        relative_gap=float(rgap),
        iterations=it,
    )


# --- Scenario fragility (F2): what a failure costs, region-wide -------------------------------


@dataclass
class FlowFragilityResult:
    """Impact of failing a set of edges, after demand re-equilibrates around the damage."""

    delta_tstt: float          # scenario TSTT - intact TSTT, over trips still servable
    delta_tstt_frac: float     # delta_tstt / intact_tstt
    intact_tstt: float
    scenario_tstt: float
    stranded_demand: float     # trips whose O-D pair is disconnected by the scenario
    intact: FlowResult
    scenario: FlowResult


def flow_fragility(graph, capacity, demand, scenario_edges, config=None):
    """Region-wide delay impact (ΔTSTT) of failing ``scenario_edges``, demand re-equilibrating.

    Parameters
    ----------
    scenario_edges : iterable of (u, v)
        Directed node pairs to remove from the network.

    Returns
    -------
    FlowFragilityResult

    Notes
    -----
    Read ``delta_tstt`` **together with** ``stranded_demand``: a closure that severs O-D pairs shows
    up as stranded demand, not (or not only) as added travel time — and because unservable trips drop
    out of TSTT, a severing closure can even *lower* scenario TSTT. ΔTSTT alone is the reroute cost
    for trips that remain connected; stranded demand is the disconnection severity. (Intact and
    scenario flow arrays are on different edge sets, so only the scalar TSTTs are directly comparable.)
    """
    config = config or FlowConfig()
    intact = assign(graph, capacity, demand, config)

    src, tgt, t0 = (np.asarray(a) for a in graph.to_coo())
    cap = np.asarray(capacity, dtype=float)
    if cap.size == 0:
        cap = np.full(src.size, np.inf)
    remove = {(int(u), int(v)) for u, v in scenario_edges}
    keep = np.fromiter(
        ((int(src[k]), int(tgt[k])) not in remove for k in range(src.size)),
        dtype=bool,
        count=src.size,
    )
    n = int(graph.node_count)
    sub = Graph.from_coo(n, src[keep].astype(np.uint32), tgt[keep].astype(np.uint32), t0[keep])
    scenario = assign(sub, cap[keep], demand, config)

    # Demand whose O-D pair is disconnected once the scenario edges are gone.
    stranded = 0.0
    for o, dests in demand.items():
        dist = np.asarray(dijkstra(sub, int(o)).distances)
        for d, vol in dests.items():
            if not np.isfinite(dist[int(d)]):
                stranded += vol

    dt = scenario.total_travel_time - intact.total_travel_time
    return FlowFragilityResult(
        delta_tstt=dt,
        delta_tstt_frac=dt / intact.total_travel_time if intact.total_travel_time > 0 else 0.0,
        intact_tstt=intact.total_travel_time,
        scenario_tstt=scenario.total_travel_time,
        stranded_demand=stranded,
        intact=intact,
        scenario=scenario,
    )


# --- Standard benchmark I/O (TNTP format) -------------------------------------------------------
# The TNTP format is the de-facto standard for traffic-assignment test networks
# (github.com/bstabler/TransportationNetworks). Node ids in files are 1-based; we store 0-based.


def _tntp_body(path):
    return open(path).read().split("<END OF METADATA>", 1)[-1]


def load_tntp(net_path, trips_path):
    """Load a TNTP network + trip table into ``(graph, capacity, demand)``.

    The graph's edge weights are free-flow travel times; ``capacity`` is CSR-aligned to
    ``graph.to_coo()``; ``demand`` is ``{origin: {dest: trips}}``. BPR ``b``/``power`` columns are
    not returned — pass matching ``alpha``/``beta`` in :class:`FlowConfig` (Sioux Falls uses the
    standard 0.15 / 4).
    """
    init, term, cap, fft = [], [], [], []
    for line in _tntp_body(net_path).splitlines():
        s = line.strip()
        if not s or s.startswith("~"):
            continue
        p = s.strip(";").split()
        if len(p) < 5:
            continue
        init.append(int(p[0]) - 1)
        term.append(int(p[1]) - 1)
        cap.append(float(p[2]))
        fft.append(float(p[4]))  # columns: init term capacity length free_flow_time b power ...
    init = np.asarray(init, dtype=np.uint32)
    term = np.asarray(term, dtype=np.uint32)
    n = int(max(init.max(), term.max())) + 1
    graph = Graph.from_coo(n, init, term, np.asarray(fft, dtype=float))

    demand = {}
    for block in re.split(r"Origin", _tntp_body(trips_path))[1:]:
        toks = block.split()
        o = int(toks[0]) - 1
        dests = {}
        for mo in re.finditer(r"(\d+)\s*:\s*([\d.eE+-]+)", " ".join(toks[1:])):
            v = float(mo.group(2))
            if v > 0:
                dests[int(mo.group(1)) - 1] = v
        if dests:
            demand[o] = dests

    # capacity aligned to graph.to_coo() order (from_coo keeps the already-source-sorted input order,
    # but map by node-pair identity to be safe against any reordering).
    src, tgt, _ = (np.asarray(a) for a in graph.to_coo())
    by_pair = {(int(init[k]), int(term[k])): cap[k] for k in range(init.size)}
    capacity = np.asarray([by_pair[(int(src[k]), int(tgt[k]))] for k in range(src.size)], dtype=float)
    return graph, capacity, demand
