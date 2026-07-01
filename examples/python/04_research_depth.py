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
import numpy as np  # noqa: E402
from gravel import hazards  # noqa: E402


def _coord_grid(n):
    """n x n lattice with lat/lon coords in the unit square and 4-neighbour edges.

    Unlike ``make_grid_graph`` this carries coordinates, so hazard polygons can
    select edges by location.
    """
    coords = np.array(
        [[r / (n - 1), c / (n - 1)] for r in range(n) for c in range(n)],
        dtype=np.float64,
    )
    src, tgt = [], []
    for r in range(n):
        for c in range(n):
            for dr, dc in ((0, 1), (1, 0)):
                rr, cc = r + dr, c + dc
                if rr < n and cc < n:
                    a, b = r * n + c, rr * n + cc
                    src += [a, b]
                    tgt += [b, a]
    return gravel.Graph.from_coo(
        n * n,
        np.array(src, dtype=np.uint32),
        np.array(tgt, dtype=np.uint32),
        np.ones(len(src)),
        coords,
    )


def main():
    g = gravel.make_grid_graph(30, 30)
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
    # Floodplain-driven failures: build a coordinate-bearing network and flood a
    # lat-band corridor through it. hazard_edge_probabilities marks edges whose BOTH
    # endpoints fall in the flood polygon; everything else keeps a low background rate.
    fg = _coord_grid(20)
    fch = gravel.build_ch(fg)
    fidx = gravel.ShortcutIndex(fch)
    flood = gravel.Polygon()  # a horizontal band across the middle of the unit square
    flood.vertices = [
        gravel.Coord(0.40, -0.1), gravel.Coord(0.40, 1.1),
        gravel.Coord(0.60, 1.1), gravel.Coord(0.60, -0.1),
    ]
    probs = hazards.hazard_edge_probabilities(fg, [(flood, 0.75)], baseline=0.02)
    flooded = int((probs > 0.02).sum())
    # With real FEMA data (needs the [interop] extra):
    #   import geopandas as gpd
    #   probs = hazards.flood_edge_probabilities(fg, gpd.read_file("NFHL_*_FLD_HAZ_AR.shp"))

    sc = gravel.StochasticFragilityConfig()
    sc.monte_carlo_runs = 100
    sc.od_sample_count = 40
    sc.seed = 1
    res = gravel.stochastic_fragility(fg, fch, fidx, probs, sc)
    print("2. stochastic fragility — flooded corridor (p=0.75 in-zone, 0.02 else)")
    print(f"   {flooded}/{fg.edge_count} edges inside the floodplain")
    print(f"   mean inflation {res.mean:.3f}  p90 {res.p90:.3f}  p99 {res.p99:.3f}")
    print(f"   disconnected fraction {res.mean_disconnected_fraction:.3f}")
    print(f"   P(mean inflation > {sc.exceedance_thresholds}) = "
          f"{[round(x, 3) for x in res.exceedance]}\n")

    # Show the work: per-edge empirical failure probability (viz data bridge).
    pfail = np.asarray(res.edge_failure_frequency)
    print(f"   per-edge P(fail): {int((pfail > 0.5).sum())} edges > 0.5, max {pfail.max():.2f}")
    # To plot it (needs geopandas): gdf = gravel.viz.failure_geoframe(fg, res);
    #   gdf.plot(column="failure_frequency", cmap="viridis")  # colorblind-safe

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
