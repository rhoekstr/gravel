"""Tests for gravel.flow — deterministic User Equilibrium (Frank-Wolfe + BPR)."""
from pathlib import Path

import gravel
import numpy as np
import pytest
from gravel import flow

DATA = Path(__file__).resolve().parent / "data" / "siouxfalls"


def test_bpr_formula():
    # t = t0*(1 + alpha*(x/c)^beta): free-flow at x=0, +15% at x=c, uncongestible at c<=0.
    assert float(flow.bpr(10.0, 1000.0, 0.0)) == 10.0
    assert abs(float(flow.bpr(10.0, 1000.0, 1000.0)) - 11.5) < 1e-9
    assert float(flow.bpr(10.0, 0.0, 5000.0)) == 10.0


def test_stochastic_ue_not_yet_implemented():
    g = gravel.make_grid_graph(3, 3)
    with pytest.raises(NotImplementedError):
        flow.assign(g, [], {0: {8: 100.0}}, flow.FlowConfig(theta=1.0))


def test_ue_equalizes_used_routes():
    # Diamond: route A (0-1-3) vs route B (0-2-3). At UE both used routes carry equal travel time
    # (Wardrop's first principle), and total demand is conserved across the two.
    src = np.array([0, 1, 0, 2], dtype=np.uint32)
    tgt = np.array([1, 3, 2, 3], dtype=np.uint32)
    t0 = np.array([10.0, 1.0, 12.0, 1.0])
    cap = np.array([1000.0, 1e9, 1000.0, 1e9])
    g = gravel.Graph.from_coo(4, src, tgt, t0)

    r = flow.assign(g, cap, {0: {3: 1500.0}}, flow.FlowConfig(gap_tol=1e-7, max_iterations=800))

    s, t, _ = (np.asarray(a) for a in g.to_coo())
    idx = {(int(s[k]), int(t[k])): k for k in range(s.size)}
    cost_a = r.edge_times[idx[(0, 1)]] + r.edge_times[idx[(1, 3)]]
    cost_b = r.edge_times[idx[(0, 2)]] + r.edge_times[idx[(2, 3)]]
    flow_a, flow_b = r.edge_flows[idx[(0, 1)]], r.edge_flows[idx[(0, 2)]]
    assert flow_a > 0 and flow_b > 0                 # both routes used
    assert abs(flow_a + flow_b - 1500.0) < 1.0       # demand conserved
    assert abs(cost_a - cost_b) < 0.05               # Wardrop: equal cost on used routes


def test_sioux_falls_reproduces_known_ue():
    graph, capacity, demand = flow.load_tntp(
        DATA / "SiouxFalls_net.tntp", DATA / "SiouxFalls_trips.tntp"
    )
    r = flow.assign(graph, capacity, demand, flow.FlowConfig(gap_tol=1e-4, max_iterations=1500))

    known = {}
    for line in open(DATA / "SiouxFalls_flow.tntp"):
        p = line.split()
        if len(p) >= 3:
            try:
                known[(int(p[0]) - 1, int(p[1]) - 1)] = float(p[2])
            except ValueError:
                pass
    src, tgt, _ = (np.asarray(a) for a in graph.to_coo())
    kv = np.array([known[(int(src[k]), int(tgt[k]))] for k in range(src.size)])

    mape = float(np.mean(np.abs(r.edge_flows - kv) / np.where(kv > 1, kv, 1)) * 100)
    corr = float(np.corrcoef(r.edge_flows, kv)[0, 1])
    assert r.relative_gap < 1e-3      # converged
    assert corr > 0.9999              # matches the published equilibrium
    assert mape < 1.0                 # within 1% mean absolute error per link
