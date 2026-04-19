# Changelog

All notable changes to Gravel are documented here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
