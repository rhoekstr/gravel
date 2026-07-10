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
from dataclasses import dataclass, replace

import numpy as np

from ._gravel import Graph, dijkstra

_INVALID = np.iinfo(np.uint32).max  # dijkstra predecessor sentinel (no predecessor)

# A demand matrix is a mapping origin_node -> {dest_node: trips}. Zero-volume pairs may be omitted.
Demand = dict


@dataclass
class FlowConfig:
    """Assignment parameters.

    ``alpha`` / ``beta`` are the BPR volume-delay coefficients (standard 0.15 / 4). ``theta`` is the
    logit route-choice dispersion: ``None`` selects deterministic UE (Frank-Wolfe); a finite value
    selects stochastic UE (Dial STOCH logit loading + MSA). Larger ``theta`` sharpens choice toward the
    shortest path (``theta`` → ∞ recovers the deterministic limit); smaller spreads flow over alternates.
    ``theta`` is the parameter calibrated against real diversion data in Phase F3 (DD-F5).
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


def _stochastic_ue(src, tgt, t0, cap, n, m, demand, config):
    """Stochastic UE by Dial STOCH logit loading + MSA. Returns (flows, gap, iterations).

    Dial (1971): a link a=(i,j) is *efficient* from origin r when it moves away from r
    (``d_r[i] < d_r[j]``, using one-to-many Dijkstra distances). Its likelihood is
    ``L_a = exp(θ·(d_r[j] − d_r[i] − t_a))`` (the bracket is ≤ 0 by the triangle inequality, so no
    overflow). A forward pass in increasing-distance order accumulates link weights; a backward pass in
    decreasing order distributes each destination's demand back through those weights — loading all
    destinations of one origin in a single sweep, no path enumeration. MSA averages toward the fixed
    point. The reported gap is the MSA relative flow change (SUE has no clean duality gap).
    """
    theta = config.theta
    src32, tgt32 = src.astype(np.uint32), tgt.astype(np.uint32)
    out_links = [[] for _ in range(n)]
    in_links = [[] for _ in range(n)]
    for k in range(m):
        out_links[int(src[k])].append(k)
        in_links[int(tgt[k])].append(k)

    def dial_load(cost):
        g = Graph.from_coo(n, src32, tgt32, cost)
        y = np.zeros(m)
        for o, dests in demand.items():
            o = int(o)
            d = np.asarray(dijkstra(g, o).distances)
            order = np.argsort(d, kind="stable")  # increasing distance; unreachable (inf) sorts last
            eff = (d[src] < d[tgt]) & np.isfinite(d[tgt])
            delta = np.where(eff, d[tgt] - d[src] - cost, -np.inf)  # ≤ 0 on efficient links
            likelihood = np.where(eff, np.exp(theta * delta), 0.0)
            # forward: link weights W in increasing-distance order
            s_in = np.zeros(n)
            s_in[o] = 1.0
            w = np.zeros(m)
            for i in order:
                i = int(i)
                if s_in[i] == 0.0 or not np.isfinite(d[i]):
                    continue
                for a in out_links[i]:
                    if likelihood[a] > 0.0:
                        w[a] = likelihood[a] * s_in[i]
                        s_in[int(tgt[a])] += w[a]
            # backward: distribute demand through the weights, decreasing-distance order
            arriving = np.zeros(n)
            for dst, vol in dests.items():
                arriving[int(dst)] += vol
            for j in order[::-1]:
                j = int(j)
                denom = 0.0
                for a in in_links[j]:
                    denom += w[a]
                if denom <= 0.0:
                    continue
                share = arriving[j] / denom
                for a in in_links[j]:
                    if w[a] > 0.0:
                        f = share * w[a]
                        y[a] += f
                        arriving[int(src[a])] += f
        return y

    x = dial_load(t0)
    rgap = float("inf")
    it = 0
    while it < config.max_iterations:
        it += 1
        cost = bpr(t0, cap, x, config.alpha, config.beta)
        aux = dial_load(cost)
        x_new = x + (aux - x) / (it + 1)  # Method of Successive Averages
        rgap = float(np.sum(np.abs(x_new - x)) / max(float(np.sum(x)), 1.0))
        x = x_new
        if rgap < config.gap_tol:
            break
    return x, rgap, it


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

    # Stochastic UE (finite theta): Dial STOCH logit loading + MSA. Deterministic UE (theta=None)
    # falls through to Frank-Wolfe below.
    if config.theta is not None:
        x, rgap, it = _stochastic_ue(src, tgt, t0, cap, n, m, demand, config)
        cost = bpr(t0, cap, x, config.alpha, config.beta)
        return FlowResult(
            edge_flows=x, edge_times=cost, total_travel_time=float(cost @ x),
            relative_gap=float(rgap), iterations=it,
        )

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

    sub, subcap = _remove_edges(graph, capacity, scenario_edges)
    scenario = assign(sub, subcap, demand, config)

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


# --- Realtime diversion calibration (F3): measure θ, don't assume it --------------------------
# This is the 3.0.0 graduation gate. When a real road closes, traffic diverts to alternates at some
# observed rate; the model predicts a rate that depends on θ. Fit θ so predicted matches observed
# (DD-F5). Validated here on synthetic data (recover a known θ); wired to real PeMS volumes when access
# lands. Data-source stance: open/public-agency (PeMS/NPMRDS/511), never commercial extractive APIs.


def _remove_edges(graph, capacity, remove):
    """Return ``(sub_graph, sub_capacity)`` with the given ``(u, v)`` node-pair edges deleted."""
    src, tgt, t0 = (np.asarray(a) for a in graph.to_coo())
    cap = np.asarray(capacity, dtype=float)
    if cap.size == 0:
        cap = np.full(src.size, np.inf)
    rem = {(int(u), int(v)) for u, v in remove}
    keep = np.fromiter(
        ((int(src[k]), int(tgt[k])) not in rem for k in range(src.size)), dtype=bool, count=src.size
    )
    sub = Graph.from_coo(
        int(graph.node_count), src[keep].astype(np.uint32), tgt[keep].astype(np.uint32), t0[keep]
    )
    return sub, cap[keep]


@dataclass
class ClosureObservation:
    """One measured closure event: which edges closed, which links were monitored, and the flow
    observed on them afterward (from PeMS volumes, or synthetic for validation)."""

    closure_edges: list          # (u, v) directed node pairs removed by the closure
    monitored: list              # (u, v) links where post-closure flow was observed
    observed_flow: np.ndarray    # observed flow on each monitored link (same order as `monitored`)


@dataclass
class CalibrationResult:
    theta: float                 # best-fit logit dispersion
    error: float                 # RMSE (veh) on monitored links at the best-fit θ
    theta_grid: np.ndarray       # θ values evaluated
    error_grid: np.ndarray       # RMSE at each θ (the identifiability curve — report it, don't hide it)


def diversion_flows(graph, capacity, demand, closure_edges, config=None):
    """Model-predicted post-closure equilibrium flows, keyed by ``(u, v)`` — the predicted diversion."""
    config = config or FlowConfig()
    sub, subcap = _remove_edges(graph, capacity, closure_edges)
    res = assign(sub, subcap, demand, config)
    s, t, _ = (np.asarray(a) for a in sub.to_coo())
    return {(int(s[k]), int(t[k])): float(res.edge_flows[k]) for k in range(s.size)}


def calibrate_theta(graph, capacity, demand, observations, theta_bounds=(0.02, 10.0), n_grid=15,
                    config=None):
    """Fit the logit dispersion θ to observed post-closure link flows (DD-F5).

    For each candidate θ (log-spaced over ``theta_bounds``), solve stochastic UE on each observation's
    closed network and compare predicted flow on the monitored links to the observed flow; return the θ
    minimizing RMSE, plus the full error-vs-θ curve so identifiability is visible. This is the harness
    that, fed real PeMS diversion, decides the 3.0.0 graduation gate. Validated on synthetic data by
    recovering a known θ from model-generated observations.
    """
    config = config or FlowConfig()
    thetas = np.geomspace(theta_bounds[0], theta_bounds[1], n_grid)

    def rmse(theta):
        se, cnt = 0.0, 0
        for obs in observations:
            pred_by_pair = diversion_flows(
                graph, capacity, demand, obs.closure_edges, replace(config, theta=float(theta))
            )
            pred = np.array([pred_by_pair.get((int(u), int(v)), 0.0) for u, v in obs.monitored])
            se += float(np.sum((pred - np.asarray(obs.observed_flow, dtype=float)) ** 2))
            cnt += len(obs.observed_flow)
        return (se / cnt) ** 0.5 if cnt else 0.0

    errs = np.array([rmse(t) for t in thetas])
    best = int(np.argmin(errs))
    return CalibrationResult(
        theta=float(thetas[best]), error=float(errs[best]), theta_grid=thetas, error_grid=errs
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
