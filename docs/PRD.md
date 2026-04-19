# Gravel — Product Requirements Document

**Version 2.1 | Last updated: April 2026**

## Executive Summary

Gravel is a C++ library with first-class Python bindings that quantifies how vulnerable road networks are to edge failures. It combines contraction-hierarchy routing with replacement-path fragility analysis to produce composite isolation scores for geographic regions, supporting disaster preparedness research, infrastructure planning, and transportation equity analysis.

## Problem Statement

Traditional routing libraries answer "what is the shortest path?" Gravel answers a harder question: **"how does that path degrade when edges fail?"** This matters because:

- Disaster response plans assume road networks remain functional; they rarely do
- Rural and mountain communities depend on single critical routes that have no viable detours
- Emergency services need to know where redundancy is genuinely absent versus merely reduced
- FEMA disaster declarations intersect with network topology in ways that aren't visible from map inspection alone

Gravel produces quantitative fragility scores that make these vulnerabilities comparable across counties, states, and regions.

## Users and Use Cases

### Primary users
- **Disaster sociology researchers** — studying how infrastructure failure correlates with FEMA disaster outcomes
- **Transportation planners** — identifying critical links that warrant redundancy investment
- **Emergency management** — pre-positioning resources where isolation risk is highest
- **Civil engineers** — bridge and road criticality analysis beyond traditional ADT-based methods

### Primary use cases

1. **Rank regions by vulnerability** — produce a fragility score for every county in a state/nation and rank them
2. **Scenario analysis** — given a hazard footprint (flood, wildfire, landslide), measure its impact on connectivity
3. **Progressive failure simulation** — degrade edges one at a time, plot the resulting isolation curve
4. **Inter-regional connectivity** — quantify how dependent one county is on another's roads
5. **Directional analysis** — reveal asymmetric vulnerability (e.g., a coastal town's evacuation routes north are fine but east is fragile)

## Architecture Overview

### Sub-library structure

Gravel v2.1 organizes code into six sub-libraries with a strict dependency DAG:

```
gravel-core      → (nothing)
gravel-ch        → gravel-core
gravel-simplify  → gravel-core, gravel-ch
gravel-fragility → gravel-core, gravel-ch, gravel-simplify
gravel-geo       → gravel-core, gravel-simplify
gravel-us        → gravel-geo
```

External dependencies are isolated:

| Dependency | Confined to | Optional |
|-----------|-------------|----------|
| libosmium | gravel-geo | Yes (GRAVEL_USE_OSMIUM) |
| Eigen + Spectra | gravel-fragility | No (always built) |
| nlohmann/json | gravel-geo, gravel-fragility | No |
| Apache Arrow | gravel-fragility (output) | Yes (GRAVEL_USE_ARROW) |

Consumers link only what they need. A tool that only does routing (no fragility) doesn't pull in Eigen/Spectra.

### Core data model

- **`ArrayGraph`** — structure-of-arrays CSR representation for cache efficiency
- **`ContractionResult`** — compiled contraction hierarchy for O(log n) shortest-path queries
- **`RegionAssignment`** — per-node region index (e.g., which county each node belongs to)
- **`IncrementalSSSP`** — reverse-incremental shortest-path engine for edge-removal analysis

### Analysis pipeline

```
OSM PBF ──▶ ArrayGraph ──▶ CH ──┬──▶ route queries (milliseconds)
                                │
                                ├──▶ location_fragility  (2s on 200K nodes)
                                │
                                ├──▶ county_fragility    (composite index)
                                │
                                └──▶ scenario_fragility  (hazard footprint)

County boundaries ──▶ RegionAssignment ──┬──▶ border_edges
                                         │
                                         ├──▶ boundary_nodes (protection)
                                         │
                                         └──▶ coarsen_graph (meta-graph)
```

## Functional Requirements

### FR-1: Graph Loading
- **FR-1.1** Load OSM PBF files with speed profiles (`load_osm_graph`)
- **FR-1.2** Load CSV edge lists with optional coordinates
- **FR-1.3** Build graphs from programmatic edge lists
- **FR-1.4** Serialize/deserialize graphs to binary format

### FR-2: Routing
- **FR-2.1** Shortest-path queries via contraction hierarchy (`CHQuery`)
- **FR-2.2** Bidirectional Dijkstra for verification
- **FR-2.3** Distance matrices with OpenMP parallelization
- **FR-2.4** Blocked-edge queries (`BlockedCHQuery`) for scenario analysis

### FR-3: Fragility Analysis
- **FR-3.1** Per-edge replacement-path ratios (`route_fragility`)
- **FR-3.2** Location-based isolation risk (`location_fragility`) with Monte Carlo/Greedy strategies
- **FR-3.3** County-level composite index (`county_fragility_index`)
- **FR-3.4** Scenario analysis with hazard footprints (`scenario_fragility`)
- **FR-3.5** Progressive elimination with degradation curves (`progressive_fragility`)
- **FR-3.6** Ensemble and uncertainty quantification

### FR-4: Geographic Analysis
- **FR-4.1** Point-in-polygon node assignment (`assign_nodes_to_regions`)
- **FR-4.2** GeoJSON boundary loading with coordinate swap
- **FR-4.3** Border edge summarization (`summarize_border_edges`)
- **FR-4.4** Graph coarsening (`coarsen_graph`)
- **FR-4.5** Boundary-aware simplification (preserves inter-regional nodes)
- **FR-4.6** Binary serialization of region assignments

### FR-5: US-Specific Support
- **FR-5.1** TIGER/Line loaders for counties, states, CBSAs, places, urban areas
- **FR-5.2** FIPS crosswalk (county→state, county→CBSA)
- **FR-5.3** Typed wrappers (`CountyAssignment`, `CBSAAssignment`)

### FR-6: Python Bindings
- **FR-6.1** Full API exposure via pybind11
- **FR-6.2** NumPy-compatible construction (edge lists as arrays)
- **FR-6.3** Optional OSM loading (graceful degradation if not built)

## Non-Functional Requirements

### Performance

| Target | Scale | Measured |
|--------|-------|----------|
| CH build | 200K nodes | 0.7s (M1 Mac, release) |
| Location fragility | 200K nodes, MC=20 | 2.1s |
| County fragility | 200K nodes, 20 OD pairs | 0.7s |
| National pipeline | 3,221 US counties | 3.1 hours |

### Quality
- **200+ unit tests** via Catch2 (C++) — every public function covered
- **Real-world validation** — Swain County OSM data as regression test
- **Spectral validation** — CH results cross-checked against Dijkstra
- **No memory leaks** — verified with ASan builds

### Portability
- Linux (x86_64, aarch64), macOS (x86_64, arm64), Windows (x86_64)
- C++20 compiler required
- Python 3.10+ for bindings
- CMake 3.20+

### Documentation
- Every public function has a Doxygen-style comment
- `REFERENCE.md` — complete API reference (1-for-1 with headers)
- PRD (this document) — architecture and requirements
- `examples/` — runnable tutorials in Python and C++
- Sphinx-generated API docs on GitHub Pages

## Key Design Decisions

### DD-1: Reverse-incremental SSSP over forward approach
For edge-removal analysis, we block all candidate edges first, run one blocked Dijkstra, then incrementally restore edges with bounded propagation. This is counterintuitive (most tools remove edges one at a time) but gives tight bounds and enables early termination.

### DD-2: Simplify before analyze
Degree-2 contraction reduces 200K-node county graphs to ~14K nodes with zero loss of shortest-path information. All analysis runs on the simplified graph; results map back to original node IDs via stored mapping.

### DD-3: Sample-based scoring
Location fragility scores use 200 target nodes (not all nodes) for distance inflation measurement. This caps per-trial cost at O(k) where k is sample count, independent of graph size. Accuracy is validated against full-node scoring on test graphs.

### DD-4: Composite score formula
```
isolation_risk = 0.5 * disconnected_fraction
               + 0.3 * normalized_distance_inflation
               + 0.2 * coverage_gap
```
Weights chosen empirically to rank Swain County and Bryson City as intuitively "more fragile" than Asheville. Formula is a single source of truth in `composite_formula.h`.

### DD-5: Graph coarsening for inter-county
For inter-county analysis, we coarsen the graph (one node per county) rather than running pairwise analysis on the full graph. This reduces a national matrix from O(3200²) expensive queries to O(3200) coarsening + O(adjacent pairs) cheap queries on a tiny graph.

## Version History

### v2.1 (April 2026) — Current
- **Major rewrite** of `location_fragility` using Dijkstra + IncrementalSSSP (~400x speedup)
- Sub-library architecture (6 CMake targets with enforced dependency DAG)
- New infrastructure: `EdgeSampler`, `IncrementalSSSP`, `RegionAssignment`, boundary-aware simplification
- New features: border edge summarization, graph coarsening, FIPS crosswalk, scenario fragility fast path
- Python API cleanup, region serialization, TIGER loaders
- National US county fragility pipeline (3,221 counties in ~3 hours)

### v2.0
- Progressive elimination fragility with Monte Carlo / Greedy strategies
- `AnalysisContext` performance cache
- Scenario fragility (hazard footprint intersection)
- Edge confidence scoring
- Tiled fragility analysis

### v1.0
- Initial release: routing, CH, route fragility, county fragility index

## Future Work

### Short term
- Inter-county fragility matrix for all US counties
- Choropleth visualization tooling
- CBSA / state-level aggregation helpers

### Medium term
- Temporal fragility (how networks degrade over construction schedules)
- Multi-modal integration (transit + walking)
- Stochastic edge failure modeling (probabilistic closure risk)

### Long term
- International road network support (non-TIGER boundaries)
- Real-time integration with traffic incident data
- ML-assisted edge importance ranking

## Non-Goals

- Gravel is not a turn-by-turn navigation library. Use OSRM or GraphHopper for that.
- Gravel is not a transit planner. Use OpenTripPlanner for GTFS/multi-modal.
- Gravel does not handle dynamic graphs. All CH operations assume a static edge set.
