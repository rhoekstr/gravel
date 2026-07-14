# Gravel — Product Requirements Document

**PRD revision 3.0 — reflects Gravel 3.0.0: the `gravel.flow` traffic-assignment layer ships public but experimental (real θ under-identified at corridor scale; see [`FLOW_LAYER.md`](FLOW_LAYER.md)); the 2.6 deprecation shims are removed (breaking). Cascade stays experimental (Phase 5). | Last updated: 2026-07-13**

## Executive Summary

Gravel is a C++20 library with first-class Python bindings that quantifies how vulnerable
road/infrastructure networks are to edge failures. It combines contraction-hierarchy routing with
replacement-path fragility analysis to produce composite isolation scores for geographic regions,
supporting disaster-preparedness research, infrastructure planning, and transportation-equity
analysis.

Gravel is **published and in use** — PyPI (`gravel-fragility`), currently **2.7.0**, Apache-2.0.
(conda-forge is not a current channel; the feedstock is out of date — install via pip.) It is
deliberately **dual-purpose**:

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

## What Gravel Computes Today (current capability, 2.7.0)

Gravel's **core fragility is topological**: it removes edges and recomputes shortest-paths /
isolation, with no traffic-assignment flow, congestion, or user-equilibrium model — edge `Weight` is
a fixed travel-time scalar. Where modeling depth is added (capacity-weighted criticality since 2.4.0,
per-edge failure probabilities, the experimental cascade, and real substrate capacity since 2.7.0),
it enters as **disclosed, sensitivity-tested input arrays**, never as a new simulated paradigm in the
core. This is a deliberate, defensible position (see DD-6 and the Roadmap), not an omission.

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

**Analytical depth** (2.4.0; disclosed, sweepable inputs — DD-6)
- `estimate_capacity` — per-edge PCE from `highway`/`lanes`; capacity-weighted betweenness (criticality)
- `stochastic_fragility` — Monte Carlo over per-edge failure probabilities → a fragility *distribution*
- `cascade_fragility` — Motter–Lai load-redistribution cascade (experimental, purely topological;
  validated in 2.9 against solved power flow — does not graduate; simplified to its honest core in 2.10
  with a `largest_component_fraction` severity metric; see Phase 5)

**Visualization** (2.5.0; `gravel[viz]`, pure Python, downstream of `to_geodataframe`)
- `plot_fragility` — static quantitative choropleth (colorblind-safe) for research artifacts
- `interactive_map` / `animate_failure` (lonboard WebGL) + self-contained `animate_failure_html` /
  `dashboard_html` for comprehension and public reach; real road-shape geometry from 2B

**Datasets & hazards** (2.6.0; `gravel.datasets`, `gravel[datasets]`)
- Queryable dataset catalog + info-pull (`list()` / `info(id)` / `summary()`); `DatasetInfo` in core
- Four hazard fetchers — NFHL flood, USGS ShakeMap, US Drought Monitor, FEMA NRI — each
  `fetch(...) → (GeoDataFrame, Provenance)` + `edge_probabilities(...) → np.ndarray` feeding `stochastic_fragility`

**Network substrates** (2.7.0; the engine is network-agnostic — roads were the first example)
- Five infrastructure networks as first-class fragility substrates, each `load(...) → (Graph, capacity)`:
  GridSFM & OPFData (power), CAIDA ITDK (internet), OpenFlights (air), GTFS (transit; incl. NYC/DC/Chicago presets)
- BTS T-100 airline capacity overlay (`t100`, key-join); catalog spans 12 datasets

### Current limitations

The Phase 1 interop keystone (2.3.0) closed the Python-surface gap that previously motivated the
roadmap. **Resolved in 2.3.0:** node-coordinate extraction (`Graph.node_coordinates`), COO
accessors (`Graph.to_coo` / `from_coo`), per-edge OSM metadata in Python
(`load_osm_graph_with_metadata` → `EdgeMetadata`), GeoJSON/JSONL/Parquet export bindings, and
NetworkX/GeoPandas adapters (`gravel.interop`).

The two limitations that shaped the roadmap are now resolved:

- **OSM capacity-relevant tags → modeled.** *Resolved in 2.4.0 (Phase 2A):* `estimate_capacity`
  derives per-edge PCE from `highway`/`lanes`, feeding capacity-aware betweenness (criticality +
  weighted importance).
- **Per-edge geometry.** *Resolved in 2.5.0 (Phase 2B):* degree-2 contraction preserves each edge's
  OSM polyline (`SimplificationConfig.emit_geometry`, default on), and
  `to_geodataframe(edge_geometry=…)` draws the real road shape instead of straight chords.

## Architecture Overview

### Sub-library structure

Gravel (v2.1+) organizes code into seven sub-libraries with a strict dependency DAG:

```
gravel-core      → (nothing)
gravel-ch        → gravel-core
gravel-simplify  → gravel-core, gravel-ch
gravel-fragility → gravel-core, gravel-ch, gravel-simplify
gravel-geo       → gravel-core, gravel-simplify
gravel-datasets  → gravel-core, gravel-simplify, gravel-geo
gravel-us        → gravel-geo, gravel-datasets
```

External dependencies are isolated:

| Dependency | Confined to | Optional |
|-----------|-------------|----------|
| libosmium | gravel-datasets | Yes (GRAVEL_USE_OSMIUM, AUTO by default since 2.2.2) |
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
Degree-2 contraction reduces 200K-node county graphs to ~14K nodes with zero loss of
junction-to-junction shortest-path information (isolated degree-2 cycles/lollipops, which carry no
such route, are dropped rather than contracted). All analysis runs on the simplified graph; results
map back to original node IDs via
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

The authoritative, per-release changelog lives in [`CHANGELOG.md`](../CHANGELOG.md) (Keep-a-Changelog,
SemVer) — this PRD does not duplicate it. The arc in brief:

**2.0** topological fragility core → **2.1** sub-library DAG + Dijkstra/IncrementalSSSP rewrite (~400×)
→ **2.2** published wheels / Windows / inter-region → **2.3** interop keystone + real parallelism →
**2.4** research depth (capacity → stochastic → cascade seed) → **2.5** visualization → **2.6** dataset
onboarding & catalog + hazard fetchers → **2.7** alternative network substrates + capacity overlays →
**2.8** whole-graph edge fragility → **2.9** cascade validation against solved power flow (verdict:
stays experimental — see Roadmap Phase 5).

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

### Phase 2A — Research depth (capacity → stochastic → cascade) ✅ shipped in 2.4.0

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

- ✅ **Persist edge polyline geometry (built for 2.5.0).** Degree-2 contraction records each collapsed
  chain's polyline (`EdgeGeometry`, `SimplificationConfig.emit_geometry`, default on); `to_geodataframe`
  draws real road shapes instead of straight chords. Type in `gravel-core` (pure data), populated by
  `gravel-simplify`. *(opt-out; auto-skipped without coordinates)*
- **Visualization** (`gravel.viz`, `gravel[viz]` extra, pure Python; downstream of `to_geodataframe`,
  never in the C++ core). Staged:
  - ✅ **Tier 0 (data bridge, shipped 2.4.0).** Results expose a per-edge failure trace
    (`edge_failure_round` from progressive greedy removal order; `edge_failure_frequency` from
    stochastic MC) and `failure_geoframe` returns a plot-ready `GeoDataFrame`. Zero new deps.
  - ✅ **Tier 1 + 2 (built for 2.5.0).** Two audiences, two modes (durable design principle):
    **static** = the researcher's *accurate* artifact (`plot_fragility` — quantitative choropleth,
    colorblind-safe sequential colormap, honest about uncertainty; matplotlib/geopandas); **dynamic**
    = comprehension + public reach (`interactive_map` + `animate_failure` on lonboard WebGL, plus a
    self-contained deck.gl `animate_failure_html`; texture as an ordinal encoding). Not aimed at B&W
    journal figures. Geo-viz draws real road shape via the 2B edge-geometry above (pass
    `edge_geometry` to `to_geodataframe`). Backend chosen by spike (lonboard over pydeck).

### Phase 3 — Dataset onboarding & catalog (2.6) ✅ shipped in 2.6.0

Serves the research-onboarding mission — get disaster/crisis researchers from install to a
hazard-overlaid fragility run without hand-wrangling ArcGIS feature services. Generalizes the existing
`hazard_edge_probabilities` machinery (2.4.0 already named wildfire/surge as targets) and adds a
first-class, queryable dataset catalog. Landed as a unified dataset-onboarding layer: a new seventh
linkable library `gravel-datasets` (relocating `osm_graph` from `gravel-geo` and `tiger_loader` from
`gravel-us` via `git mv`, history preserved; C++ symbols unchanged in the flat `gravel::` namespace),
the `gravel.datasets` Python package, four hazard fetchers, and back-compat deprecation shims.

- ✅ **Dataset catalog + info-pull.** A `DatasetInfo` POD in `gravel-core`
  (`include/gravel/core/dataset_info.h`): id, name, plus enums `DatasetKind` / `Domain` / `Geometry` /
  `Coverage` / `Access` (plain) and `Temporal` / `Feature` (combinable bitmasks), `source_url`,
  `field_docs_url`, license — pure data, no deps. `gravel::dataset_catalog()` (implemented in
  `gravel-datasets`, `src/datasets/catalog.cpp`) returns the 6 supported datasets, queryable from
  both the CLI and Python (`gravel.datasets.list()` / `.info(id)` / `.summary()` feature-matrix). The
  enums + `DatasetInfo` + `dataset_catalog` re-export at the top level.
  - **Design constraint (DAG):** the catalog *type* lives in core; population lives in the new
    `gravel-datasets` library (which sits above `geo`) and is aggregated at the binding layer — no
    core→geo/us dependency and no static-init-order registry across the separately-linkable
    libraries. `gravel-datasets` must not depend on `gravel-fragility` or `gravel-us`; the hazard
    point-in-polygon kernel (`edges_in_polygon` / `hazard_edge_probabilities`) stays in
    `gravel-fragility`.
  - Field definitions are **pointed to** (`field_docs_url`), never ingested — the source's data
    dictionary stays authoritative and cannot rot in a Gravel copy.
- ✅ **Hazard-source expansion (four sources).** NFHL (relocated from `gravel.hazards`) + USGS
  ShakeMap/ComCat (MMI contours, by event id + version), US Drought Monitor (D0–D4, by week), FEMA NRI
  (multi-hazard annualized baseline, `temporal=static`). All are polygon → reuse the shipped
  multi-zone `edges_in_polygon` kernel; each is a `gravel.datasets` submodule with a consistent
  `fetch(...) → (GeoDataFrame, Provenance)` and `edge_probabilities(graph, footprint, …) → np.ndarray`
  interface feeding `stochastic_fragility`, backed by a disclosed, sweepable severity→probability
  table (not authoritative rates; same DAG-clean input-array pattern as NFHL). Behind the
  `gravel[datasets]` extra (geopandas + shapely + pyproj). Deferred to 2.6.x: track-buffering hazards
  (HURDAT2 hurricane, SPC SVRGIS tornado) and MTBS raster burn-severity.
- ✅ **Fetch/resolve layer + lean provenance.** Each per-dataset `fetch(...)` returns data plus a
  `Provenance` stamp — `{dataset_id, endpoint, resolved_version, pulled_at}` — with `.to_dict` /
  `.to_json` (machine record) and `.summary()` (screen output). Version-aware (event-id for ShakeMap,
  week for Drought, vintage for NRI) and endpoint-overridable per the `GRAVEL_NFHL_ENDPOINT`
  precedent. Deliberately not full provenance handling; enough to cite what was pulled, from where,
  and when.
  - **BYO-GeoJSON is the stable contract**; auto-fetchers are best-effort convenience behind the
    `gravel[datasets]` extra, degrading to BYO on endpoint breakage.
  - **Integration-tier rule (self-serving for future datasets):** a dataset earns an auto-fetcher iff
    it has a stable version axis *and* a programmatic endpoint; otherwise it is catalog + field-docs
    pointer + BYO. (Applied in Phase 4: CAIDA's AUP gate → BYO-only; `inf-power` → bundled fixture.)
- ✅ **Per-dataset load submodules + deprecation shims.** `gravel.datasets.osm.load` /
  `.load_with_metadata` and `gravel.datasets.tiger.counties/states/cbsas/places/urban_areas` give the
  relocated loaders a consistent home. The prior top-level entry points
  (`gravel.load_osm_graph[_with_metadata]`, `gravel.load_tiger_*`) and the whole `gravel.hazards`
  module now forward through deprecation shims — still working, emitting `DeprecationWarning`, slated
  for removal in 3.0.

### Phase 4 — Alternative network substrates (2.7) ✅ shipped in 2.7.0

Acts on the fragility engine being **network-agnostic** — road networks were the first example, not
the boundary. Adds infrastructure networks beyond roads as first-class fragility substrates, each
carrying real capacity so the Phase 5 cascade has ground truth to consume and validate against.
Landed as five C++ parsers in `gravel-datasets` and matching `gravel.datasets` load submodules,
each returning a `NetworkGraph` (graph + optional CSR-aligned per-edge capacity), plus an airline
capacity overlay; the catalog grows to 12.

- ✅ **Shared network type.** `include/gravel/datasets/network_graph.h` —
  `struct NetworkGraph { std::unique_ptr<ArrayGraph> graph; std::vector<double> capacity; }`: an
  infrastructure-network graph plus an optional per-edge capacity array (empty when the source has
  none). Bound to Python so each loader returns `(Graph, capacity)` with capacity as a per-edge
  numpy array.
- ✅ **Five substrates** (each a `gravel.datasets` submodule with `load(...) → (Graph, capacity)`;
  fetchers follow the Phase 3 integration-tier rule):
  - *Power* — **GridSFM** (`load_gridsfm_network`; capacity = thermal limits in MVA, node coords).
    `gravel.datasets.gridsfm.fetch(dest, name, hour)` pulls a case JSON from the public Hugging Face
    dataset `microsoft/GridSFM_US_power_grid` (prefers `huggingface_hub`, stdlib fallback). MIT.
  - *Power* — **OPFData** (`load_opfdata_graph`; PGLib-OPF synthetic AC-OPF, capacity in MVA, **no
    coords**) as the degradation-math validation companion. `opfdata.fetch(dest, case_name, group,
    n_minus_one)` downloads + extracts a tar group from the public gridopt-dataset GCS bucket.
    CC BY 4.0.
  - *Internet* — **CAIDA ITDK** (`load_caida_itdk(ItdkConfig)`; router topology, no capacity, coords
    via optional `nodes_geo_path`). BYO only — CAIDA Acceptable Use Agreement, no fetcher.
  - *Air* — **OpenFlights** (`load_openflights_network`; airport nodes + O-D routes, node coords, no
    native capacity). `openflights.fetch(dest)` downloads `airports.dat` + `routes.dat`;
    `load(..., with_codes=True)` also returns the node→IATA code list. ODbL.
  - *Transit* — **GTFS** via Transitland (`load_gtfs_network(GtfsConfig)`; fixed routes, node coords
    + schedule-derived persons/hour capacity — the *scheduled* throughput proxy, **not** live
    routing; see Non-Goals). `gtfs.fetch(dest, onestop_id, apikey|feed_url)` downloads + extracts a
    Transitland feed (needs a free Transitland API key via `apikey=` / `GRAVEL_TRANSITLAND_APIKEY`,
    or a keyless direct `feed_url`). Per-feed license.
  - Config structs `ItdkConfig` (`nodes_path` / `links_path` / `nodes_geo_path` /
    `expansion` [CLIQUE|STAR] / `drop_placeholder_nodes`) and `GtfsConfig` (`dir` / `capacity_model`
    [`GtfsCapacityModel`, per-mode] / `window_hours`) are bound to Python. Network loaders need only
    numpy; fetchers use stdlib `urllib` (GridSFM optionally `huggingface_hub`).
- ✅ **Capacity / attribute overlay (`DatasetKind = ATTRIBUTE_OVERLAY`).** `gravel.datasets.t100`
  (BTS T-100 Segment) — the non-geometric sibling of hazard overlays (key-join, not
  point-in-polygon): `t100.load(csv, value_field='SEATS') → {(origin, dest): value}` and
  `t100.edge_capacity(graph, node_iata, table) → per-edge capacity array` for an OpenFlights graph,
  key-joined on the ordered IATA pair. OpenFlights + T-100 = topology + geography + real capacity,
  the airline analog of what GridSFM provides natively. BYO CSV from BTS TranStats (public domain).
- ✅ **Catalog grows to 12.** The 2.6 six (osm / tiger / nfhl / shakemap / usdm / nri) plus
  gridsfm / opfdata / caida / openflights / gtfs / t100. Network geometry is `POINT` (or `NONE` for
  opfdata, which has no coords); access is `FETCHER` for gridsfm / opfdata / openflights / gtfs and
  BYO for caida + t100.
- ✅ **Scope boundary.** Substrates are analyzed *independently*; interdependent multi-layer coupling
  remains a non-goal (see Non-Goals). No new fragility paradigm — the existing topological and
  capacity-weighted analyses run on the new substrates through the same interfaces, capacity feeding
  the existing stochastic/cascade input array (consistent with DD-6).

### Phase 5 — Cascade validation (conducted; verdict: does **not** graduate)

The plan for 3.0 was to mature `cascade_fragility` (Motter–Lai — load = betweenness, capacity =
(1+α)×initial load, redistribute-and-iterate to a fixed point) from *experimental* to *supported* by
wiring real per-edge capacity and validating predictions against ground-truth contingencies. **We ran
that validation. The honest outcome is that the cascade does not graduate — it stays experimental —
and the gap is now quantified rather than assumed.**

**The units wall (audit).** "Wiring real capacity" turns out to be dimensionally incoherent, not a
plumbing task. The model's load is edge **betweenness** (unitless path-counts, ~10⁴); real capacity is
a **thermal limit** (MVA, ~10²). Making them comparable requires load in MVA — a power-flow solve
governed by Kirchhoff's laws — which is exactly the flow modeling DD-6 excludes. And any topological
redistribution rule (betweenness recompute, proportional spread) is not Kirchhoff: real power splits
across all paths by admittance, and can *rise* on a line when a parallel line is added (Braess). So
capacity cannot be dropped into a betweenness cascade and yield physics.

**What we measured (the necessary condition).** For the cascade to be a physical model, its "load"
must at least track real load. Tested directly on DeepMind **OPFData** solved AC-OPF states, which ship
per branch both the thermal limit (`rate_a`, MVA) and the solved flow |S| = √(pf²+qf²). Per solved
state: Spearman rank correlation of Gravel edge betweenness vs real |S|, and the overlap of the 30%
most-between lines with the 30% most-loaded (random ≈ 0.30). Reproduce with
[`scripts/validate_cascade_powerflow.py`](../scripts/validate_cascade_powerflow.py):

| case | branches | ρ(unit, \|S\|) | ρ(br_x, \|S\|) | ρ(br_x, util) | top-30% overlap |
|---|---:|---:|---:|---:|---:|
| IEEE-14  |  20 | −0.17 | +0.07 | −0.18 | 0.17 |
| IEEE-118 | 186 | +0.22 | +0.35 | +0.17 | 0.40 |
| GOC-500  | 728 | −0.01 | +0.03 | −0.08 | ≈0.30 |

Best case ρ ≈ 0.35 (r² ≤ 0.12); typically ≈ 0, sometimes negative; the critical-line overlap is at or
below chance. This reproduces Hines, Cotilla-Sanchez & Blumsack (*Chaos* 2010, "Do topological models
provide good information about electricity infrastructure vulnerability?") on modern solved data.
Impedance-weighting helps marginally but nowhere near usable. Tellingly, the proxy that *would* track
the physics is current-flow / Laplacian betweenness — essentially the DC power-flow solve DD-6
excludes.

**Verdict.** `cascade_fragility` remains **experimental**. The necessary condition fails across three
standard systems, so the sufficient condition (predicting real contingency propagation) cannot hold.
Kept honest in the API and docs: the cascade is a topological covariate, clearly labeled. A true
graduation would require adding a power-flow (DC/Laplacian) model — a deliberate DD-6 reversal and a
scope decision for the maintainer, not a default.

**2.10 follow-through.** Acting on this verdict, 2.10 simplified the model to its honest core: the
`PCE_WEIGHTED` capacity source and its `edge_pce` input were removed — they reweighted the *tolerance*,
not the *load*, dressing a topological model in capacity-looking inputs without making it physical — and
a purely-topological `largest_component_fraction` severity metric (how connected the surviving graph
stays) was added. The cascade is now unambiguously one thing: classic betweenness-tolerance Motter–Lai
as a structural stress test.

**Recommended next step for a real N-1 harness.** OPFData's "N-1" re-solves a full AC-OPF with
generators *re-dispatched* (and drops a generator half the time), and GridSFM's "N-1" is
load/gen/derate perturbation, not line outage — so neither is a clean fixed-dispatch line-outage table.
The purpose-built dataset is **PFΔ** (`pfdelta/pfdelta`, arXiv 2510.22048): 859,800 solved flows in
explicit n / n-1 / n-2 folders with per-branch `edge_limits` + solved flows, so "which lines overload
after line k is removed" is directly computable as ground truth. Left as the next validation, and only
worth doing once a physical redistribution model exists to test against it.

- **Boundary (unchanged; stays inside DD-6):** the cascade is the *complex-networks* load-redistribution
  model, **not** traffic-assignment/user-equilibrium or power-flow physics. Domain-specific flow physics
  (DC/AC power flow, transit headway dynamics) stay out of the core by design. The demand-driven
  congestion-cascade direction (probabilistic rerouting under capacity, with real slow-down delay) is
  specified as a separate consumer layer in [`FLOW_LAYER.md`](FLOW_LAYER.md), explicitly outside DD-6.

### Hardening & operational (surfaced during the 2.3.0 release)

The 2.3.0 wheel build — blocked for hours by a gitlab.com outage that cascaded through Eigen 5.0 and
cross-arch libomp — exposed that build-time dependency clones are a release liability. Follow-ups:

- **Finish build network-independence.** Eigen is now vendored (`third_party/eigen`), but Spectra,
  nlohmann/json, pybind11, and Catch2 still `FetchContent`-clone from github at build time. Vendor or
  checksum-pin the rest so a release can never be blocked by an upstream host outage.
- **conda-forge is stale (targets 2.2.x) — currently not a supported channel.** The README and docs no
  longer advertise it. Either revive the feedstock and bump it to the current PyPI release (2.7.0), or
  formally drop conda-forge; until then, PyPI is the only distribution.
- ✅ **Refreshed headline performance numbers** (2026-07-01, `bench/baselines/routing_performance.md`).
  Re-benchmarked Release + OpenMP on real counties: distance matrix and `route_fragility` are ~5×
  faster on macOS post-2.3.0 (the April numbers were effectively serial), confirmed by a 1→10-thread
  scaling curve on the same machine. Single-threaded ops unchanged. The `perf_baseline.json`
  Google-Benchmark regression gate is refreshed separately via `gravel_perf`.
- ~~Extend deterministic mode to `kirchhoff_index` / `natural_connectivity`~~ — **not needed.** Audit
  found both are serial (no OpenMP) and seeded, hence already reproducible; there is no unpinned
  parallel reduction to fix. (If they are ever parallelized, they'll need the H4 treatment then.)

### Longer-horizon / research-track

- **Customizable Contraction Hierarchies (CCH).** Metric-independent structure (nested-dissection
  order + triangulated shortcuts) built once, then fast weight *customization* — an edge removal
  becomes setting its weight to ∞ and re-customizing the affected triangles, exact and non-degrading.
  The principled route to editable/dynamic networks and cheap repeated re-routing, and the only clean
  way to relax the "static-topology CH" non-goal below. Preferred over hand-rolled incremental CH
  updates (intricate, quality-eroding); today `BlockedCHQuery` already covers fixed-topology edge
  blocking without a rebuild, so CCH is warranted only if a true editable-network use case emerges.
- **Flow / assignment layer (external consumer).** Demand-driven congestion cascades — Stochastic User
  Equilibrium (BPR slow-down delay + logit rerouting, solved by MSA) as a `gravel.flow` layer *on top
  of* the core, importing it one-directionally and leaving DD-6 intact. The honest home for "how much
  slower does the region get when this fails" (ΔTSTT), which the topological cascade cannot answer.
  **Shipped in 3.0.0 as `gravel.flow`, public but experimental:** the solver is exact (Sioux Falls) and
  recovers a known θ synthetically, but real θ does not identify from open corridor data (boundary /
  around-corridor rerouting is invisible at corridor scale). See [`FLOW_LAYER.md`](FLOW_LAYER.md) for the
  full calibration study and the regional-ODME path to graduation.
- Temporal fragility (degradation over construction schedules)
- International road-network support (non-TIGER boundaries)
- ML-assisted edge-importance ranking

## Non-Goals

- Gravel is **not a turn-by-turn navigation library.** Use OSRM or GraphHopper for that.
- Gravel is **not a transit *planner*.** Use OpenTripPlanner for GTFS trip planning and
  live/multi-modal routing. This bounds the *routing* use case, not the *substrate*: the fragility
  engine is network-agnostic (road networks were merely the first example), so a GTFS-derived transit
  network — fixed routes with average historic capacity — is a legitimate **fragility** target
  (Phase 4). What is out of scope is live routing on it, not modeling its network fragility.
- Gravel's CH is **static topology.** All CH operations assume a fixed edge set; the hierarchy is
  built once and is not incrementally mutated. (Fragility analysis still removes edges — by
  recomputation on the degraded graph, not by editing the CH.)
- Gravel is **not a traffic-assignment / user-equilibrium engine.** Full flow modeling (BPR cost
  functions plus an origin-destination demand matrix solved to equilibrium) is out of scope *for the
  core*: it requires data Gravel cannot calibrate and would trade a defensible topological covariate for
  a parameter-laden one. The complex-networks cascade model in Phase 2A is the supported topological
  alternative. Where a genuine equilibrium answer *is* wanted — congestion as slow-down rather than
  blockage, some travelers taking a longer path to avoid a jam — it belongs in a **separate consumer
  layer** that imports Gravel as a routing/capacity/sampling engine without changing the core. That
  layer is specified in [`FLOW_LAYER.md`](FLOW_LAYER.md) (Stochastic User Equilibrium; explicitly
  outside DD-6, so the core's topological stance is unaffected).
- Gravel does **not yet** model interdependent multi-layer infrastructure as a first-class graph. When
  needed, coupled failures are expressed as an externally-built edge set fed to `scenario_fragility`;
  a true multi-layer core is reserved for a research need that demands interdependency covariates.
  (Phase 4's added substrates — power, transit, air, internet — are analyzed one network at a time;
  that is substrate breadth, not multi-layer coupling, which stays reserved.)
