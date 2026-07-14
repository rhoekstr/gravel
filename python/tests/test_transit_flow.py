"""F4 (scaffold): the flow layer applies to GTFS transit graphs, not just roads.

``gravel.datasets.gtfs.load`` returns ``(Graph, capacity)`` — exactly ``flow.assign``'s inputs — so
rider assignment on a transit network and the cost of a **service disruption** reuse the road machinery
unchanged: a disruption is ``flow_fragility`` with the closed transit edges, giving rider-reroute delay
(ΔTSTT in rider-minutes) plus stranded riders. Real-time GTFS-RT closure integration is deferred (it
hits the same demand-identification wall as roads); this scaffold validates the mechanism synthetically.
"""
import gravel
import numpy as np
from gravel import flow


def _two_line_transit():
    # Two transit lines from stop 0 to stop 3: line A (0-1-3, fast) and line B (0-2-3, slower).
    # Edge weight = in-vehicle minutes; capacity = persons/hour on the boarding legs (a crowding proxy,
    # the same units gtfs.load emits). The transfer legs (1-3, 2-3) are effectively uncapacitated here.
    src = np.array([0, 1, 0, 2], np.uint32)
    tgt = np.array([1, 3, 2, 3], np.uint32)
    t0 = np.array([6.0, 4.0, 8.0, 6.0])           # line A = 10 min, line B = 14 min
    cap = np.array([1500.0, 1e9, 1500.0, 1e9])    # persons/hour boarding capacity
    return gravel.Graph.from_coo(4, src, tgt, t0), cap


def _edge_index(g):
    s, t, _ = (np.asarray(a) for a in g.to_coo())
    return {(int(s[k]), int(t[k])): k for k in range(len(s))}


def test_transit_sue_spreads_riders_over_lines():
    g, cap = _two_line_transit()
    r = flow.assign(g, cap, {0: {3: 2000.0}}, flow.FlowConfig(theta=0.3, max_iterations=200))
    idx = _edge_index(g)
    assert r.edge_flows[idx[(0, 1)]] > 0  # line A carries riders
    assert r.edge_flows[idx[(0, 2)]] > 0  # line B too (logit spreads across both)


def test_transit_disruption_reroutes_riders():
    g, cap = _two_line_transit()
    # Close line A's boarding leg: riders reroute to the slower line B -> more rider-minutes, none stranded.
    r = flow.flow_fragility(g, cap, {0: {3: 2000.0}}, [(0, 1)],
                            flow.FlowConfig(theta=0.3, max_iterations=200))
    assert r.stranded_demand == 0.0   # line B still serves 0->3
    assert r.delta_tstt > 0.0         # forcing everyone onto the slower line costs rider-minutes


def test_transit_disruption_strands_riders():
    g, cap = _two_line_transit()
    # Close both lines' boarding legs: no path 0->3 remains -> every rider stranded.
    r = flow.flow_fragility(g, cap, {0: {3: 2000.0}}, [(0, 1), (0, 2)],
                            flow.FlowConfig(max_iterations=100))
    assert r.stranded_demand == 2000.0
    assert r.scenario_tstt == 0.0
