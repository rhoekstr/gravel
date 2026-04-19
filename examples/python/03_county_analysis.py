"""County analysis example — assign nodes to regions, compute county fragility.

Requires: Gravel built with GRAVEL_USE_OSMIUM=ON, plus a county GeoJSON
boundary file and an OSM PBF covering those counties.

Run: python examples/python/03_county_analysis.py county.osm.pbf counties.geojson
"""

import sys
import gravel


def main(pbf_path: str, geojson_path: str):
    # Load OSM graph
    print(f"Loading {pbf_path}...")
    graph = gravel.load_osm_graph(pbf_path, gravel.SpeedProfile.car())
    print(f"  {graph.node_count:,} nodes\n")

    # Build CH
    ch = gravel.build_ch(graph)
    idx = gravel.ShortcutIndex(ch)

    # Load county boundaries
    print(f"Loading regions from {geojson_path}...")
    regions = gravel.load_regions_geojson(geojson_path)
    print(f"  {len(regions)} regions loaded\n")

    # Assign every graph node to a region (point-in-polygon)
    print("Assigning nodes to regions...")
    assignment = gravel.assign_nodes_to_regions(graph, regions)
    print(f"  {assignment.unassigned_count:,} unassigned nodes\n")

    for i, r in enumerate(assignment.regions):
        count = assignment.region_node_counts[i]
        if count > 0:
            print(f"  {r.region_id} {r.label}: {count:,} nodes")

    # Summarize border edges
    print("\nBorder edge analysis...")
    borders = gravel.summarize_border_edges(graph, assignment)
    print(f"  {borders.total_border_edges:,} border edges across "
          f"{borders.connected_pairs} connected region pairs\n")

    # Coarsen the graph — each county becomes a single node
    print("Coarsening graph (county → meta-node)...")
    coarsened = gravel.coarsen_graph(graph, assignment, borders)
    print(f"  Coarsened: {coarsened.graph.node_count} regions, "
          f"{coarsened.graph.edge_count} edges\n")

    # For each region with enough data, compute county fragility
    print("Computing county fragility for each region...")
    for i, r in enumerate(assignment.regions):
        if assignment.region_node_counts[i] < 50:
            continue

        cfg = gravel.CountyFragilityConfig()
        cfg.boundary = r.boundary
        cfg.od_sample_count = 20
        cfg.skip_spectral = True  # faster
        cfg.seed = 42

        result = gravel.county_fragility_index(graph, ch, idx, cfg)
        print(f"  {r.region_id} {r.label}: "
              f"composite={result.composite_index:.3f}, "
              f"bridges={len(result.bridges.bridges)}, "
              f"subgraph={result.subgraph_nodes:,} nodes")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python 03_county_analysis.py county.osm.pbf counties.geojson")
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
