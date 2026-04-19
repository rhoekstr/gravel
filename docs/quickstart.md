# Quick Start

## 1. Load a road network

Gravel supports three graph sources:

### From an OSM PBF file (requires libosmium)

```python
import gravel

# Download Swain County NC from Geofabrik first:
#   https://download.geofabrik.de/north-america/us/north-carolina.html
graph = gravel.load_osm_graph("swain_county.osm.pbf", gravel.SpeedProfile.car())
print(f"{graph.node_count:,} nodes, {graph.edge_count:,} edges")
```

### From a CSV edge list

```python
# CSV format: source_id,target_id,weight[,secondary_weight]
from gravel import CSVGraphLoader
graph = CSVGraphLoader.load("edges.csv")
```

### Programmatically

```python
import numpy as np
import gravel

# Build a 10x10 grid graph
graph = gravel.make_grid_graph(10, 10)
```

## 2. Build a contraction hierarchy

One-time cost that makes subsequent queries orders of magnitude faster:

```python
ch = gravel.build_ch(graph)
q = gravel.CHQuery(ch)

# Single-pair shortest path
result = q.route(source=0, target=99)
print(f"Distance: {result.distance}s, Path length: {len(result.path)} nodes")
```

## 3. Compute isolation fragility

Location fragility measures how vulnerable a specific point is to road failures:

```python
cfg = gravel.LocationFragilityConfig()
cfg.center = gravel.Coord(35.43, -83.45)  # lat/lon
cfg.radius_meters = 30000                   # 30km search radius
cfg.monte_carlo_runs = 20                   # statistical confidence
cfg.removal_fraction = 0.10                 # remove 10% of SP edges
cfg.seed = 42                                # reproducibility

result = gravel.location_fragility(graph, ch, cfg)

print(f"Isolation risk: {result.isolation_risk:.3f}")  # 0 = perfect, 1 = maximum risk
print(f"Reachable nodes: {result.reachable_nodes:,}")
print(f"Directional coverage: {result.directional_coverage:.2f}")
print(f"Directional asymmetry: {result.directional_asymmetry:.3f}")
```

### Interpret the result

- **`isolation_risk`** ∈ [0, 1]: composite score combining disconnection, distance inflation, coverage
- **`curve`**: degradation curve as edges are progressively restored
- **`directional_coverage`**: fraction of compass sectors reachable under max removal
- **`directional_asymmetry`**: HHI concentration of per-sector fragility

## 4. Assign regions and analyze counties

If you have boundary polygons (e.g., US Census TIGER files):

```python
# Load county boundaries
counties = gravel.load_regions_geojson("counties.geojson")

# Assign every graph node to a county
assignment = gravel.assign_nodes_to_regions(graph, counties)

# Summarize edges that cross county boundaries
borders = gravel.summarize_border_edges(graph, assignment)
print(f"{borders.total_border_edges} border edges across {borders.connected_pairs} county pairs")

# Collapse into a meta-graph (one node per county)
coarsened = gravel.coarsen_graph(graph, assignment, borders)
print(f"Coarsened: {coarsened.graph.node_count} regions, {coarsened.graph.edge_count} edges")
```

## 5. Scenario analysis

Simulate a hazard footprint (flood, wildfire, landslide):

```python
# Define hazard polygon
hazard = gravel.Polygon()
# ... populate with vertices ...

cfg = gravel.ScenarioConfig()
cfg.baseline.boundary = county_boundary
cfg.hazard_footprint = hazard  # automatically finds affected edges

idx = gravel.ShortcutIndex(ch)
result = gravel.scenario_fragility(graph, ch, idx, cfg)

print(f"Baseline composite: {result.baseline.composite_index:.3f}")
print(f"Scenario composite: {result.scenario.composite_index:.3f}")
print(f"Delta: +{result.delta_composite:.3f} ({result.relative_change*100:.1f}% increase)")
print(f"Edges blocked: {result.edges_blocked}")
print(f"Bridges blocked: {result.bridges_blocked}")
```

## 6. Progressive elimination

Build a full degradation curve:

```python
cfg = gravel.ProgressiveFragilityConfig()
cfg.base_config.boundary = county_boundary
cfg.k_max = 50  # remove up to 50 edges
cfg.strategy = gravel.SelectionStrategy.GREEDY_BETWEENNESS
cfg.monte_carlo_runs = 1  # greedy is deterministic

result = gravel.progressive_fragility(graph, ch, idx, cfg)

for level in result.curve[::5]:  # every 5th level
    print(f"k={level.k:3d}: composite={level.mean_composite:.3f}")
```

## Next Steps

- **[Concepts](concepts.md)** — deeper dive into the algorithms
- **[Pipeline](pipeline.md)** — running the national US county analysis
- **[API Reference](api/python.md)** — complete function reference
- **[Examples](examples/location_fragility.md)** — runnable notebooks
