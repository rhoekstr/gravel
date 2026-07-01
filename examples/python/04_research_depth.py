#!/usr/bin/env python3
"""Phase 2A research depth: capacity weighting, stochastic fragility, cascades.

Runs on a synthetic graph so it works without OSM data. The commented OSM/floodplain
sections show how the same tools apply to real hazard-scenario modeling.

    python examples/python/04_research_depth.py
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))

import gravel  # noqa: E402


def main():
    g = gravel.make_grid_graph(30, 30)
    ch = gravel.build_ch(g)
    idx = gravel.ShortcutIndex(ch)
    print(f"graph: {g.node_count} nodes, {g.edge_count} edges\n")

    # ------------------------------------------------------------------
    # 1. Capacity-aware edge importance (normalize + weight)
    # ------------------------------------------------------------------
    # In practice capacity comes from OSM metadata:
    #   graph, md = gravel.load_osm_graph_with_metadata("county.osm.pbf")
    #   capacity = gravel.estimate_capacity(md, gravel.CapacityConfig.hcm())
    # Here we synthesize a capacity array.
    capacity = [500.0 + (e % 5) * 400.0 for e in range(g.edge_count)]

    bc = gravel.BetweennessConfig()
    bc.sample_sources = 100
    bc.edge_capacity = capacity          # -> populates criticality
    bc.deterministic = True              # reproducible covariate
    b = gravel.edge_betweenness(g, bc)

    importance = gravel.capacity_weighted_importance(b, capacity)  # betweenness x capacity
    worst_saturation = max(range(g.edge_count), key=lambda e: b.criticality[e])
    worst_consequence = max(range(g.edge_count), key=lambda e: importance[e])
    print("1. capacity-aware betweenness")
    print(f"   most-saturated edge (load/capacity):   {worst_saturation}")
    print(f"   highest-consequence edge (load*cap):   {worst_consequence}\n")

    # ------------------------------------------------------------------
    # 2. Stochastic fragility — distribution under random failures
    # ------------------------------------------------------------------
    # Floodplain scenario: edges inside a FEMA flood polygon get elevated closure
    # probability; everything else stays low. Here we use a uniform baseline.
    #   flood = gravel.edges_in_polygon(graph, flood_polygon)
    #   probs = [0.6 if (u, v) in flood_set else 0.02 for ...]
    probs = [0.05] * g.edge_count

    sc = gravel.StochasticFragilityConfig()
    sc.monte_carlo_runs = 100
    sc.od_sample_count = 40
    sc.seed = 1
    res = gravel.stochastic_fragility(g, ch, idx, probs, sc)
    print("2. stochastic fragility (p=0.05 per edge)")
    print(f"   mean inflation {res.mean:.3f}  p90 {res.p90:.3f}  p99 {res.p99:.3f}")
    print(f"   disconnected fraction {res.mean_disconnected_fraction:.3f}")
    print(f"   P(mean inflation > {sc.exceedance_thresholds}) = "
          f"{[round(x, 3) for x in res.exceedance]}\n")

    # ------------------------------------------------------------------
    # 3. Cascading failure — robustness as a curve over tolerance alpha
    # ------------------------------------------------------------------
    cc = gravel.CascadeFragilityConfig()
    cc.betweenness_config.sample_sources = 150
    cc.betweenness_config.deterministic = True
    curve = gravel.cascade_vs_alpha(g, cc, [0.05, 0.1, 0.2, 0.5, 1.0])
    print("3. cascade size vs tolerance alpha")
    for p in curve:
        print(f"   alpha={p.alpha:<4}  cascade fraction {p.cascade_fraction:.3f}  "
              f"({p.iterations} rounds)")
    print("\n   Every modeling constant (PCE, failure probability, alpha) is a disclosed,")
    print("   sweepable input — report covariates as curves/distributions, not single numbers.")


if __name__ == "__main__":
    main()
