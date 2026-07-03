"""Flood-weighted road-fragility dashboard for a city (Asheville, NC by default).

End-to-end use of the 2.5.0 stack:

  real road geometry (2B)  →  FEMA NFHL flood zones (gravel.hazards.fetch_nfhl_flood_zones)
  →  per-edge flood probability (flood_edge_probabilities)
  →  a stochastic flood-driven removal order (viz.failure_sequence_from_probabilities)
  →  a self-contained dashboard: map + "% of trips severed" chart (viz.dashboard_html)

Requires the ``[viz]`` extra (geopandas/shapely/pyproj) and network access to FEMA. Needs
an OSM roads file for the city; grab one from Overpass, e.g.::

    curl -X POST https://overpass-api.de/api/interpreter --data-urlencode \\
      'data=[out:xml][timeout:170];(way["highway"~"^(motorway|trunk|primary|secondary|tertiary|unclassified|residential|living_street)(_link)?$"](35.55,-82.60,35.64,-82.52);>;);out body;' \\
      -o asheville.osm

Then::

    python examples/python/08_asheville_flood_dashboard.py asheville.osm asheville_flood.html
"""

import sys

import gravel
import numpy as np
from gravel import hazards, viz


def main(roads_path: str, out_path: str) -> None:
    if not gravel.HAS_OSM:
        raise SystemExit("this example needs an OSM-enabled build (gravel.HAS_OSM is False)")

    # 1. Roads -> simplified graph keeping real road geometry.
    g, _ = gravel.load_osm_graph_with_metadata(roads_path)
    cfg = gravel.SimplificationConfig()
    cfg.emit_geometry = True
    cfg.estimate_degradation = False
    sres = gravel.simplify_graph(g, None, None, cfg)
    sg = sres.graph
    print(f"roads: {g.node_count} -> {sg.node_count} nodes, {sg.edge_count} edges")

    # 2. FEMA NFHL flood zones for the graph's bounding box.
    coords = sg.node_coordinates()  # (N, 2) [lat, lon]
    bbox = (float(coords[:, 1].min()), float(coords[:, 0].min()),
            float(coords[:, 1].max()), float(coords[:, 0].max()))
    flood = hazards.fetch_nfhl_flood_zones(bbox)
    print(f"FEMA NFHL zones: {len(flood)}  ({flood['FLD_ZONE'].value_counts().to_dict()})")

    # 3. Per-edge flood probability (from the detailed polygons), then a stochastic
    #    flood-driven removal order.
    probs = hazards.flood_edge_probabilities(sg, flood)  # design-flood closure rates
    print(f"flood-exposed edges: {(probs > 0).sum()} of {sg.edge_count}")
    # Raw NFHL geometry is extremely detailed; simplify a display copy (~20 m) so the
    # embedded map layer stays small. Probabilities above used the full-detail polygons.
    flood_map = flood.copy()
    flood_map["geometry"] = flood.geometry.simplify(0.0002, preserve_topology=True)
    failure_round = viz.failure_sequence_from_probabilities(
        probs, seed=1, limit=1000, stages=50
    )
    removed = int((~np.isnan(failure_round)).sum())
    print(f"animating {removed} flood-driven removals over up to 50 stages")

    # 4. Dashboard: map (real geometry + FEMA risk layer) + "% of trips severed" chart.
    viz.dashboard_html(
        sg, failure_round, out_path,
        edge_geometry=sres.edge_geometry,
        hazard=flood_map, hazard_zone_field="FLD_ZONE",
        title="Asheville — flood-weighted road fragility (FEMA NFHL)",
    )
    print(f"wrote {out_path}")


if __name__ == "__main__":
    roads = sys.argv[1] if len(sys.argv) > 1 else "asheville.osm"
    out = sys.argv[2] if len(sys.argv) > 2 else "asheville_flood_dashboard.html"
    main(roads, out)
