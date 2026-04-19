"""Location fragility example — measure isolation risk for a geographic point.

Requires: Gravel built with GRAVEL_USE_OSMIUM=ON, and an OSM PBF file.

Download Swain County, NC from Geofabrik:
    https://download.geofabrik.de/north-america/us/north-carolina.html
or use a smaller extract.

Run: python examples/python/02_location_fragility.py path/to/county.osm.pbf
"""

import sys
import gravel


def main(pbf_path: str):
    # Load OSM PBF into a graph
    print(f"Loading {pbf_path}...")
    graph = gravel.load_osm_graph(pbf_path, gravel.SpeedProfile.car())
    print(f"  {graph.node_count:,} nodes, {graph.edge_count:,} edges\n")

    # Build CH
    print("Building contraction hierarchy...")
    ch = gravel.build_ch(graph)
    print("  Done\n")

    # Configure location fragility analysis
    cfg = gravel.LocationFragilityConfig()
    cfg.center = gravel.Coord(35.4312, -83.4496)  # Bryson City, NC
    cfg.radius_meters = 30000                       # 30 km
    cfg.removal_fraction = 0.10                     # block 10% of SP edges
    cfg.sample_count = 200                          # target nodes for scoring
    cfg.strategy = gravel.SelectionStrategy.MONTE_CARLO
    cfg.monte_carlo_runs = 20
    cfg.angular_bins = 8                            # 8 compass sectors
    cfg.seed = 42

    print("Computing location fragility...")
    result = gravel.location_fragility(graph, ch, cfg)
    print()

    print("=" * 60)
    print("RESULTS")
    print("=" * 60)
    print(f"Isolation risk:        {result.isolation_risk:.3f}  (0=safe, 1=critical)")
    print(f"AUC normalized:        {result.auc_normalized:.3f}")
    print(f"Baseline risk:         {result.baseline_isolation_risk:.3f}")
    print(f"Reachable nodes:       {result.reachable_nodes:,}")
    print(f"SP edges found:        {result.sp_edges_total:,}")
    print(f"SP edges removed:      {result.sp_edges_removed:,}")
    print(f"Simplified subgraph:   {result.subgraph_nodes:,} nodes, "
          f"{result.subgraph_edges:,} edges")
    print()
    print(f"Directional coverage:  {result.directional_coverage:.2f}  "
          f"(1=all sectors reachable)")
    print(f"Directional asymmetry: {result.directional_asymmetry:.3f}  "
          f"(0=uniform, 1=concentrated)")
    print()

    # Show degradation curve
    print("Degradation curve:")
    print(f"  {'k':>4}  {'mean_risk':>10}  {'std':>8}  {'p50':>8}  {'p90':>8}")
    print("  " + "-" * 50)
    for level in result.curve[::max(1, len(result.curve) // 10)]:  # ~10 samples
        print(f"  {level.k:>4}  {level.mean_isolation_risk:>10.3f}  "
              f"{level.std_isolation_risk:>8.3f}  "
              f"{level.p50:>8.3f}  {level.p90:>8.3f}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python 02_location_fragility.py path/to/county.osm.pbf")
        sys.exit(1)
    main(sys.argv[1])
