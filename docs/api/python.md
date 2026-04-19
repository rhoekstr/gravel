# Python API Reference

The `gravel` package exposes the full C++ library via pybind11 bindings.

```{eval-rst}
.. autosummary::
    :toctree: generated

    gravel
```

## Core

```{eval-rst}
.. automodule:: gravel
    :members: Graph, CH, CHQuery, RouteResult, Coord, Polygon
    :no-index:
```

## Routing

```{eval-rst}
.. currentmodule:: gravel

.. autofunction:: build_ch
.. autofunction:: dijkstra_pair
.. autoclass:: CHQuery
    :members:
.. autoclass:: BlockedCHQuery
    :members:
```

## Fragility

### Route Fragility

```{eval-rst}
.. autoclass:: FragilityResult
    :members:
.. autoclass:: EdgeFragility
    :members:
.. autofunction:: route_fragility
.. autofunction:: batch_fragility
```

### Location Fragility

```{eval-rst}
.. autoclass:: LocationFragilityConfig
    :members:
.. autoclass:: LocationKLevel
    :members:
.. autoclass:: LocationFragilityResult
    :members:
.. autofunction:: location_fragility
```

### County Fragility

```{eval-rst}
.. autoclass:: CountyFragilityConfig
    :members:
.. autoclass:: CountyFragilityResult
    :members:
.. autofunction:: county_fragility_index
```

### Scenario Fragility

```{eval-rst}
.. autoclass:: ScenarioConfig
    :members:
.. autoclass:: ScenarioResult
    :members:
.. autofunction:: scenario_fragility
.. autofunction:: edges_in_polygon
```

### Progressive Elimination

```{eval-rst}
.. autoclass:: ProgressiveFragilityConfig
    :members:
.. autoclass:: ProgressiveFragilityResult
    :members:
.. autofunction:: progressive_fragility
.. autoclass:: SelectionStrategy
    :members:
```

## Network Analysis

```{eval-rst}
.. autofunction:: algebraic_connectivity
.. autofunction:: edge_betweenness
.. autofunction:: kirchhoff_index
.. autofunction:: natural_connectivity
.. autoclass:: BridgeResult
    :members:
.. autofunction:: extract_subgraph
```

## Geographic Analysis

### Region Assignment

```{eval-rst}
.. autoclass:: RegionSpec
    :members:
.. autoclass:: RegionAssignment
    :members:
.. autofunction:: assign_nodes_to_regions
.. autofunction:: load_regions_geojson
.. autofunction:: boundary_nodes
.. autofunction:: save_region_assignment
.. autofunction:: load_region_assignment
```

### Border Edges & Coarsening

```{eval-rst}
.. autoclass:: BorderEdgeResult
    :members:
.. autoclass:: BorderEdgeSummary
    :members:
.. autofunction:: summarize_border_edges
.. autoclass:: CoarseningConfig
    :members:
.. autoclass:: CoarseningResult
    :members:
.. autofunction:: coarsen_graph
```

## US-Specific (TIGER/FIPS)

```{eval-rst}
.. autofunction:: load_tiger_counties
.. autofunction:: load_tiger_states
.. autofunction:: load_tiger_cbsas
.. autofunction:: load_tiger_places
.. autofunction:: load_tiger_urban_areas
```

## OSM Loading (optional)

Only available when built with `GRAVEL_USE_OSMIUM=ON`.

```{eval-rst}
.. autoclass:: SpeedProfile
    :members:
.. autoclass:: OSMConfig
    :members:
.. autofunction:: load_osm_graph
```

## Edge Sampling

```{eval-rst}
.. autoclass:: SamplingStrategy
    :members:
.. autoclass:: SamplerConfig
    :members:
.. autoclass:: EdgeSampler
    :members:
```

## Snapping & Elevation

```{eval-rst}
.. autofunction:: snap_quality
.. autoclass:: SnapQualityReport
    :members:
.. autofunction:: load_srtm_elevation
.. autofunction:: classify_closure_risk
```
