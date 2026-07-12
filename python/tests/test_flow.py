"""Tests for gravel.flow — deterministic User Equilibrium (Frank-Wolfe + BPR)."""
from pathlib import Path

import gravel
import numpy as np
from gravel import flow

DATA = Path(__file__).resolve().parent / "data" / "siouxfalls"


def test_bpr_formula():
    # t = t0*(1 + alpha*(x/c)^beta): free-flow at x=0, +15% at x=c, uncongestible at c<=0.
    assert float(flow.bpr(10.0, 1000.0, 0.0)) == 10.0
    assert abs(float(flow.bpr(10.0, 1000.0, 1000.0)) - 11.5) < 1e-9
    assert float(flow.bpr(10.0, 0.0, 5000.0)) == 10.0


def test_stochastic_ue_sharpens_to_deterministic():
    # Dial SUE: large theta sharpens toward the deterministic UE; small theta spreads flow.
    graph, capacity, demand = flow.load_tntp(
        DATA / "SiouxFalls_net.tntp", DATA / "SiouxFalls_trips.tntp"
    )
    det = flow.assign(graph, capacity, demand, flow.FlowConfig(gap_tol=1e-4, max_iterations=1500))
    sharp = flow.assign(graph, capacity, demand, flow.FlowConfig(theta=5.0, max_iterations=200))
    spread = flow.assign(graph, capacity, demand, flow.FlowConfig(theta=0.05, max_iterations=200))
    corr_sharp = float(np.corrcoef(sharp.edge_flows, det.edge_flows)[0, 1])
    corr_spread = float(np.corrcoef(spread.edge_flows, det.edge_flows)[0, 1])
    assert corr_sharp > 0.99            # large theta ≈ deterministic UE
    assert corr_spread < corr_sharp     # smaller theta diverges (flow spreads to alternates)


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


def test_flow_fragility_reroute_raises_delay():
    # Diamond again: removing route B's entry (0->2) forces all demand onto route A, which is still
    # connected -- nothing stranded, but the region gets slower (ΔTSTT > 0).
    src = np.array([0, 1, 0, 2], dtype=np.uint32)
    tgt = np.array([1, 3, 2, 3], dtype=np.uint32)
    t0 = np.array([10.0, 1.0, 12.0, 1.0])
    cap = np.array([1000.0, 1e9, 1000.0, 1e9])
    g = gravel.Graph.from_coo(4, src, tgt, t0)

    r = flow.flow_fragility(g, cap, {0: {3: 1500.0}}, [(0, 2)],
                            flow.FlowConfig(gap_tol=1e-7, max_iterations=800))
    assert r.stranded_demand == 0.0    # 0->3 still connected via route A
    assert r.delta_tstt > 0.0          # forcing everyone onto A raises total travel time


def test_flow_fragility_counts_stranded_demand():
    # Chain 0->1->2; removing (1,2) severs the only path to node 2.
    src = np.array([0, 1], dtype=np.uint32)
    tgt = np.array([1, 2], dtype=np.uint32)
    t0 = np.array([5.0, 5.0])
    cap = np.array([1000.0, 1000.0])
    g = gravel.Graph.from_coo(3, src, tgt, t0)

    r = flow.flow_fragility(g, cap, {0: {2: 100.0}}, [(1, 2)], flow.FlowConfig())
    assert r.stranded_demand == 100.0
    assert r.scenario_tstt == 0.0      # the only demand is now unservable


def test_calibrate_recovers_known_theta():
    # Primary route A plus two alternates B, C of different cost. Close A; the diversion split between
    # B and C depends on theta. Generate "observed" flows at a known theta, then recover it -- the F3
    # calibration machinery validated on synthetic data (real PeMS volumes plug in the same way).
    src = np.array([0, 1, 0, 2, 0, 4], dtype=np.uint32)  # A:(0,1)(1,3)  B:(0,2)(2,3)  C:(0,4)(4,3)
    tgt = np.array([1, 3, 2, 3, 4, 3], dtype=np.uint32)
    t0 = np.array([8.0, 4.0, 10.0, 3.0, 11.0, 3.0])
    cap = np.full(6, 1e9)  # uncongested: route choice dominates, so theta is cleanly identified
    g = gravel.Graph.from_coo(5, src, tgt, t0)
    demand = {0: {3: 6000.0}}
    theta_true = 0.5

    pred = flow.diversion_flows(g, cap, demand, [(0, 1)],
                                flow.FlowConfig(max_iterations=200, theta=theta_true))
    monitored = [(0, 2), (0, 4)]
    observed = np.array([pred[(0, 2)], pred[(0, 4)]])
    assert observed.min() > 0  # both alternates carry flow (theta is identifiable)

    obs = flow.ClosureObservation(closure_edges=[(0, 1)], monitored=monitored,
                                  observed=observed, observable="flow")
    result = flow.calibrate_theta(g, cap, demand, [obs], theta_bounds=(0.05, 5.0), n_grid=15,
                                  config=flow.FlowConfig(max_iterations=200))
    assert result.observable == "flow"
    assert abs(np.log(result.theta / theta_true)) < np.log(1.7)  # recovered within grid resolution
    assert result.error < 1.0  # near-perfect fit at the recovered theta (self-consistent data)


def test_calibrate_recovers_theta_from_speed():
    # The PRIMARY path: fit theta to closure-induced *slowdown*, no volume used. Finite capacities so the
    # overflow onto alternates B and C actually congests them; the pair of slowdown ratios encodes theta.
    src = np.array([0, 1, 0, 2, 0, 4], dtype=np.uint32)  # A:(0,1)(1,3)  B:(0,2)(2,3)  C:(0,4)(4,3)
    tgt = np.array([1, 3, 2, 3, 4, 3], dtype=np.uint32)
    t0 = np.array([8.0, 4.0, 10.0, 3.0, 11.0, 3.0])
    cap = np.full(6, 3000.0)  # finite -> overflow slows the alternates
    g = gravel.Graph.from_coo(5, src, tgt, t0)
    demand = {0: {3: 5000.0}}
    theta_true = 0.4
    mon = [(0, 2), (0, 4)]

    pred = flow._diversion_predict(g, cap, demand, [(0, 1)],
                                   flow.FlowConfig(max_iterations=400, theta=theta_true))
    observed = np.array([pred[m]["congestion"] for m in mon])  # slowdown ratios t/t0 on B, C
    assert observed.max() > 1.02  # the alternates genuinely slow (there is a signal to fit)

    obs = flow.ClosureObservation(closure_edges=[(0, 1)], monitored=mon,
                                  observed=observed, observable="congestion")
    result = flow.calibrate_theta(g, cap, demand, [obs], theta_bounds=(0.05, 3.0), n_grid=15,
                                  config=flow.FlowConfig(max_iterations=400))
    assert result.observable == "congestion"
    assert abs(np.log(result.theta / theta_true)) < np.log(1.7)  # recovered from speed within grid res
