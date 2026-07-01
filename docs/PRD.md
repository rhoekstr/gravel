# Gravel — Product Requirements Document

**PRD revision 2.3 — reflects Gravel 2.3.0 | Last updated: June 2026**

## Executive Summary

Gravel is a C++20 library with first-class Python bindings that quantifies how vulnerable
road/infrastructure networks are to edge failures. It combines contraction-hierarchy routing with
replacement-path fragility analysis to produce composite isolation scores for geographic regions,
supporting disaster-preparedness research, infrastructure planning, and transportation-equity
analysis.

Gravel is **published and in use** — PyPI (`gravel-fragility`) and conda-forge, currently **2.3.0**,
Apache-2.0. It is deliberately **dual-purpose**:

- a **network-fragility research tool** — generating defensible, reproducible measures of how isolated
  a place becomes under infrastructure failure, for social-science and hazards research; and
- a **workforce-planning resource** (Awry Labs) — ranking where infrastructure isolation risk is
  highest for planners and emergency managers.

> **Scope note (2026-07):** Gravel began as a disaster-sociology dissertation covariate, but that tie
> has ended — road fragility washed out under covariates in the post-disaster-crime analysis (a null
> result). Gravel is now a **general** network-fragility tool with an academic/research core and a
> public-sector audience, not a bespoke covariate generator for one study.

That dual purpose is the lens for every prioritization call in this document: a change earns its
place when it serves a defensible research measure, an adopter's workflow, or (ideally) both.

## Problem Statement

Traditional routing libraries answer "what is the shortest path?" Gravel answers a harder question:
**"how does that path degrade when edges fail?"** This matters because:

- Disaster-response plans assume road networks remain functional; they rarely do
- Rural and mountain communities depend on single critical routes that have no viable detours
- Emergency services need to know where redundancy is genuinely absent versus merely reduced
- FEMA disaster declarations intersect with network topology in ways that aren't visible from map
  inspection alone

Gravel produces quantitative fragility scores that make these vulnerabilities comparable across
counties, states, and regions.

## Users and Use Cases

### Primary users
- **Disaster-sociology researchers** — studying how infrastructure failure correlates with FEMA
  disaster outcomes
- **Transportation planners** — identifying critical links that warrant redundancy investment
- **Emergency management** — pre-positioning resources where isolation risk is highest
- **Civil engineers** — bridge and road criticality analysis beyond traditional ADT-based methods

### Primary use cases

1. **Rank regions by vulnerability** — produce a fragility score for every county in a state/nation
   and rank them
2. **Scenario analysis** — given a hazard footprint (flood, wildfire, landslide), measure its impact
   on connectivity
3. **Progressive failure simulation** — degrade edges one at a time, plot the resulting isolation
   curve
4. **Inter-regional connectivity** — quantify how dependent one county is on another's roads
5. **Directional analysis** — reveal asymmetric vulnerability (e.g., a coastal town's evacuation
   routes north are fine but east is fragile)

## What Gravel Computes Today (current capability, 2.2.3)

Gravel's fragility analysis is **purely topological**: it removes edges and recomputes
shortest-paths / isolation. There is no flow, capacity, load, or congestion model — edge `Weight` is
a fixed travel-time scalar. This is a deliberate, defensible position (see Design Decisions and
Roadmap), not an omission.

**Routing & core**
- OSM PBF, CSV, and programmatic graph construction; binary serialize/deserialize
- Contraction-hierarchy shortest-path and distance-matrix queries; blocked-edge queries
- Degree-2 simplification, bridge detection, structure-of-arrays CSR representation
- Region collapse to a meta-graph (`ReducedGraph`, v2.2.0) for inter-region work

**Fragility analyses** (all topological, edge-removal based)
- **Route** (`route_fragility`) — per-edge replacement-path ratio along an s→t path; bottleneck ID
- **Location** (`location_fragility`) — isolation-risk degradation curve for a point as local roads
  fail
- **County** (`county_fragility_index`) — composite index over a polygon region
- **Scenario** (`scenario_fragility`) — baseline vs. hazard-footprint delta over a blocked edge set
- **Progressive** (`progressive_fragility`) — degradation curve over k removals (greedy / betweenness
  / random)
- **Tiled** (`tiled_fragility_analysis`) — spatial grid of location fragility for heatmaps
- **Inter-region / inter-county** — fragility between collapsed regions; national adjacency-driven
  pipeline

**Geographic & US**
- Point-in-polygon node assignment, GeoJSON boundary loading, border-edge summarization, coarsening
- TIGER/Line loaders (counties, states, CBSAs, places, urban areas), FIPS crosswalks

### Current limitations

The Phase 1 interop keystone (2.3.0) closed the Python-surface gap that previously motivated the
roadmap. **Resolved in 2.3.0:** node-coordinate extraction (`Graph.node_coordinates`), COO
accessors (`Graph.to_coo` / `from_coo`), per-edge OSM metadata in Python
(`load_osm_graph_with_metadata` → `EdgeMetadata`), GeoJSON/JSONL/Parquet export bindings, and
NetworkX/GeoPandas adapters (`gravel.interop`).

Remaining, and shaping the rest of the roadmap:

- **OSM capacity-relevant tags are exposed but not yet modeled.** `highway`, `lanes`, `maxspeed`
  now survive to Python as `EdgeMetadata`, but Gravel does not derive a capacity/PCE estimate or use
  it in ranking. That derivation is Phase 2A (capacity-as-input — see Roadmap).
- **No per-edge geometry.** Edges store endpoints and a weight, not the intermediate OSM way
  polyline — so `to_geodataframe` renders straight node-to-node segments. Persisting real geometry
  is Phase 2B.

## Architecture Overview

### Sub-library structure

Gravel (v2.1+) organizes code into six sub-libraries with a strict dependency DAG:

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
| libosmium | gravel-geo | Yes (GRAVEL_USE_OSMIUM, AUTO by default since 2.2.2) |
| Eigen + Spectra | gravel-fragility | No (always built) |
| nlohmann/json | gravel-geo, gravel-fragility | No |
| Apache Arrow | gravel-fragility (output) | Yes (GRAVEL_USE_ARROW) |

Consumers link only what they need. A tool that only does routing (no fragility) doesn't pull in
Eigen/Spectra. **The DAG is enforced, not advisory: an include that crosses a boundary the wrong way
is a build error.** This constraint shapes the roadmap directly — see the capacity design constraint
in Phase 2A.

### Core data model

- **`ArrayGraph`** — structure-of-arrays CSR representation for cache efficiency
- **`ContractionResult`** — compiled contraction hierarchy for fast shortest-path queries
- **`RegionAssignment`** — per-node region index (e.g., which county each node belongs to)
- **`ReducedGraph`** — region-collapsed meta-graph (one node per region) for inter-region analysis
- **`IncrementalSSSP`** — reverse-incremental shortest-path engine for edge-removal analysis

### Analysis pipeline

```
OSM PBF ──▶ ArrayGraph ──▶ CH ──┬──▶ route queries (microseconds)
                                │
                                ├──▶ location_fragility  (~2s on 200K nodes)
                                │
                                ├──▶ county_fragility    (composite index)
                                │
                                ├──▶ scenario_fragility  (hazard footprint)
                                │
                                └──▶ progressive_fragility (degradation curve)

County boundaries ──▶ RegionAssignment ──┬──▶ border_edges
                                         │
                                         ├──▶ boundary_nodes (protection)
                                         │
                                         └──▶ coarsen_graph ──▶ ReducedGraph ──▶ inter-region fragility
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
- **FR-3.6** Tiled fragility for heatmaps (`tiled_fragility_analysis`)
- **FR-3.7** Inter-region / inter-county fragility on collapsed graphs (v2.2.0)
- **FR-3.8** Ensemble and uncertainty quantification

### FR-4: Geographic Analysis
- **FR-4.1** Point-in-polygon node assignment (`assign_nodes_to_regions`)
- **FR-4.2** GeoJSON boundary loading with coordinate swap
- **FR-4.3** Border edge summarization (`summarize_border_edges`)
- **FR-4.4** Graph coarsening (`coarsen_graph`) to `ReducedGraph`
- **FR-4.5** Boundary-aware simplification (preserves inter-regional nodes)
- **FR-4.6** Binary serialization of region assignments

### FR-5: US-Specific Support
- **FR-5.1** TIGER/Line loaders for counties, states, CBSAs, places, urban areas
- **FR-5.2** FIPS crosswalk (county→state, county→CBSA)
- **FR-5.3** Typed wrappers (`CountyAssignment`, `CBSAAssignment`)

### FR-6: Python Bindings
- **FR-6.1** Core API exposure via pybind11 (graph construction, routing, all fragility analyses)
- **FR-6.2** NumPy-compatible construction (CSR arrays, edge lists)
- **FR-6.3** Optional OSM loading with graceful degradation (`HAS_OSM` feature flag)
- **FR-6.4** *(delivered 2.3.0)* node coordinates (`Graph.node_coordinates`), COO accessors
  (`to_coo`/`from_coo`), per-edge OSM metadata (`load_osm_graph_with_metadata` → `EdgeMetadata`),
  GeoJSON/JSONL/Parquet export bindings (`HAS_ARROW` flag), and NetworkX/GeoPandas adapters
  (`gravel.interop`, `gravel[interop]` extra)

## Non-Functional Requirements

### Performance

| Target | Scale | Measured |
|--------|-------|----------|
| CH build | 200K nodes | ~0.7s (M1 Mac, release) |
| Location fragility | 200K nodes, MC=20 | ~2.1s |
| County fragility | 200K nodes, 20 OD pairs | ~0.7s |
| National pipeline | 3,221 US counties | ~3.1 hours |
| Inter-county pipeline | ~8,547 adjacent pairs | ~22 hours |

Representative figures; see `README.md` for the full benchmark table (including 593K-node graphs).

### Quality
- **200+ unit tests** via Catch2 (C++) — every public function covered; pytest for Python bindings
- **Real-world validation** — Swain County OSM data as regression test
- **Cross-checked** — CH results verified against Dijkstra
- **No memory leaks** — verified with ASan builds

### Portability
- Linux (x86_64, aarch64), macOS (x86_64, arm64), Windows (x86_64, since 2.2.1)
- OSM ships in every published wheel across 20 platforms (since 2.2.2)
- C++20 compiler required; Python 3.10+ for bindings; CMake 3.24+

### Documentation
- Every public function has a Doxygen-style comment
- `REFERENCE.md` — complete API reference (1-for-1 with headers)
- `CLAUDE.md` — repository working reference (DAG, module map, build/test, conventions)
- PRD (this document) — architecture, requirements, direction
- `CHANGELOG.md` — every user-visible change (Keep-a-Changelog, SemVer)
- `examples/` — runnable tutorials in Python and C++

## Key Design Decisions

### DD-1: Reverse-incremental SSSP over forward approach
For edge-removal analysis, we block all candidate edges first, run one blocked Dijkstra, then
incrementally restore edges with bounded propagation. This is counterintuitive (most tools remove
edges one at a time) but gives tight bounds and enables early termination.

### DD-2: Simplify before analyze
Degree-2 contraction reduces 200K-node county graphs to ~14K nodes with zero loss of shortest-path
information. All analysis runs on the simplified graph; results map back to original node IDs via
stored mapping.

### DD-3: Sample-based scoring
Location fragility scores use ~200 target nodes (not all nodes) for distance-inflation measurement.
This caps per-trial cost at O(k) where k is sample count, independent of graph size. Accuracy is
validated against full-node scoring on test graphs.

### DD-4: Composite score formula
```
isolation_risk = 0.5 * disconnected_fraction
               + 0.3 * normalized_distance_inflation
               + 0.2 * coverage_gap
```
Weights chosen empirically to rank Swain County and Bryson City as intuitively "more fragile" than
Asheville. Formula is a single source of truth in `composite_formula.h`.

### DD-5: Graph coarsening for inter-county
For inter-county analysis, we coarsen the graph (one node per county) into a `ReducedGraph` rather
than running pairwise analysis on the full graph. This reduces a national matrix from O(3200²)
expensive queries to O(3200) coarsening + O(adjacent pairs) cheap queries on a tiny graph.

### DD-6: Topology, not flow (and why)
Gravel models edge failure as removal + shortest-path recomputation, not as load redistribution under
a demand model. For a research covariate this is a *feature*: a cut-vertex or replacement-path ratio
is an objective topological fact a reviewer cannot attack, whereas a flow/equilibrium result inherits
the validity risk of an uncalibrated demand matrix and capacity parameters. Where modeling depth is
added (capacity weighting, failure probability), it enters as **disclosed, sensitivity-tested
inputs** — never as a new simulated paradigm baked into the core. See Roadmap and Non-Goals.

## Version History

### v2.4.0 (in review) — Phase 2A research depth
- HCM capacity model + capacity-aware betweenness (criticality + weighted importance);
  `stochastic_fragility` (distribution over per-edge failures, floodplain-ready);
  `cascade_fragility` (Motter–Lai, experimental). Modeling constants are disclosed, sweepable inputs.

### v2.3.0 (June 2026)
- **Interop keystone (Phase 1).** Python surface gains `Graph.node_coordinates` / `has_coordinates`,
  `to_coo` / `from_coo`; per-edge OSM metadata via `load_osm_graph_with_metadata` → `EdgeMetadata`;
  GeoJSON/JSONL/Parquet export bindings + `HAS_ARROW` flag; and the `gravel.interop` NetworkX /
  GeoPandas adapter module (`gravel[interop]` extra). Backward compatible; DAG preserved.
- **Performance & parallelism hardening.** macOS OpenMP detection (Homebrew `libomp`) — macOS
  builds were previously silently serial; `HAS_OPENMP` / `max_threads()` / `set_max_threads()`
  visibility + control; `route_fragility` parallelized over path edges (was serial); national
  pipeline `--jobs` process pool with per-worker thread caps; GIL released on heavy calls;
  `BetweennessConfig.deterministic` for reproducible, thread-count-invariant betweenness.

### v2.2.3 (June 2026)
- PyPI/README metadata linked to Awry Labs project pages

### v2.2.2 (April 2026)
- OSM ships in every published wheel (20 platforms); `HAS_OSM` feature flag;
  `GRAVEL_USE_OSMIUM=AUTO` default

### v2.2.1 (April 2026)
- Windows support (CreateFileMappingW backend); CMake ≥3.24 (FetchContent FIND_PACKAGE_ARGS)

### v2.2.0 (April 2026)
- `ReducedGraph` (region collapse); inter-region fragility; national adjacency-driven inter-county
  pipeline

### v2.1 (April 2026)
- **Major rewrite** of `location_fragility` using Dijkstra + IncrementalSSSP (~400x speedup)
- Sub-library architecture (6 CMake targets with enforced dependency DAG)
- New infrastructure: `EdgeSampler`, `IncrementalSSSP`, `RegionAssignment`, boundary-aware
  simplification
- New features: border edge summarization, graph coarsening, FIPS crosswalk, scenario fragility fast
  path
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

## Roadmap

**Organizing principle: expose what already exists before building what doesn't, and add modeling
depth as inputs rather than new paradigms.** Several of the most-requested enhancements (ecosystem
interop, richer maps, capacity awareness) turn out to be blocked on a single gap — C++ capability
that never reached the Python surface. Closing that gap is the keystone; everything else builds on it.

Each phase ships as a deliberate version bump with `REFERENCE.md` + `CHANGELOG.md` updates and tests,
preserving the public API and wheel build.

### Phase 1 — Interop keystone ✅ shipped in 2.3.0

Mostly *exposing existing C++* to pybind11; unblocked both audiences at once.

- ✅ Per-edge OSM metadata exposed via `load_osm_graph_with_metadata` → `EdgeMetadata` (highway class,
  `lanes`, `maxspeed`, bridge/tunnel, name/ref/surface), in CSR edge order aligned with `to_coo()`.
- ✅ `Graph.node_coordinates()` as an (N, 2) array, plus `to_coo()` / `from_coo()` COO accessors.
- ✅ GeoJSON (`route_to_geojson`, `location_fragility_to_geojson`), JSONL, and Arrow Parquet writers
  surfaced to Python; `gravel.HAS_ARROW` flag.
- ✅ `gravel.interop` pure-Python adapters (`gravel[interop]` extra, lazy imports): `to_networkx` /
  `from_networkx`, `to_geodataframe` / `from_geodataframe`. The mature geo-Python stack (Folium via
  `gdf.explore()`, kepler.gl, matplotlib) then comes for free.

### Performance hardening ✅ shipped in 2.3.0

Reprioritized ahead of Phase 2A: an audit found the existing OpenMP instrumentation was not
*delivering* in practice. The fix made parallelism real, exploited the largest parallel axis, and
gave reproducibility a guarantee.

- ✅ **macOS no longer silently serial.** `cmake/OpenMPDetect.cmake` finds Homebrew `libomp` on Apple
  Clang and builds a working OpenMP target (mac wheels install `libomp`). ~4.6× betweenness where it
  was serial.
- ✅ **Visibility + control:** `gravel.HAS_OPENMP`, `max_threads()`, `set_max_threads()`
  (`GRAVEL_HAS_OPENMP` in C++).
- ✅ **`route_fragility` parallelized over path edges** — a real bug the audit surfaced (only
  `batch_fragility` was threaded). Test #217 went from a multi-minute hang to ~9 s.
- ✅ **National pipeline `--jobs` process pool** over counties (the biggest untapped axis), with
  per-worker `OMP_NUM_THREADS = cores // jobs` to avoid oversubscription.
- ✅ **GIL released** on the heavy compute calls (interactivity + Python-thread parallelism).
- ✅ **Reproducible covariates:** `BetweennessConfig.deterministic` gives bit-identical, thread-count-
  invariant betweenness; Monte Carlo statistics were already deterministic (sort-before-aggregate).

### Phase 2A — Research depth (capacity → stochastic → cascade) ✅ built for 2.4.0 (in review)

Serves Gravel's network-fragility research identity. Sequenced because each step feeds
the next. Implemented as `capacity` (HCM model + capacity-aware betweenness), `stochastic_fragility`
(distribution over per-edge failures; floodplain-ready), and `cascade_fragility` (Motter–Lai,
experimental). All modeling constants are disclosed, sweepable inputs; the DAG holds (capacity /
probabilities enter fragility as arrays, derivation in geo/Python).

- **Capacity-weighted edge importance.** Derive a per-edge Passenger-Car-Equivalent estimate from
  highway class × `lanes` (class-default fallbacks when `lanes` is unknown); weight
  betweenness/progressive ranking by throughput so high-capacity cuts rank higher.
  - **Design constraint (DAG):** capacity enters `gravel-fragility` as an optional input array (like
    weights); the *derivation* from OSM tags lives in `gravel-geo` / Python. `gravel-fragility` must
    not `#include` geo — that's a build error, not a style note.
  - **Validity:** the per-lane PCE constants are a modeling assumption — disclosed and
    sensitivity-tested across a range, never a single hidden value.
- **Stochastic edge failure** (promoted from prior "medium term"). Per-edge failure probabilities →
  Monte Carlo over realizations → a *distribution* of fragility (confidence intervals) rather than a
  point estimate. Embarrassingly parallel — the natural home for additional OpenMP if profiling
  confirms a hotspot. Strong research fit: gives a measure with uncertainty bounds.
  - **Flagship hazard source — floodplain-weighted closure.** ✅ built for 2.4.0 (`gravel.hazards`).
    `flood_edge_probabilities` ingests FEMA NFHL floodplain polygons and maps flood-zone codes to
    per-edge closure probability (disclosed `NFHL_EVENT_CLOSURE` design-flood-scenario default and
    `NFHL_ANNUAL_PROBABILITY` annual-exceedance tables), feeding `stochastic_fragility`; the
    geopandas-free `hazard_edge_probabilities` core generalizes to any polygonal hazard (wildfire,
    landslide, storm surge). Reuses the shipped `edges_in_polygon`; derivation lives in Python, fed to
    fragility as an input array (DAG-clean). *Not yet done:* elevation-refined exposure, and finer
    edge–zone matching once real edge geometry lands (2B) — today's predicate is both-endpoints-inside.
- **Cascading failure — Motter–Lai style (experimental).** Load = betweenness (already computed),
  capacity = (1+α)×initial load; remove an edge → redistribute → fail anything over capacity →
  iterate to a fixed point. Built on existing betweenness + `progressive_fragility` machinery; no
  demand matrix, no static-CH violation (analysis removes edges by recomputation, not by mutating the
  hierarchy). Reported as cascade-size-vs-α curves, not a single α.

### Phase 2B — Adoption polish

- **Persist edge polyline geometry.** Store the intermediate OSM way geometry per edge so maps render
  real road shapes instead of straight node-to-node segments. Prerequisite for honest cartography.
  *(geo layer, optional, memory cost noted)*
- **Visualization** (`gravel.viz`, `gravel[viz]` extra, pure Python; downstream of `to_geodataframe`,
  never in the C++ core). Staged:
  - ✅ **Tier 0 (data bridge, shipped 2.4.0).** Results expose a per-edge failure trace
    (`edge_failure_round` from progressive greedy removal order; `edge_failure_frequency` from
    stochastic MC) and `failure_geoframe` returns a plot-ready `GeoDataFrame`. Zero new deps.
  - **Tier 1 + 2 (2.5.0 — comprehensive visual update).** Two audiences, two modes (durable design
    principle): **static** = the researcher's *accurate* artifact (quantitative choropleth,
    colorblind-safe sequential colormap, honest about uncertainty; matplotlib/geopandas); **dynamic**
    = comprehension + public reach (watch isolation propagate; pydeck/lonboard WebGL; dots/lines/grid
    texture as an ordinal encoding). Not aimed at B&W journal figures. Geo-viz credibility is coupled
    to the 2B edge-geometry work above — label maps schematic until it lands.

### Hardening & operational (surfaced during the 2.3.0 release)

The 2.3.0 wheel build — blocked for hours by a gitlab.com outage that cascaded through Eigen 5.0 and
cross-arch libomp — exposed that build-time dependency clones are a release liability. Follow-ups:

- **Finish build network-independence.** Eigen is now vendored (`third_party/eigen`), but Spectra,
  nlohmann/json, pybind11, and Catch2 still `FetchContent`-clone from github at build time. Vendor or
  checksum-pin the rest so a release can never be blocked by an upstream host outage.
- **conda-forge → 2.3.0.** The feedstock still targets 2.2.x; bump it to match the PyPI release.
- ✅ **Refreshed headline performance numbers** (2026-07-01, `bench/baselines/routing_performance.md`).
  Re-benchmarked Release + OpenMP on real counties: distance matrix and `route_fragility` are ~5×
  faster on macOS post-2.3.0 (the April numbers were effectively serial), confirmed by a 1→10-thread
  scaling curve on the same machine. Single-threaded ops unchanged. The `perf_baseline.json`
  Google-Benchmark regression gate is refreshed separately via `gravel_perf`.
- ~~Extend deterministic mode to `kirchhoff_index` / `natural_connectivity`~~ — **not needed.** Audit
  found both are serial (no OpenMP) and seeded, hence already reproducible; there is no unpinned
  parallel reduction to fix. (If they are ever parallelized, they'll need the H4 treatment then.)

### Longer-horizon / research-track

- Temporal fragility (degradation over construction schedules)
- International road-network support (non-TIGER boundaries)
- ML-assisted edge-importance ranking

## Non-Goals

- Gravel is **not a turn-by-turn navigation library.** Use OSRM or GraphHopper for that.
- Gravel is **not a transit planner.** Use OpenTripPlanner for GTFS/multi-modal.
- Gravel's CH is **static topology.** All CH operations assume a fixed edge set; the hierarchy is
  built once and is not incrementally mutated. (Fragility analysis still removes edges — by
  recomputation on the degraded graph, not by editing the CH.)
- Gravel is **not a traffic-assignment / user-equilibrium engine.** Full flow modeling (BPR cost
  functions plus an origin-destination demand matrix solved to equilibrium) is out of scope: it
  requires data Gravel cannot calibrate and would trade a defensible topological covariate for a
  parameter-laden one. The complex-networks cascade model in Phase 2A is the supported alternative.
- Gravel does **not yet** model interdependent multi-layer infrastructure as a first-class graph. When
  needed, coupled failures are expressed as an externally-built edge set fed to `scenario_fragility`;
  a true multi-layer core is reserved for a research need that demands interdependency covariates.
