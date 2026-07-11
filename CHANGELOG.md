# Changelog

All notable changes to Gravel are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

**Flow layer (3.0.0, in progress) — Phases F1–F3 (synthetic).** First code for the demand-driven
traffic-assignment consumer layer specified in `docs/FLOW_LAYER.md`.

### Added
- **`gravel.flow` (F1)** — deterministic User Equilibrium via Frank-Wolfe + BPR volume-delay:
  `flow.assign(graph, capacity, demand, FlowConfig()) → FlowResult` (per-edge equilibrium flows/times,
  TSTT, relative gap), the `bpr(...)` cost function, and `load_tntp(...)` for standard benchmark
  networks. Solves by rebuilding the graph with congested costs each iteration and loading all-or-nothing
  via the one-to-many `dijkstra` (DD-F2) — the CH is deliberately not used. Needs only numpy; the `[sue]`
  extra is reserved for the θ-calibration/realtime-diversion tooling (Phase F3). **Tier-1 validated:**
  reproduces the published Sioux Falls UE solution (MAPE ≈ 0.04%, correlation ≈ 1.0000).
- **`flow.flow_fragility(graph, capacity, demand, scenario_edges, config)` (F2)** — the payoff: the
  region-wide delay cost of a failure. Returns **ΔTSTT** (increase in total system travel time after
  demand re-equilibrates around the removed edges) and its fraction, plus **stranded demand** (trips whose
  O-D pair the closure disconnects). Read together, they separate reroute cost from disconnection.
- **Stochastic (logit) UE + the θ-calibration harness (F3, synthetic)** — `FlowConfig(theta=...)` now
  solves stochastic UE by Dial STOCH loading + MSA (sharpens to the deterministic UE as θ→∞; spreads flow
  as θ→0). `flow.diversion_flows(...)` gives model-predicted post-closure flows, and
  `flow.calibrate_theta(graph, capacity, demand, observations, ...)` fits θ to observed data, returning
  the best-fit θ and the full error-vs-θ curve (`ClosureObservation`, `CalibrationResult`). The **primary
  observable is speed** — the closure-induced slowdown ratio `t/t0 = v_freeflow/v_observed`
  (`observable="congestion"`), which matches Gravel's travel-time output directly, needs only broad speed
  data, and avoids the ill-posed speed→volume inversion; volume (`observable="flow"`) is the secondary
  count-based cross-check. Validated on synthetic data both ways (recovers a known θ from model-generated
  speed *and* volume observations). This is the 3.0.0 graduation gate (DD-F5). Framed per **DD-F6**: the
  layer is a *general* statistical-redistribution model (default for any network), with domain-specific
  calibrators on top — roads first. No version bump — 3.0.0 tags only once it validates on real data.

## [2.10.0] — 2026-07-10

**Cascade, simplified to its honest core.** Acting on the 2.9 verdict — the Motter–Lai
`cascade_fragility` is a *topological* model, not a physical one — this release removes the part that
cosplayed physics and adds a purely-topological severity metric. The cascade is now unambiguously one
thing: a betweenness-tolerance overload stress test.

> **Version note (maintainer's call at tag time):** the removal below is a breaking change, but only to
> the **experimental** cascade surface, which the README/REFERENCE explicitly exclude from the stability
> contract. Under that scope this is a **minor** bump (2.10.0), consistent with the 2.9 "minor-not-3.0"
> reasoning. Retag as a major if you'd rather treat any public-symbol removal as breaking.

### Changed (breaking — experimental cascade surface only)
- **Removed `CascadeCapacity` / `PCE_WEIGHTED` and `CascadeFragilityConfig.capacity_source` /
  `.edge_pce`.** The PCE-weighted capacity source reweighted the *tolerance*, not the *load*, so it
  dressed a topological model in capacity-looking inputs without making it physical — and 2.9 established
  the underlying load proxy is too weak to rescue. `cascade_fragility` is now classic
  betweenness-tolerance Motter–Lai only.

### Added
- **`CascadeFragilityResult.largest_component_fraction`** — nodes in the largest surviving connected
  component ÷ node_count, on the undirected graph of non-failed edges (1.0 = still fully connected,
  small = shattered). A purely topological severity signal, more meaningful than the raw failed-edge
  count. Covered on real data: a gated OSM test confirms Swain County NC is majority-bridge and that
  removing its single worst road fragments the network (lcf well below 1.0), where a bridge-free grid
  stays pinned at 1.0.
- **One-to-many Dijkstra bound to Python** — `gravel.dijkstra(graph, source) → DijkstraResult` with
  numpy `distances` and `predecessors`, plus `gravel.reconstruct_path(result, s, t)`. The C++
  `dijkstra()` was previously unbound (only single-pair `dijkstra_pair` was exposed). This is the
  shortest-path-*tree* primitive the future flow layer relies on (`docs/FLOW_LAYER.md`), and it is
  useful on its own for one-origin-to-many-destinations queries under changing weights.

### Documented
- **`docs/FLOW_LAYER.md`** — a design spec for a separate, demand-driven flow/assignment *consumer*
  layer (Stochastic User Equilibrium: BPR congestion delay + logit rerouting, solved by MSA), living on
  top of Gravel and explicitly outside DD-6. This is the honest home for the "congestion cascade"
  questions the topological cascade cannot answer — congestion as slow-down, not blockage; some drivers
  taking a longer path to avoid a jam. Scoped for **3.0.0**, with a two-tier validation plan: solver
  correctness on Sioux Falls, then behavioral validation by **measuring real diversion rates from
  realtime data** (PeMS / NPMRDS / 511) to calibrate the logit dispersion θ — the 2.9 "measure it, don't
  assume it" discipline applied to route choice. **Spec only; no implementation in this release.**

## [2.9.0] — 2026-07-09

**Cascade validation — the honest verdict.** The 3.0 plan was to graduate the experimental Motter–Lai
`cascade_fragility` to a validated physical model by wiring real per-edge capacity and scoring against
ground-truth contingencies. We ran the validation; the model **does not graduate**. This release ships
the validation (so the finding is reproducible) and documents the boundary, rather than shipping a
graduation that the evidence does not support. No API changes; no breaking changes.

> **Version note (maintainer's call at tag time):** this is a *non-graduation* — additive (a study
> script + docs), no new validated feature and no breaking change, so SemVer makes it a **minor** bump
> (2.9.0), not the 3.0.0 major the roadmap pencilled in. 3.0.0 should be reserved for an actual cascade
> graduation, which would require adding a power-flow model (a deliberate DD-6 decision). Retag if you
> disagree.

### Added
- **`scripts/validate_cascade_powerflow.py`** — validates the cascade's core assumption against
  DeepMind OPFData solved AC-OPF states: for each solved state it correlates Gravel edge betweenness
  (the cascade's "load") with the real per-branch apparent power flow |S| = √(pf²+qf²), and measures
  whether the most-between lines are the most-loaded. Finding: betweenness barely tracks power flow
  (Spearman ρ ≲ 0.35, often ≈ 0, sometimes negative; critical-line overlap at/below chance) across
  IEEE-14/118 and GOC-500 — reproducing Hines et al. (*Chaos* 2010) on modern data.

### Documented
- **`cascade_fragility` is a topological model, not a physical one** — recorded in the header, in
  `REFERENCE.md` §33.4, and in `docs/PRD.md` Phase 5 (methodology, results table, verdict). It stays
  **experimental**. Real per-edge robustness (e.g. thermal headroom) can inform the tolerance via
  `PCE_WEIGHTED`, but that reweights the tolerance, not the load, so it cannot make the cascade
  physical. A faithful grid model would need a power-flow solve, out of scope by design (DD-6). The
  purpose-built dataset for a true single-line-outage N-1 harness is **PFΔ** (arXiv 2510.22048),
  noted as the recommended next step.

## [2.8.0] — 2026-07-09

**Whole-graph edge fragility.** A standard caller that ranks *every* edge in a network by criticality,
not just the edges along one route — the piece needed to color a whole transmission grid (or road
network) by how much its failure would hurt. Additive on top of 2.7 — no existing symbol or signature
changes.

### Added
- **`edge_fragility(ch, shortcut_index, graph, config)` (`gravel-fragility`,
  `gravel/fragility/edge_fragility.h`).** Generalizes `route_fragility` from a single s-t path to
  every edge: for each edge, the **path-inflation ratio** (shortest endpoint distance with the edge
  removed ÷ its normal distance; ∞ for a bridge) and, for bridges, the **stranded count** (nodes
  disconnected). Returns `EdgeFragilityResult { fragility_ratio, replacement_distance, stranded_count,
  is_bridge }` — four arrays in CSR edge order aligned with `Graph.to_coo()`. `EdgeFragilityConfig`
  toggles the two measures (`compute_ratio` skips all CH queries; `compute_stranded` drops cut sizes;
  bridge classification is always computed). OpenMP-parallelized over edges; one `BlockedCHQuery` per
  thread. Bound to Python as `gravel.edge_fragility`, `gravel.EdgeFragilityConfig`,
  `gravel.EdgeFragilityResult` with zero-copy NumPy views.
- **`bridge_edge_info(graph)` (`gravel-simplify`, `gravel/simplify/bridges.h`).** Per-CSR-edge bridge
  flags **plus cut sizes** (how many nodes fall to the smaller side when a bridge is removed) in one
  iterative-Tarjan pass carrying DFS subtree sizes. Returns `EdgeBridgeInfo { is_bridge, cut_size }`
  in CSR edge order. Vector-indexed throughout (sort-grouped edges + counting-sort adjacency — no hash
  maps, no per-node vectors): ~10s on a 28M-edge OSM road network, ~8× faster than a hash-map design
  and bit-identical. Bound to Python as `gravel.bridge_edge_info` / `gravel.EdgeBridgeInfo`. Parallel
  edges (a double-circuit line) are redundancy, never bridges.

### Changed
- **Example `examples/python/09_power_grid_multihazard.py`** now colors each transmission branch by its
  `edge_fragility` path-inflation ratio (with genuine bridges as the top tier) over a faint FEMA NRI
  multi-hazard county tint, replacing the earlier betweenness-centrality coloring. Default region
  widened from North Carolina to the Eastern US (Atlantic seaboard + PA/VT/WV/DC).

## [2.7.0] — 2026-07-08

**Phase 4 — alternative network substrates.** `gravel.datasets` grows past roads: five
infrastructure-network parsers (power grid, internet router topology, air, transit) plus an airline
capacity overlay, each returning a `Graph` and an optional per-edge capacity array that
fragility/cascade analyses consume exactly like a road graph. Additive on top of 2.6 — no existing
symbol or signature changes.

### Added
- **`NetworkGraph` type (`gravel-datasets`, `gravel/datasets/network_graph.h`).**
  `struct NetworkGraph { std::unique_ptr<ArrayGraph> graph; std::vector<double> capacity; }` — an
  infrastructure-network graph plus an optional CSR-aligned per-edge capacity (empty when the source
  carries none).
- **Five infrastructure-network C++ parsers, each returning `NetworkGraph`.**
  `load_gridsfm_network(path)`, `load_opfdata_graph(path)`,
  `load_openflights_network(airports, routes, collapse_parallel, drop_codeshare, node_iata*)`,
  `load_caida_itdk(ItdkConfig)`, and `load_gtfs_network(GtfsConfig)`. Bound to Python; each returns
  `(Graph, capacity)`, with `capacity` a per-edge NumPy array. The config structs `ItdkConfig`
  (`nodes_path` / `links_path` / `nodes_geo_path` / `expansion` [`CLIQUE` | `STAR`] /
  `drop_placeholder_nodes`) and `GtfsConfig` (`dir` / `capacity_model` [`GtfsCapacityModel`, per-mode]
  / `window_hours`) are bound too.
- **Five `gravel.datasets` submodules, each `load(...) -> (Graph, capacity)`.**
  - **`gridsfm`** — US power grid; `capacity` = thermal limits (MVA); node coords.
    `fetch(dest, name, hour)` pulls a case JSON from the Hugging Face dataset
    `microsoft/GridSFM_US_power_grid` (public; prefers `huggingface_hub`, stdlib fallback). MIT.
  - **`opfdata`** — synthetic AC-OPF power; `capacity` in MVA; **no coords**.
    `fetch(dest, case_name, group, n_minus_one)` downloads + extracts a tar group from the public
    `gridopt-dataset` GCS bucket. CC BY 4.0.
  - **`caida`** — internet router topology; no capacity; coords via optional `nodes_geo_path`.
    BYO only (CAIDA Acceptable Use Agreement — no fetcher).
  - **`openflights`** — air network; node coords; no native capacity. `fetch(dest)` downloads
    `airports.dat` + `routes.dat`; `load(..., with_codes=True)` also returns the node→IATA code list.
    ODbL.
  - **`gtfs`** — transit; node coords + schedule-derived persons/hour capacity.
    `fetch(dest, onestop_id, apikey|feed_url)` downloads + extracts a Transitland feed (needs a free
    Transitland API key via `apikey=` or `GRAVEL_TRANSITLAND_APIKEY`, or a keyless direct `feed_url`).
    Per-feed license.
  The network loaders need only NumPy; the fetchers use stdlib `urllib` (`gridsfm` optionally
  `huggingface_hub`; `gtfs` needs a Transitland key).
- **GTFS major-city presets (`gravel.datasets.gtfs.fetch_city` / `cities`).** Pull a whole city's
  transit feed by name: `"nyc"` (MTA subway), `"chicago"` (CTA), `"bart"` (SF Bay Area), and `"boston"`
  (MBTA) are keyless; `"dc"` (WMATA Metrorail) needs a free `api_key` (`apikey=` or `GRAVEL_WMATA_APIKEY`).
  Case-insensitive with aliases. `gtfs.fetch` also gained an `extra_headers=` argument for authenticated
  agency ZIP endpoints.
- **BTS T-100 airline capacity overlay (`gravel.datasets.t100`, `DatasetKind.ATTRIBUTE_OVERLAY`).**
  `t100.load(csv, value_field='SEATS') -> {(origin, dest): value}` and
  `t100.edge_capacity(graph, node_iata, table) -> np.ndarray` build a per-edge capacity array for an
  OpenFlights graph, key-joined on the ordered IATA pair. BYO CSV from BTS TranStats (public domain).

### Changed
- **The dataset catalog now lists 12 datasets** — 2.6's `osm` / `tiger` / `nfhl` / `shakemap` /
  `usdm` / `nri` plus the new `gridsfm` / `opfdata` / `caida` / `openflights` / `gtfs` / `t100`.
  Network geometry is `POINT` (`NONE` for `opfdata`, which has no coords); `access` is `FETCHER` for
  `gridsfm` / `opfdata` / `openflights` / `gtfs` and `BYO` for `caida` and `t100`.

### Fixed
- **Build:** renamed the removed scikit-build-core `cmake.verbose` key to `build.verbose`, so
  source and wheel builds succeed under scikit-build-core ≥ 0.10 (the stale key aborted
  `pip install` / wheel builds with a hard `Getting requirements to build editable` error).

## [2.6.0] — 2026-07-08

**Unified dataset onboarding.** A single `gravel.datasets` package now answers "what can I load, and
how?" and does the loading — road networks, administrative boundaries, and four hazard overlays behind
one consistent interface, with a self-describing catalog and a citable provenance stamp on every remote
pull. A file/library relocation, not an API break: the existing C++ symbols and their signatures are
unchanged, and the old Python entry points still work (with a deprecation warning) through 3.0.

### Added
- **`gravel-datasets` (7th linkable library).** A new dataset-onboarding layer above `gravel-geo`:
  depends on `gravel-core`, `gravel-simplify`, and (public) `gravel-geo`, plus optional libosmium; must
  not depend on `gravel-fragility` or `gravel-us`. The relocated OSM/TIGER loaders and the new dataset
  catalog live here. The sub-library DAG is now **seven** libraries, not six.
- **`gravel.datasets` info-pull API.** `gravel.datasets.list() -> list[Dataset]`,
  `.info(id) -> Dataset` (`KeyError` on an unknown id), and `.summary()` (prints and returns a
  feature-matrix string) let a user discover the supported datasets before loading anything. A `Dataset`
  wrapper exposes `id`, `name`, `kind`, `domain`, `geometry`, `temporal`, `coverage`, `features`,
  `versioning`, `source_url`, `field_docs_url`, `license`, `access`, plus `available`, `feature_names()`,
  `temporal_names()`, `has_feature(feature)`, `to_dict()`, and `to_json()`.
- **`DatasetInfo` catalog + enums (`gravel-core`, `gravel/core/dataset_info.h`).** A `DatasetInfo` POD
  with enums `DatasetKind`, `Domain`, `Geometry`, `Coverage`, `Access` (plain) and `Temporal`, `Feature`
  (combinable bitmasks). `dataset_catalog()` (implemented in `gravel-datasets`, `src/datasets/catalog.cpp`)
  returns the six supported datasets. `DatasetInfo`, `dataset_catalog`, and the enums are re-exported at
  the Python top level (`gravel.DatasetKind` / `Domain` / `Geometry` / `Temporal` / `Coverage` / `Access`
  / `Feature` / `DatasetInfo` / `dataset_catalog`).
- **Four hazard-overlay fetchers (`gravel.datasets.{nfhl, shakemap, usdm, nri}`).** Each exposes a
  consistent `fetch(...) -> (GeoDataFrame, Provenance)` and
  `edge_probabilities(graph, footprint, …) -> np.ndarray` (feeds `gravel.stochastic_fragility`), backed
  by a disclosed, sweepable severity → probability table (illustrative, **not** authoritative rates):
  - **`nfhl`** — FEMA National Flood Hazard Layer (relocated from `gravel.hazards`).
  - **`shakemap`** — USGS ShakeMap via ComCat, by event id + version.
  - **`usdm`** — US Drought Monitor, by week.
  - **`nri`** — FEMA National Risk Index (annualized baseline).
  These need the new **`gravel[datasets]`** extra (geopandas + shapely + pyproj).
- **`Provenance` stamp.** `fetch()`'s second return value: a lean, citable
  `{dataset_id, endpoint, resolved_version, pulled_at}` with `to_dict()`, `to_json()`, and `summary()`.
  Deliberately a citation stamp, not full lineage.
- **OSM / TIGER dataset submodules.** `gravel.datasets.osm.load(pbf_path, speed_profile=None)` and
  `.load_with_metadata(pbf_path, speed_profile=None, bidirectional=True)`;
  `gravel.datasets.tiger.counties` / `states` / `cbsas` / `places` / `urban_areas(geojson_path)` — one
  consistent per-dataset interface alongside the hazard overlays.
- **`gravel[datasets]` pip extra.** Pulls in geopandas + shapely + pyproj for the hazard fetchers.

### Changed
- **`osm_graph` and `tiger_loader` relocated into `gravel-datasets`.** Moved (with git history) from
  `gravel-geo` and `gravel-us` respectively, alongside the new dataset catalog. The public C++ and Python
  symbols are **unchanged** — `gravel::load_osm_graph`, `gravel::load_tiger_counties`, etc. keep their
  flat `gravel::` namespace and signatures; this is a file/library move, not an API break. `gravel-us`
  now depends on `gravel-datasets` (its county/CBSA-assignment headers use the relocated `tiger_loader`).
- **The sub-library DAG is now seven libraries.** `gravel-datasets` sits above `gravel-geo`; the
  "six libraries" invariant becomes seven. The hazard point-in-polygon kernel
  (`edges_in_polygon` / `hazard_edge_probabilities`) did **not** move — it stays in `gravel-fragility`.

### Deprecated
- **`gravel.hazards.*`** — now a shim forwarding to `gravel.datasets` (`fetch_nfhl_flood_zones` →
  `datasets.nfhl.fetch()[0]`; `flood_edge_probabilities` → `datasets.nfhl.edge_probabilities`;
  `hazard_edge_probabilities` → `datasets._hazard.hazard_edge_probabilities`; `NFHL_*` constants +
  `nfhl_zone_color` → `datasets.nfhl.*`). Still works, emits `DeprecationWarning`, **removed in 3.0**.
- **`gravel.load_osm_graph` / `gravel.load_osm_graph_with_metadata`** — use
  `gravel.datasets.osm.load` / `load_with_metadata`. Deprecated, **removed in 3.0**.
- **`gravel.load_tiger_counties` / `states` / `cbsas` / `places` / `urban_areas`** — use
  `gravel.datasets.tiger.*`. Deprecated, **removed in 3.0**.

## [2.5.0] — 2026-07-03

**Phase 2B — real edge geometry.** Foundation for faithful maps: a simplified graph can now be drawn
along the true road shape instead of straight chords. OSM loads one edge per way segment, so full
geometry already exists as degree-2 node chains; previously it was lost when those chains were
contracted. Degree-2 contraction now records the collapsed polyline by default. Additive — the new
field is populated automatically; existing routing/fragility behavior and the public ABI are unchanged.

### Added
- **`EdgeGeometry`** (`gravel-core`, `gravel/core/edge_geometry.h`). Ragged-CSR per-edge polyline
  (`offsets` + `points`), index-aligned to a graph's edge order, with `points_for(edge)`,
  `edge_count()`, and `empty()`. Pure data — populated by simplification, consumed by the geo/Python
  layers, no cross-DAG dependency.
- **`SimplificationConfig.emit_geometry`** (**default `true`**). `simplify_graph` / `contract_degree2`
  record each edge's coordinate chain — the real shape for merged degree-2 chains, a 2-point segment
  for kept edges — in `SimplificationResult.edge_geometry`. Set `false` to skip it. Automatically
  skipped when the graph has no coordinates. Honored for the filter + degree-2 pipeline; left empty if
  CH-level pruning also runs (it would misalign the edge set).
- **`interop.to_geodataframe(..., edge_geometry=...)`.** Given an `EdgeGeometry`, each edge is drawn as
  its true `LineString`; without it, the previous straight-segment behavior is unchanged.
- **Python exports.** `simplify_graph`, `SimplificationConfig`, `SimplificationResult`, and
  `EdgeGeometry` are now re-exported at the top level (`gravel.simplify_graph`, …).
- **Animated failure playback (`gravel.viz` Tier 2).** `animate_failure(graph, progressive_result, …)`
  returns a Play/slider ipywidgets widget over a lonboard map that scrubs the progressive removal
  order (see "Three-state failure coloring" below for the per-round encoding). Only the color array
  updates per frame (data sent once via GeoArrow), so it stays smooth at scale. Requires a greedy
  `ProgressiveFragilityResult`; notebook-interactive (ipywidgets ships with lonboard).
- **Self-contained animated HTML (`gravel.viz` Tier 2).** `animate_failure_html(graph, result, path, …)`
  writes a standalone HTML file that plays/scrubs the removal sequence with deck.gl entirely
  client-side — no kernel or server. Geometry is embedded once; each frame only re-evaluates the color
  accessor (`updateTriggers` keyed to the round). Supports `edge_geometry` and `hazard`; needs only
  geopandas. Verified end-to-end in a headless browser (render + scrub + no console errors).
- **Interactive fragility maps (`gravel.viz` Tier 2).** `interactive_map(graph, result, …)` returns a
  lonboard (WebGL) `Map` that renders the per-edge failure trace on a pan/zoom basemap, scales to
  county-size networks via GeoArrow transport, and exports to standalone HTML (`m.to_html(...)`) for
  sharing. Supports `edge_geometry` (real road shape) and an optional `hazard` base layer, same
  colorblind-safe encoding as Tier 1. Backend chosen by spike (lonboard over pydeck: ~2× smaller HTML
  and ~25× faster build at 40K edges, one-line GeoDataFrame ingestion, token-free basemap). Needs the
  `[viz]` extra (now pins lonboard; matplotlib retained for Tier 1). CI now installs the interop/viz
  extras so these renderers are actually exercised.
- **Static fragility maps (`gravel.viz` Tier 1).** `plot_fragility(graph, result, …)` renders the
  per-edge failure trace as a static, colorblind-safe matplotlib choropleth — the researcher's
  accurate artifact. Progressive survivors are greyed (not painted "failed first"); an optional
  `hazard` layer draws the risk geometry (e.g. floodplain) underneath as the causal "why"; and
  `edge_geometry` draws edges along the real road shape (2B). `failure_geoframe` gains an
  `edge_geometry` argument to match. Needs the `[viz]` extra (adds matplotlib).
- **FEMA NFHL flood-data access.** `hazards.fetch_nfhl_flood_zones(bbox)` pulls real flood-hazard
  polygons from FEMA's National Flood Hazard Layer (paginated ArcGIS query, stdlib HTTP → a
  `FLD_ZONE`-tagged `GeoDataFrame` for `flood_edge_probabilities`). Endpoint is configurable via
  the `endpoint=` argument or the `GRAVEL_NFHL_ENDPOINT` environment variable
  (`hazards.NFHL_ENDPOINT`). `hazards.nfhl_zone_color` gives a severity color ramp for drawing the
  risk layer.
- **Hazard-ordered removal sequences.** `viz.failure_sequence_from_probabilities(probs, …)` turns a
  per-edge hazard probability (e.g. from `flood_edge_probabilities`) into an animatable
  `failure_round` — a seeded stochastic realization by default (worst-exposure ordered), or
  deterministic exposure order. `failure_geoframe` / `plot_fragility` / `interactive_map` /
  `animate_failure` / `animate_failure_html` now accept a `failure_round` array anywhere they took a
  progressive result, so a flood scenario is a first-class animation input.
- **Fragility dashboard.** `viz.dashboard_html(graph, result_or_failure_round, path, …)` writes a
  self-contained two-panel HTML — a deck.gl map (real geometry, optional severity-colored hazard
  base layer via `hazard_zone_field`) above a synced chart of **% of trips severed vs stage** — with
  play/slider driving both. `viz.connectivity_curve` exposes that per-stage metric
  (`1 − Σ(component_size²)/n²`, union-find). Example: `examples/python/08_asheville_flood_dashboard.py`
  (FEMA NFHL → flood order → dashboard).
- **Three-state failure coloring.** The animated renderers (`animate_failure`, `animate_failure_html`,
  `dashboard_html`) now distinguish **blocked** roads (directly failed — red, `FAILED_COLOR`) from
  **stranded** roads (intact but cut off from the main network — yellow, `STRANDED_COLOR`), over the
  still-connected network (blue, `ACTIVE_COLOR`). `viz.disconnection_rounds(graph, failure_round)`
  computes the per-edge round at which an edge becomes stranded (union-find per stage); on by default
  via `show_stranded=True`. This surfaces the network amplification the severed-% chart measures — a
  handful of blocked crossings isolating a much larger dry area.

### Changed
- **Default animation colors.** Failed/blocked edges now render **red** (was grey) and the new
  stranded state renders **yellow** across the animated renderers (`ACTIVE_COLOR` / `FAILED_COLOR` /
  `STRANDED_COLOR` are overridable). The static `plot_fragility` choropleth is unchanged.
- **Connectivity/disconnection moved to the C++ engine.** The severed-fraction curve and per-edge
  stranded rounds are now computed by a single C++ kernel, `network_disruption(graph, failure_round)`
  (`gravel-fragility`, `analysis/`, one reverse-incremental union-find). `viz.connectivity_curve` and
  `viz.disconnection_rounds` are now thin wrappers over it (same signatures) — milliseconds on
  county-scale graphs, keeping `viz` a thin layer over the engine.
- **Five more hot kernels moved to the C++ engine.** Following the connectivity move, the remaining
  Python analysis hotspots now compute in C++, with the Python names kept as thin wrappers (identical
  signatures, ABI unchanged):
  - **`hazard_edge_probabilities`** (`gravel-fragility`, `scenario_fragility.h`) — multi-zone
    point-in-polygon with per-polygon bbox pre-filter, both-endpoints rule, max-wins, and per-node
    PIP caching. This was the dominant Python cost: **101,760 edges × 250 zones now in ~228 ms**
    (previously the multi-second-to-minutes hotspot on national runs).
  - **`edge_failure_round`** (`gravel-fragility`, `analysis/`) — maps a flat removal sequence to
    per-edge rounds via a per-`(u, v)` queue (parallel-edge safe).
  - **`failure_sequence_from_probabilities`** (`gravel-fragility`, `analysis/`) — engine-side RNG (`mt19937_64`,
    seeded, thread-count invariant) for the seeded stochastic realization; deterministic
    worst-exposure order otherwise.
  - **`from_geodataframe` node snapping** (`graph_from_endpoints`, `gravel-core`) — coordinate
    quantization + node dedup in C++; `interop` extracts endpoints (shapely) then hands off.
  - **`simplify_edge_geometry`** (`gravel-core`, Douglas–Peucker in degree space) — downscales
    per-edge polylines natively. The animated renderers expose it as a `geometry_tolerance=` parameter
    (0 = full resolution), so map granularity is a render-time knob instead of a viz pre-pass.
- **`SimplificationConfig.emit_geometry` defaults to `true`** — new simplified graphs carry per-edge
  geometry out of the box (a few MB per county). Internal fragility paths that discard it
  (`location_fragility`, per-county analysis, the `simplify` CLI) opt out, so the ~2 s
  county-fragility hot path pays nothing.
- Corrected the degree-2 contraction docs: it is not unconditionally "lossless" — an isolated
  degree-2 cycle (a ring with no junction, or a lollipop loop) has no anchor and is dropped. It
  carries no junction-to-junction route, so routing/fragility on the surviving graph is unaffected.

### Fixed
- **Phantom one-way edges in degree-2 contraction.** A merged direction was emitted from a positive
  weight *sum* rather than actual edge *existence*, so contracting a one-way chain could synthesize a
  nonexistent (usually zero-weight) reverse edge — distorting routing and fragility on simplified
  graphs (~396 such edges on the Swain fixture). Contraction now tracks per-direction existence and
  emits a direction only when the through-path exists. *(Pre-existing; surfaced by the 2B geometry
  work, which drew the phantoms on maps.)*
- **`viz.edge_failure_round` on parallel edges.** Graphs with parallel edges (e.g. several degree-2
  chains contracted between the same two junctions) mis-sized the output and raised `IndexError`; it
  now keys a per-`(u, v)` queue so each parallel edge gets its own round, aligned to `edge_count`.

## [2.4.0] — 2026-07-01

**Phase 2A — research depth.** Adds modeling depth on top of the topological core, all as
**disclosed, sweepable inputs** (capacity, failure probability, cascade tolerance) reported as
curves/distributions — never hidden constants. The DAG is intact: capacity/probabilities enter
`gravel-fragility` as input arrays; derivation lives in `gravel-geo`/Python.

### Added
- **HCM capacity model** (`gravel-geo`). `estimate_capacity(EdgeMetadata, CapacityConfig=CapacityConfig.hcm())`
  → per-edge capacity (PCE/hour) = `lanes × per-lane-capacity(highway class)`, with class-default
  lanes when the `lanes` tag is absent. Citable HCM-style defaults (motorway ~2200 down to service
  ~400 PCE/h/lane); every constant is overridable and meant to be sensitivity-swept. Uses the
  `EdgeMetadata` exposed in 2.3.0 (OSM builds).
- **Capacity-aware betweenness.** `BetweennessConfig.edge_capacity` populates
  `BetweennessResult.criticality` (betweenness ÷ capacity — saturation); `capacity_weighted_importance(betweenness, capacity)`
  = betweenness × capacity (consequence — high-throughput corridors rank above low-capacity streets).
- **Stochastic fragility.** `stochastic_fragility(graph, ch, shortcut_index, edge_probabilities, config)`
  → a *distribution* of fragility under independent per-edge failures: mean / std / p50 / p90 / p99 of
  O-D distance inflation, mean disconnected fraction, and an exceedance curve. Three targets
  (`OD_DISTANCE_INFLATION` default, `LOCATION_ISOLATION`, `INTER_REGION`) all reduce to distance
  inflation over a probe-pair set. Uses `BlockedCHQuery` (no CH rebuild), parallel over runs, seeded
  with an ordered reduction (thread-count invariant). Flagship hazard source: floodplain / FEMA-NFHL
  intersection → per-edge closure probability.
- **Motter–Lai cascading failure (experimental).** `cascade_fragility(graph, config)` and
  `cascade_vs_alpha(graph, config, alphas)`: load = edge betweenness, capacity = `(1+α)·initial_load`
  (or PCE-weighted); recompute betweenness on the degraded graph each round (failed edges masked with
  infinite weight, so edge indexing is preserved), fail overloaded edges, iterate to a fixed point.
  No demand matrix, no CH mutation. Reported as cascade-size-vs-α (which can be non-monotone near the
  transition — a real property of the model). Set `BetweennessConfig.deterministic` for reproducible
  cascades and prefer sampled betweenness on large graphs.
- **Floodplain / hazard ingestion.** New `gravel.hazards` module turns a spatial hazard footprint into
  the per-edge failure-probability array `stochastic_fragility` consumes: `hazard_edge_probabilities`
  (geopandas-free core over `(Polygon, prob)` zones) and `flood_edge_probabilities` (FEMA NFHL
  `GeoDataFrame` → probabilities), plus disclosed `NFHL_EVENT_CLOSURE` (design-flood scenario, default)
  and `NFHL_ANNUAL_PROBABILITY` (annual-exceedance) tables. Derivation lives in Python; the DAG keeps
  `gravel-fragility` hazard-agnostic. Reuses the shipped `edges_in_polygon`; no C++/ABI change.
- **Visualization data bridge (`gravel.viz`, Tier 0).** Turns a fragility result into a per-edge column
  ready for `gdf.plot(...)` / pydeck / lonboard: `edge_failure_round` (progressive greedy removal order
  → animatable rounds), `edge_failure_frequency` (stochastic per-edge P(fail) → static choropleth), and
  `failure_geoframe` (dispatches to a plot-ready `GeoDataFrame`). Rendering helpers land in 2.5.0.
- **`StochasticFragilityResult.edge_failure_frequency`** — per-edge empirical failure probability (CSR
  order, thread-count invariant), the honest floodplain visual (which roads actually fail across runs).

### Docs
- Refreshed the README / `routing_performance.md` benchmark table (Release + OpenMP, real counties,
  2026-07-01). Documents the 2.3.0 parallel wins now visible on macOS: distance matrix and
  `route_fragility` ~5× faster (1→10-thread scaling curve included); single-threaded ops unchanged.

### Notes
- Capacity-weighted ranking is exposed as full-graph operations (criticality + weighted importance);
  embedding capacity into `progressive_fragility`'s re-indexed subgraph strategy is deferred (it needs
  a subgraph edge-index mapping).

## [2.3.0] — 2026-06-30

The **interop keystone** — the Python surface now exposes capabilities that previously
existed only in the C++ core, and gains first-class adapters to the geo-Python ecosystem.
This unblocks NetworkX/GeoPandas workflows and visualization without adding any heavy
dependency to the core install. No existing API changed; all additions are backward
compatible.

### Added
- **`Graph` coordinate and COO accessors.**
  - `Graph.node_coordinates() -> ndarray (N, 2)` — per-node `[lat, lon]` (`(0, 2)` when the
    graph has no coordinates).
  - `Graph.has_coordinates -> bool`.
  - `Graph.to_coo() -> (sources, targets, weights)` — three parallel NumPy arrays
    (`uint32, uint32, float64`) in CSR edge order.
  - `Graph.from_coo(num_nodes, sources, targets, weights, coords=None)` — the inverse of
    `to_coo()`, with optional `(N, 2)` `[lat, lon]` coordinates. Round-trips exactly.
- **Per-edge OSM metadata in Python.** `load_osm_graph_with_metadata(pbf_path, speed_profile=SpeedProfile.car(), bidirectional=True) -> (Graph, EdgeMetadata)`
  wires up the existing C++ `load_osm_graph_with_labels` path. The new `EdgeMetadata`
  Python type exposes `keys`, `get(key)`, `has(key)`, `key in md`, and `md[key]`, with
  per-edge tags (`highway`, `lanes`, `maxspeed`, `name`, `surface`, `bridge`, `tunnel`,
  `ref`) in CSR edge order — aligned with `Graph.to_coo()`. **This resolves the v2.2.2 note**
  that deferred per-edge OSM tag access ("If you need per-edge OSM tags in Python, open an
  issue"). OSM-enabled builds only (`gravel.HAS_OSM`).
- **GeoJSON / tabular export bindings.** `route_to_geojson(graph, path, fragility=None)` and
  `location_fragility_to_geojson(result, center)` return GeoJSON strings;
  `write_fragility_jsonl(results, od_pairs, path)` writes JSON Lines (always available). The
  Arrow Parquet writers (`write_fragility_parquet`, `write_county_fragility_parquet`,
  `write_betweenness_parquet`) are now exposed when built with Arrow.
- **`gravel.HAS_ARROW: bool`** — public feature flag for Parquet support, mirroring
  `gravel.HAS_OSM`. `False` in the standard PyPI wheels (built without Arrow).
- **`gravel.interop` adapter module** — NetworkX and GeoPandas converters:
  `to_networkx` / `from_networkx` and `to_geodataframe` / `from_geodataframe`. Optional
  dependencies are lazy-imported, so importing `gravel.interop` never fails for a missing
  package; install the heavier ones with the new **`gravel-fragility[interop]`** extra
  (NetworkX, GeoPandas, Shapely, pyproj). Edge metadata flows through as NetworkX edge
  attributes and GeoDataFrame columns. From a `GeoDataFrame`, `gdf.explore()` yields an
  interactive Folium map.

### Changed
- **NumPy is now a required runtime dependency.** The core `Graph` array accessors
  (`node_coordinates`, `to_coo`, `from_coo`) and the CSR constructor return/accept NumPy
  arrays, so `numpy>=1.23` moved from the `[interop]` extra into `[project].dependencies`.
  NumPy is the floor of the scientific-Python stack and present in essentially every relevant
  deployment; heavier integrations (NetworkX, GeoPandas) remain optional.

### Performance & parallelism (hardening)

This release also hardens Gravel's parallelism — making it real on every platform,
exploiting the largest parallel axis, and giving reproducibility a guarantee.

- **macOS builds are no longer silently serial.** `find_package(OpenMP)` fails on Apple
  Clang (no bundled OpenMP runtime), which meant every `#pragma omp` was a no-op and the
  parallel kernels (fragility, betweenness, distance matrices) ran single-threaded on macOS
  *with no warning*. New `cmake/OpenMPDetect.cmake` locates Homebrew `libomp` and builds a
  working `OpenMP::OpenMP_CXX` target, and emits a loud status line either way. The macOS
  wheel build now installs `libomp`. Measured ~4.6× on betweenness (10-core M-series) where
  it was previously serial.
- **macOS wheels are now arm64-only.** Homebrew on the Apple-Silicon CI runner provides an
  arm64-only `libomp`, so an x86_64 (Intel) cross-build links no OpenMP runtime and the wheel
  fails to import. Rather than ship a broken or silently-serial Intel wheel, the project ships
  arm64 wheels (with OpenMP) and Intel-Mac users install from the **sdist**, which now builds
  fully offline (vendored Eigen) and picks up the user's own `libomp`. Apple stopped selling
  Intel Macs in 2023.
- **`gravel.HAS_OPENMP: bool`, `gravel.max_threads()`, `gravel.set_max_threads(n)`** — make
  the parallel state visible and controllable; `set_max_threads` is also used to avoid
  oversubscription under a process pool. (`GRAVEL_HAS_OPENMP` is the C++ equivalent.)
- **`route_fragility` is now parallelized over path edges.** Previously only `batch_fragility`
  (over O-D pairs) was threaded; a single `route_fragility` ran its per-edge blocked queries
  serially. On a long real path this was minutes; it now scales across cores with a
  thread-local `BlockedCHQuery` (same pattern as `batch_fragility`).
- **National pipeline parallelizes across counties.** `scripts/national_fragility.py` gains
  `--jobs/-j N`: a process pool over the (independent) counties, with each worker's OpenMP
  threads capped to `cores // jobs` to prevent W×cores oversubscription. The pipeline was
  previously serial across counties.
- **Reproducible betweenness.** `BetweennessConfig.deterministic` (default `false`)
  accumulates source contributions serially in a fixed order, so the result is bit-identical
  across runs and thread counts — for when betweenness feeds a published/covariate value. The
  default parallel path can differ in low-order bits (~1e-9) by thread count. (Monte Carlo
  fragility statistics were already thread-count deterministic — `compute_level_stats` sorts
  before aggregating.)

### Notes
- **Geometry caveat.** Edges store endpoints, not the intermediate OSM way polyline, so
  `to_geodataframe` renders straight node-to-node segments. Persisting full edge geometry is
  planned for a later release; treat the geometry as topological, not cartographic.
- The DAG is preserved: all additions live in the Python bindings, `gravel.interop`
  (pure Python), or existing layers. No new cross-library C++ dependency was introduced.

## [2.2.3] — 2026-06-28

### Changed
- **Package metadata now links to the [Awry Labs](https://awrylabs.com/) project pages.** The PyPI sidebar (`[project.urls]`) gains a *Project Page (Awry Labs)* link (https://awrylabs.com/gravel.html) and a *Kindling (related project)* cross-link (https://awrylabs.com/kindling.html); the README "About" section carries the same links. No code or API changes — metadata only.

## [2.2.2] — 2026-04-20

### Added
- **OSM support now ships in every PyPI wheel on every platform** (Linux x86_64/aarch64, macOS x86_64/arm64, Windows AMD64 — 20 wheels). `cibuildwheel` `before-all` hooks install libosmium + protozero + runtime libs (zlib/expat/bz2/lz4) on each platform before the wheel build: headers-only checkout for manylinux, `brew install` for macOS, `vcpkg install` for Windows. `load_osm_graph`, `OSMConfig`, `SpeedProfile` are present in the `gravel` module after `pip install gravel-fragility` — no source build required.
- **`gravel.HAS_OSM: bool`** — public feature flag for runtime OSM detection. Replaces the implicit `hasattr(gravel, "load_osm_graph")` pattern. Guaranteed to exist on every build (True when OSM is compiled in, False otherwise) so downstream code can branch cleanly.

### Fixed
- `python/gravel/__init__.py` no longer imports `load_osm_graph_with_labels` — that symbol exists in the C++ core (`OSMLoadResult load_osm_graph_with_labels(...)`) but was never exposed via pybind11, so the `from ._gravel import` always raised `ImportError` and the old `except: pass` silently set OSM imports as all-or-nothing. The stale import masked that `HAS_OSM` could never be `True` via the old pattern. If you need per-edge OSM tags in Python, open an issue — we'll add the `OSMLoadResult` binding in a future release.
- **`GRAVEL_USE_OSMIUM` now supports `AUTO`** as the default. Three values:
  - `AUTO` (default) — enable if libosmium is found, disable gracefully if not
  - `ON` — hard-require libosmium, fail configure if missing
  - `OFF` — never enable, even if libosmium is present
  Source-build users no longer need to manually pass any flag to get OSM loaders when libosmium is installed. `cmake/OsmiumDetect.cmake` centralizes the detection + version check (≥2.20) + informative status messages in one place.

### Changed
- **CMake OSM detection emits loud status messages** in every configure run. Enabled builds print the libosmium path + version. Disabled AUTO builds print platform-specific install commands (`brew install libosmium protozero` / `sudo apt install libosmium2-dev` / `conda install -c conda-forge libosmium` / `vcpkg install libosmium protozero`) plus the `-DGRAVEL_USE_OSMIUM=ON` escape hatch. Source builders never need to guess what they got.
- **Windows CI now tests the OSM code path.** `vcpkg install libosmium protozero zlib expat bzip2 lz4` runs before the Windows C++ tests configure, and the matrix entry's `osm-flag` changed from `OFF` to `ON`.
- **PyPI wheel test command** now asserts `gravel.HAS_OSM` — if the `before-all` hook ever fails to install libosmium but the build somehow completes, the test fails fast rather than silently shipping a no-OSM wheel.

### Notes
- Source-distribution installs (`pip install gravel-fragility --no-binary gravel-fragility`) behave on the AUTO default: users with libosmium get OSM automatically; users without it get a fully working library minus OSM loaders and a clear CMake message explaining how to enable them.
- conda-forge recipe (PR [#33037](https://github.com/conda-forge/staged-recipes/pull/33037)) will be updated to target v2.2.2. The pybind11 patch remains necessary there (network-blocked build sandbox).


## [2.2.1] — 2026-04-19

### Added
- **Windows support** — `src/io/mapped_file.cpp` now has a Windows backend using `CreateFileMappingW`/`MapViewOfFile`/`UnmapViewOfFile`, with UTF-8 paths transparently converted to wide for the WinAPI. `FILE_FLAG_SEQUENTIAL_SCAN` hints the cache manager to read-ahead, matching the POSIX `madvise(MADV_SEQUENTIAL)` hint. Binary wheels now ship for `win_amd64` alongside Linux and macOS.
- **`gravel/core/constants.h`** — centralized `PI`, `TWO_PI`, `DEG_TO_RAD`, `RAD_TO_DEG` via `std::numbers` (C++20). Replaces all uses of the POSIX `M_PI` macro across source and tests. `M_PI` was a GNU extension not exposed by MSVC's `<cmath>`, which blocked Windows builds.
- **`FIND_PACKAGE_ARGS` on every `FetchContent_Declare`** — CMake 3.24 feature that lets builds use a system/conda-forge/vcpkg-installed package when available, and only fall back to git-cloning when no system dep exists. This makes the project conda-forge-ready out of the box (conda-forge blocks network at build time, so system deps are required there) and sidesteps systemic FetchContent git-clone hangs observed on GitHub Actions `windows-latest` runners.

### Changed
- **Minimum CMake is now 3.24** (was 3.20) — required for `FetchContent_Declare(... FIND_PACKAGE_ARGS ...)`. Ubuntu 22.04's stock `apt install cmake` gives 3.22, which no longer suffices; 22.04 users should `pip install cmake` or `snap install cmake --classic` to get ≥3.24. Ubuntu 24.04 ships 3.28, so stock apt works there.
- **CI now runs on Windows** — C++ tests execute on `windows-latest` in the matrix with `GRAVEL_USE_OSMIUM=OFF` (libosmium is unavailable on Windows). OSM tests continue to run only on Linux and macOS. Windows CI pre-installs Eigen + nlohmann-json + Catch2 via vcpkg; wheel builds similarly pre-install Eigen + nlohmann-json via cibuildwheel's `before-build` hook.
- **macOS CI now installs Eigen + nlohmann-json via brew** — takes advantage of the new `FIND_PACKAGE_ARGS` path for faster configure (skips Eigen git-clone).
- Wheel-build matrix re-includes `windows-latest`; `pyproject.toml` re-adds the Windows classifier.

### Notes
- OSM loaders (`load_osm_graph`, `OSMConfig`, `SpeedProfile`) are still unavailable on Windows PyPI wheels — libosmium has no official Windows distribution on PyPI. conda-forge support (shipping with OSM loaders enabled via the conda-forge libosmium package) is being prepared separately.


## [2.2.0] — 2026-04-19

### Added
- **`gravel/simplify/reduced_graph.h`** — generic `build_reduced_graph()` collapses a partitioned graph to one central node per region + border nodes, with intra-region central-to-border edges weighted by CH distance and inter-region edges preserved. Region-agnostic (works with any int32 region partition, not just US counties).
- **`gravel/fragility/inter_region_fragility.h`** — `inter_region_fragility()` runs progressive edge-removal fragility on a `ReducedGraph`, producing a degradation curve + AUC metrics per adjacent pair
- **`gravel/geo/geography_skeleton.h`** — thin adapter `build_reduced_geography_graph()` for use with `RegionAssignment` (geography-specific convenience)
- **Node betweenness** — `BetweennessResult.node_scores` field added (Brandes's algorithm naturally computes it; was previously discarded)
- **`ReducedGraphConfig::Centrality::PROVIDED`** — caller-provided central nodes (used to select betweenness-based centrals from gravel-fragility without violating the dependency DAG)
- `scripts/national_inter_county.py` — **adjacency-driven** national pipeline that handles cross-state county pairs correctly (via `osmium merge` of state PBFs); reduces ~10M-node state graphs to a few-thousand-node reduced graph for fast inter-county fragility
- `scripts/benchmark_routing.py` — routing and route fragility performance benchmarks
- `tests/test_inter_geography.cpp` — 7 new tests covering skeleton + inter-geography fragility
- National inter-county fragility dataset: `data/sample-results/inter_county_fragility.csv` (8,547 adjacent county pairs incl. 1,082 cross-state)

### Changed
- **Sub-library placement of geography reduction**: moved `ReducedGraph` + `build_reduced_graph()` to `gravel-simplify` (it's a graph-reduction operation), `inter_region_fragility()` to `gravel-fragility`. Thin geo-specific adapters remain in `gravel-geo`. Clean dependency DAG preserved.
- **`RegionPair` / `RegionPairHash`** moved from `gravel/geo/border_edges.h` to `gravel/simplify/reduced_graph.h` (generic integer-pair utility). `border_edges.h` re-imports via `#include`.
- `scripts/national_inter_county.py` — now **adjacency-driven** instead of state-by-state. Cross-state county pairs (e.g., Bristol VA ↔ Bristol TN) are now captured correctly via `osmium merge`.


## [2.1.0] — 2026-04

### Added
- **`IncrementalSSSP`** — reverse-incremental shortest-path engine (`gravel/core/incremental_sssp.h`)
- **`EdgeSampler`** — unified edge sampling with 5 strategies (`gravel/core/edge_sampler.h`)
- **`RegionAssignment`** — node-to-region mapping via point-in-polygon (`gravel/geo/region_assignment.h`)
- **`GeoJSONLoader`** — GeoJSON boundary loading with coordinate swap (`gravel/geo/geojson_loader.h`)
- **`BoundaryNodes`** — identify region-boundary nodes for simplification protection (`gravel/geo/boundary_nodes.h`)
- **`BorderEdges`** — summarize cross-region edges (`gravel/geo/border_edges.h`)
- **`GraphCoarsening`** — collapse regions into meta-nodes (`gravel/geo/graph_coarsening.h`)
- **`RegionSerialization`** — binary save/load for RegionAssignment (`gravel/geo/region_serialization.h`)
- **TIGER loaders** — US Census county/state/CBSA/place/urban-area GeoJSON (`gravel/us/tiger_loader.h`)
- **`CountyAssignment`, `CBSAAssignment`** — typed wrappers (`gravel/us/*.h`)
- **`FIPSCrosswalk`** — county/state/CBSA lookups (`gravel/us/fips_crosswalk.h`)
- **`EdgeMetadata`** extracted to `gravel/core/edge_metadata.h` (reusable outside OSM)
- Python bindings for all new types
- `scripts/national_fragility.py` — national US county fragility pipeline
- `scripts/inter_county_matrix.py` — inter-county fragility via graph coarsening
- `scripts/visualize_results.py` — choropleth and distribution visualizations
- `examples/` directory with Python and C++ tutorials

### Changed
- **Complete rewrite of `location_fragility`** using Dijkstra + IncrementalSSSP on simplified subgraph. ~400x speedup: from 80+ minutes to ~2 seconds on 200K-node county graphs. Config and result structures redesigned (breaking change).
- **`county_fragility_index` optimization** — replaced per-edge CH fragility calls with local Dijkstra on the simplified subgraph
- **Scenario fragility fast path** — uses `BlockedCHQuery` instead of rebuilding the full CH (10-100x speedup)
- **Sub-library architecture** — split monolithic library into 6 targets with enforced dependency DAG:
  - `gravel-core` (stdlib + OpenMP only)
  - `gravel-ch` (+ contraction hierarchy)
  - `gravel-simplify` (+ simplification, bridges)
  - `gravel-fragility` (+ all fragility, Eigen/Spectra)
  - `gravel-geo` (+ OSM, regions, snapping)
  - `gravel-us` (+ TIGER, FIPS)
- **Boundary-aware `contract_degree2`** accepts optional `boundary_protection` set to preserve border nodes
- **Config ownership assertions** — `ProgressiveFragilityConfig` now validates inputs with `std::invalid_argument`
- **Table-driven strategy dispatch** in progressive fragility (replaces switch statement)
- **Deduplicated SSSP code** — extracted shared logic to internal `progressive_sssp.h`
- **Python API cleanup** — `python/gravel/__init__.py` now exports 96 symbols with conditional OSM imports

### Removed
- **`progressive_location_fragility`** — replaced by integrated MC/Greedy support in `location_fragility`
- **`ShortcutIndex` parameter** from `location_fragility()` signature (not needed in new design)

### Fixed
- Bridge-endpoint-protection pipeline ordering (`boundary_nodes()` must run on filtered graph)
- Node ID mapping through simplified → raw subgraph → original graph chain
- Floating-point epsilon for SP-edge DAG criterion

## [2.0.0] — 2025-12

### Added
- Progressive elimination fragility with Monte Carlo / Greedy Betweenness / Greedy Fragility strategies
- Degradation curve with AUC metrics, critical-k detection, jump detection
- `AnalysisContext` performance cache (subgraph + simplification + bridges + entry points)
- Scenario fragility analysis (hazard footprint intersection)
- Edge confidence scoring from OSM metadata
- Tiled fragility analysis (spatial fragility fields)
- Bridge classification (motorway vs arterial vs local)
- Bridge replacement cost estimation
- Population-weighted OD sampling
- Ensemble fragility with weight sensitivity analysis
- Edge metadata generic tag store

## [1.0.0] — 2025-09

### Added
- Initial release
- Contraction hierarchy construction and query
- Route fragility with per-edge replacement path ratios
- `BlockedCHQuery` for edge-removal distance queries
- Alternative route finding (Hershberger-Suri, via-path)
- Bernstein approximation for penalty-based routing
- County fragility index (composite of bridges, connectivity, accessibility, fragility)
- Location fragility (geographic isolation risk)
- Algebraic connectivity, Kirchhoff index, natural connectivity
- Edge betweenness (exact and sampled)
- Bridge detection and classification
- Coordinate snapping with quality reports
- Elevation data integration (SRTM)
- Closure risk classification
- Graph simplification (degree-2 contraction, edge filtering, CH pruning)
- Landmark-based A* lower bounds
- OSM PBF loading via libosmium
- CSV graph loading
- Python bindings via pybind11
- CLI tool for common operations
