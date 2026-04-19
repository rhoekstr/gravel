# Gravel Technical Reference

Gravel is a C++ library (with Python bindings) for road network routing and vulnerability analysis. It combines contraction hierarchy routing with replacement-path fragility computation to measure how robust a road network is when individual edges fail. Gravel targets transportation resilience research at county, region, and state scales.

**Core design principles:**

- Structure-of-Arrays (SoA) CSR graph layout for cache efficiency
- All graph sources (CSV, OSM, numpy) normalize to a single `ArrayGraph` representation
- Contraction hierarchy for sub-millisecond shortest path queries
- Replacement-path fragility: for every edge on a route, what happens if that edge is removed?
- Composable analysis pipeline: bridges, spectral metrics, betweenness, fragility sampling

---

## Table of Contents

1. [Fundamental Types](#1-fundamental-types)
2. [Graph Construction](#2-graph-construction)
3. [Contraction Hierarchy](#3-contraction-hierarchy)
4. [Route Fragility](#4-route-fragility)
5. [Alternative Routes](#5-alternative-routes)
6. [Network Analysis](#6-network-analysis)
7. [County Fragility Index](#7-county-fragility-index)
8. [Location Fragility](#8-location-fragility)
9. [Graph Simplification](#9-graph-simplification)
10. [Elevation and Closure Risk](#10-elevation-and-closure-risk)
11. [Coordinate Snapping](#11-coordinate-snapping)
12. [Filter Pipeline](#12-filter-pipeline)
13. [Landmarks (ALT Lower Bounds)](#13-landmarks-alt-lower-bounds)
14. [Geographic Utilities](#14-geographic-utilities)
15. [CLI Reference](#15-cli-reference)
16. [Scale Limits and Constraints](#16-scale-limits-and-constraints)
17. [v2.0 Enhancements](#17-v20-enhancements)
    - 17.1 [Fragility Distribution Statistics](#171-fragility-distribution-statistics)
    - 17.2 [Directional Asymmetry](#172-directional-asymmetry)
    - 17.3 [Bridge Replacement Cost](#173-bridge-replacement-cost)
    - 17.4 [Bridge Classification](#174-bridge-classification)
    - 17.5 [Population-Weighted OD Sampling](#175-population-weighted-od-sampling)
    - 17.6 [Scenario Fragility (Hazard Footprint)](#176-scenario-fragility-hazard-footprint)
    - 17.7 [Ensemble Fragility (Sampling Variance)](#177-ensemble-fragility-sampling-variance)
    - 17.8 [Weight Sensitivity Analysis](#178-weight-sensitivity-analysis)
    - 17.9 [Edge Confidence](#179-edge-confidence)
    - 17.10 [Multi-Scale Tiled Fragility](#1710-multi-scale-tiled-fragility)
    - 17.11 [Edge Metadata (Generic Tag Storage)](#1711-edge-metadata-generic-tag-storage)
18. [Progressive Elimination Fragility](#18-progressive-elimination-fragility)
    - 18.1 [The Degradation Curve](#181-the-degradation-curve)
    - 18.2 [Edge Selection Strategies](#182-edge-selection-strategies)
    - 18.3 [Configuration](#183-configuration)
    - 18.4 [Result Structures](#184-result-structures)
    - 18.5 [AUC Metrics](#185-auc-metrics)
    - 18.6 [Critical K and Jump Detection](#186-critical-k-and-jump-detection)
    - 18.7 [CLI Reference](#187-cli-reference)
    - 18.8 [Code Example](#188-code-example)
    - 18.9 [Interpretation Guide](#189-interpretation-guide)
    - 18.10 [Methodology Notes](#1810-methodology-notes)
19. [AnalysisContext (Performance Cache)](#19-analysiscontext-performance-cache)
20. [Performance Optimizations](#20-performance-optimizations)
21. [Sub-Library Architecture (v2.1)](#21-sub-library-architecture-v21)
22. [EdgeSampler](#22-edgesampler)
23. [IncrementalSSSP](#23-incrementalsssp)
24. [RegionAssignment](#24-regionassignment)
25. [Boundary-Aware Degree-2 Contraction](#25-boundary-aware-degree-2-contraction)
26. [Border Edge Summarization](#26-border-edge-summarization)
27. [Geographic Graph Coarsening](#27-geographic-graph-coarsening)
28. [FIPS Crosswalk and Typed Wrappers](#28-fips-crosswalk-and-typed-wrappers)
29. [Reduced Graph](#29-reduced-graph-gravelsimplifyreduced_graphh)
30. [Inter-Region Fragility](#30-inter-region-fragility-gravelfragilityinter_region_fragilityh)

---

## 1. Fundamental Types

Defined in `gravel/core/types.h`.

| Type | Underlying | Description |
|------|-----------|-------------|
| `NodeID` | `uint32_t` | Node identifier. Max value: 2^32 - 2 (4,294,967,294 nodes). |
| `EdgeID` | `uint32_t` | Edge identifier (position within CSR arrays). |
| `Weight` | `double` | Edge weight (travel time in seconds for OSM, arbitrary for CSV). |
| `Level` | `uint32_t` | Contraction hierarchy level. Higher = more important. |

**Sentinel values:**

| Constant | Value | Meaning |
|----------|-------|---------|
| `INVALID_NODE` | `uint32_t max` (4,294,967,295) | No node / null reference. |
| `INF_WEIGHT` | `+infinity` | Unreachable / no path exists. |

### Coord

Geographic coordinate.

```cpp
struct Coord {
    double lat = 0.0;
    double lon = 0.0;
};
```

### Edge

Full edge representation used during graph construction.

```cpp
struct Edge {
    NodeID source;
    NodeID target;
    Weight weight;
    Weight secondary_weight = 0.0;  // e.g., distance in meters when weight = travel time
    std::string label;               // e.g., OSM highway tag ("primary", "residential")
};
```

### CompactEdge

Minimal edge for CSR storage. Source is implicit from the offset array.

```cpp
struct CompactEdge {
    NodeID target;
    Weight weight;
};
```

---

## 2. Graph Construction

All graph sources produce an `ArrayGraph`, the canonical in-memory representation.

### GravelGraph (Interface)

Defined in `gravel/core/graph_interface.h`. Abstract base class for all graph types.

| Method | Return | Description |
|--------|--------|-------------|
| `node_count()` | `NodeID` | Total number of nodes. |
| `edge_count()` | `EdgeID` | Total number of directed edges. |
| `outgoing_targets(node)` | `span<const NodeID>` | Target array for outgoing edges from `node`. |
| `outgoing_weights(node)` | `span<const Weight>` | Weight array for outgoing edges from `node`. |
| `degree(node)` | `uint32_t` | Out-degree of `node`. |
| `node_coordinate(node)` | `optional<Coord>` | Geographic coordinate, if available. |
| `edge_label(node, local_idx)` | `optional<string>` | Edge label (e.g., road class), if available. |
| `edge_secondary_weight(node, local_idx)` | `optional<Weight>` | Secondary weight (e.g., distance), if available. |

### ArrayGraph

Defined in `gravel/core/array_graph.h`. Compressed Sparse Row graph with SoA layout.

**Constructors:**

```cpp
// From edge list. Nodes numbered [0, num_nodes).
ArrayGraph(NodeID num_nodes, std::vector<Edge> edges);

// From pre-built SoA CSR arrays (deserialization, pybind11).
ArrayGraph(std::vector<uint32_t> offsets,
           std::vector<NodeID> targets,
           std::vector<Weight> weights,
           std::vector<Coord> coords = {});
```

**Raw array access** (for CH builder and serialization):

| Method | Returns | Size |
|--------|---------|------|
| `raw_offsets()` | `const vector<uint32_t>&` | `node_count + 1` |
| `raw_targets()` | `const vector<NodeID>&` | `edge_count` |
| `raw_weights()` | `const vector<Weight>&` | `edge_count` |
| `raw_coords()` | `const vector<Coord>&` | `node_count` or empty |

### CSV Loading

Defined in `gravel/core/csv_graph.h`.

#### CSVConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `path` | `string` | (required) | Path to CSV file. |
| `delimiter` | `char` | `','` | Column delimiter. |
| `has_header` | `bool` | `true` | First row is column names. |
| `source_col` | `string` | `"source"` | Column name for source node ID. |
| `target_col` | `string` | `"target"` | Column name for target node ID. |
| `weight_col` | `string` | `"weight"` | Column name for edge weight. |
| `secondary_weight_col` | `string` | `""` | Column for secondary weight (optional). |
| `label_col` | `string` | `""` | Column for edge labels (optional). |
| `bidirectional` | `bool` | `false` | Add reverse edge for every edge. |

```cpp
std::unique_ptr<ArrayGraph> load_csv_graph(const CSVConfig& config);
```

### OSM Loading

Defined in `gravel/core/osm_graph.h`. Requires compile-time flag `GRAVEL_HAS_OSMIUM=1`.

#### SpeedProfile

Provides default speed mappings (km/h) by OSM highway tag.

| Method | Description |
|--------|-------------|
| `SpeedProfile::car()` | Returns a `map<string, double>` with car speed defaults. |

Default car speeds: motorway=110, trunk=90, primary=70, secondary=60, tertiary=50, residential=30, unclassified=40, service=20, living_street=10. Link types are 50-65% of their parent.

#### OSMConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `pbf_path` | `string` | (required) | Path to `.osm.pbf` file. |
| `speed_profile` | `map<string, double>` | (required) | Highway tag to speed (km/h). |
| `bidirectional` | `bool` | `true` | Add reverse edges for two-way roads. |

```cpp
std::unique_ptr<ArrayGraph> load_osm_graph(const OSMConfig& config);
```

The resulting graph has:
- **weight** = travel time in seconds
- **secondary_weight** = distance in meters
- **coordinates** for every node

### Subgraph Extraction

Defined in `gravel/core/subgraph.h`.

#### Polygon

```cpp
struct Polygon {
    std::vector<Coord> vertices;  // closed ring (first == last, or auto-closed)
};
```

#### SubgraphResult

```cpp
struct SubgraphResult {
    std::shared_ptr<ArrayGraph> graph;
    std::vector<NodeID> new_to_original;                // new_id -> original_id
    std::unordered_map<NodeID, NodeID> original_to_new; // original_id -> new_id
};
```

```cpp
SubgraphResult extract_subgraph(const ArrayGraph& graph, const Polygon& boundary);
```

**Inputs:** Graph with node coordinates, polygon boundary.
**Outputs:** Induced subgraph of nodes within the polygon, plus bidirectional ID mappings.
**Complexity:** O(V + E) -- single pass over all nodes and edges.
**Requires:** Graph must have node coordinates.

---

## 3. Contraction Hierarchy

### Build Configuration

Defined in `gravel/ch/contraction.h`.

#### CHBuildConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `edge_diff_weight` | `double` | `190.0` | Priority weight for edge difference (shortcuts_added - edges_removed). |
| `deleted_nbr_weight` | `double` | `120.0` | Priority weight for number of already-contracted neighbors. |
| `level_weight` | `double` | `1.0` | Priority weight for current contraction level. |
| `max_settle_for_witness` | `int` | `500` | Dijkstra settle limit during actual contraction witness search. |
| `max_settle_for_priority` | `int` | `50` | Dijkstra settle limit during priority estimation (cheaper). |
| `parallel` | `bool` | `false` | Use batched parallel contraction (SPoCH-style independent sets). |
| `batch_size` | `uint32_t` | `0` | Max independent set size per parallel batch. 0 = auto-select. |

Node contraction priority = `edge_diff_weight * edge_diff + deleted_nbr_weight * deleted_neighbors + level_weight * level`.

#### ContractionResult

The output of CH construction. Contains bidirectional overlay graphs in SoA CSR format.

| Field | Type | Description |
|-------|------|-------------|
| `num_nodes` | `NodeID` | Total node count. |
| `up_offsets` | `vector<uint32_t>` | CSR offsets for upward graph (size: num_nodes+1). |
| `up_targets` | `vector<NodeID>` | Edge targets in upward graph. |
| `up_weights` | `vector<Weight>` | Edge weights in upward graph. |
| `up_shortcut_mid` | `vector<NodeID>` | Shortcut midpoint. `INVALID_NODE` = original edge. |
| `down_offsets` | `vector<uint32_t>` | CSR offsets for downward graph. |
| `down_targets` | `vector<NodeID>` | Edge targets in downward graph. |
| `down_weights` | `vector<Weight>` | Edge weights in downward graph. |
| `down_shortcut_mid` | `vector<NodeID>` | Shortcut midpoint for downward graph. |
| `node_levels` | `vector<Level>` | Contraction level per node. |
| `order` | `vector<NodeID>` | `order[rank] = original node ID`. |
| `unpack_map` | `unordered_map<uint64_t, NodeID>` | `pack_edge(u,v)` -> midpoint for ALL overlay edges. |

```cpp
ContractionResult build_ch(const ArrayGraph& graph,
                           CHBuildConfig config = {},
                           std::function<void(int)> progress_cb = nullptr);
```

**Inputs:** ArrayGraph, optional config, optional progress callback (receives 0-100).
**Outputs:** ContractionResult with up/down overlay graphs and node ordering.
**Complexity:** O(V log V * witness_search_cost) typical. Witness search bounded by `max_settle_for_witness`.
**Parallel mode:** SPoCH-style batched independent set contraction. Finds maximal independent sets of low-priority nodes and contracts them in parallel.

### CH Query

Defined in `gravel/ch/ch_query.h`.

#### CHQuery

```cpp
class CHQuery {
public:
    explicit CHQuery(const ContractionResult& ch);

    RouteResult route(NodeID source, NodeID target) const;
    Weight distance(NodeID source, NodeID target) const;

    // Snap-aware overloads (phantom nodes)
    Weight distance(const SnapResult& from, const SnapResult& to) const;
    RouteResult route(const SnapResult& from, const SnapResult& to) const;

    // Many-to-many distance matrix (OpenMP-parallelized)
    std::vector<Weight> distance_matrix(
        const std::vector<NodeID>& origins,
        const std::vector<NodeID>& destinations) const;
};
```

#### RouteResult

```cpp
struct RouteResult {
    Weight distance = INF_WEIGHT;
    std::vector<NodeID> path;  // unpacked node sequence; empty if no path
};
```

| Method | Description | Complexity |
|--------|-------------|------------|
| `distance(s, t)` | Distance-only query, no path unpacking. | O(k) where k = nodes settled (typically ~1000 for continental graphs) |
| `route(s, t)` | Full path query with shortcut unpacking. | O(k + path_length) |
| `distance(snap_from, snap_to)` | Phantom-to-phantom via dual-seeded CH search. | O(k) |
| `route(snap_from, snap_to)` | Phantom-to-phantom with path unpacking. | O(k + path_length) |
| `distance_matrix(origins, dests)` | Returns flat row-major vector of size `|O| * |D|`. | O(\|O\| * \|D\| * k) with OpenMP parallelism |

---

## 4. Route Fragility

Route fragility measures, for every edge on the shortest s-t path, how much longer the route becomes if that edge is removed.

### Core Concepts

- **Primary path:** Shortest s-t path via CH query.
- **Replacement distance:** Shortest s-t distance with one edge blocked.
- **Fragility ratio:** `replacement_distance / primary_distance`. Always >= 1.0. Infinite if no alternative exists (bridge-like edge).
- **Bottleneck edge:** The path edge with the highest fragility ratio.

### EdgeFragility

```cpp
struct EdgeFragility {
    NodeID source = INVALID_NODE;
    NodeID target = INVALID_NODE;
    Weight replacement_distance = INF_WEIGHT;
    double fragility_ratio = 0.0;  // replacement / primary (>= 1.0, INF if bridge-like)
};
```

### FragilityResult

```cpp
struct FragilityResult {
    Weight primary_distance = INF_WEIGHT;
    std::vector<NodeID> primary_path;
    std::vector<EdgeFragility> edge_fragilities;  // one per path edge (path.size()-1)

    bool valid() const;
    size_t bottleneck_index() const;       // index of highest-ratio edge
    const EdgeFragility& bottleneck() const;
};
```

### AlternateRouteResult

```cpp
struct AlternateRouteResult {
    Weight distance = INF_WEIGHT;
    std::vector<NodeID> path;
    double sharing = 0.0;   // fraction of edges shared with primary
    double stretch = 0.0;   // distance / primary_distance
};
```

### FragilityConfig

```cpp
struct FragilityConfig {
    // Placeholder for future options (e.g., max_replacement_distance).
};
```

### Functions

```cpp
FragilityResult route_fragility(
    const ContractionResult& ch,
    const ShortcutIndex& shortcut_idx,
    const ArrayGraph& graph,
    NodeID source, NodeID target,
    FragilityConfig config = {});
```

**Algorithm:**
1. Compute primary s-t path via CH query.
2. For each edge on the path, run a `BlockedCHQuery` with that edge blocked.
3. The blocked query also blocks all CH shortcuts that expand through the blocked original edge.

**Complexity:** O(path_length * k) where k = CH query cost per blocked query.

```cpp
std::vector<FragilityResult> batch_fragility(
    const ContractionResult& ch,
    const ShortcutIndex& shortcut_idx,
    const ArrayGraph& graph,
    const std::vector<std::pair<NodeID, NodeID>>& od_pairs,
    FragilityConfig config = {});
```

**Parallelism:** OpenMP-parallelized over O-D pairs.

### ShortcutIndex

Defined in `gravel/fragility/shortcut_index.h`. Maps each original edge to all CH shortcuts that contain it.

```cpp
class ShortcutIndex {
public:
    explicit ShortcutIndex(const ContractionResult& ch);
    std::span<const uint64_t> shortcuts_using(NodeID u, NodeID v) const;
    size_t size() const;
};
```

Built once from `ContractionResult` by recursively expanding every shortcut. Used by `BlockedCHQuery`.

### BlockedCHQuery

Defined in `gravel/fragility/blocked_ch_query.h`. CH query that can block specific original edges.

```cpp
class BlockedCHQuery {
public:
    BlockedCHQuery(const ContractionResult& ch, const ShortcutIndex& idx,
                   const ArrayGraph& graph);

    Weight distance_blocking(NodeID source, NodeID target,
                             const std::vector<std::pair<NodeID, NodeID>>& blocked_edges) const;
};
```

When an original edge (u,v) is blocked:
- All shortcuts that recursively expand through (u,v) are also blocked.
- If a shortcut is blocked but the underlying original edge exists and is unblocked, the original edge weight is used as a fallback.

**Thread safety:** Workspace is per-instance. Create one `BlockedCHQuery` per thread for parallel use.

### Bridge Detection

Defined in `gravel/fragility/bridges.h`.

```cpp
struct BridgeResult {
    std::vector<std::pair<NodeID, NodeID>> bridges;  // {u, v} with u < v
};

BridgeResult find_bridges(const ArrayGraph& graph);
```

**Algorithm:** Iterative Tarjan's bridge-finding algorithm.
**Complexity:** O(V + E) time and space.
**Notes:** Treats directed graph as undirected. Parallel edges between the same pair are NOT bridges.

### Hershberger-Suri Exact Replacement Paths

Defined in `gravel/fragility/hershberger_suri.h`.

```cpp
FragilityResult hershberger_suri(const ArrayGraph& graph,
                                  NodeID source, NodeID target);
```

**Algorithm:**
1. Forward Dijkstra from s: build SPT and distance array.
2. Reverse Dijkstra from t: backward distances.
3. Extract s-t path from SPT.
4. For each non-tree edge (x,y), compute detour cost: `dist_fwd[x] + w(x,y) + dist_bwd[y]`.
5. Use LCA on SPT to determine which path edge each non-tree edge can replace.
6. For each path edge, replacement distance = min detour cost among applicable non-tree edges.

**Complexity:** O(m + n log n) for a single source-target pair.
**Accuracy:** Exact.

### Bernstein Approximate Replacement Paths

Defined in `gravel/fragility/bernstein_approx.h`.

#### BernsteinConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `epsilon` | `double` | `0.1` | Approximation factor. Results within (1+epsilon) of exact. |

```cpp
FragilityResult bernstein_approx(const ArrayGraph& graph,
                                  NodeID source, NodeID target,
                                  BernsteinConfig config = {});
```

**Algorithm:**
1. Compute SPT from s, extract s-t path.
2. Partition path into O(log n / epsilon) geometrically increasing segments.
3. For each segment boundary, compute detour distances using backward Dijkstra.
4. Combine to get approximate replacement distances.

**Complexity:** O((m + n log n) * log n / epsilon).
**Accuracy:** (1+epsilon)-approximate. All replacement distances are within (1+epsilon) of exact.

---

## 5. Alternative Routes

Defined in `gravel/fragility/via_path.h`.

### ViaPathConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `max_stretch` | `double` | `1.25` | Max ratio of alternative distance to primary distance. |
| `max_sharing` | `double` | `0.80` | Max fraction of edges shared with primary path. |
| `max_alternatives` | `uint32_t` | `3` | Maximum number of alternative routes to return. |

```cpp
std::vector<AlternateRouteResult> find_alternative_routes(
    const ContractionResult& ch,
    NodeID source, NodeID target,
    ViaPathConfig config = {});
```

**Algorithm** (Abraham et al. via-path method):
1. Full forward CH search from s (no early termination).
2. Full backward CH search from t.
3. For each node v settled in both where `dist(s,v) + dist(v,t) <= max_stretch * d*`:
   - Skip if v is on the primary path.
   - Compute path s->v->t, check sharing and stretch.
   - Keep if sharing < max_sharing.
4. Return top-k by stretch.

**Complexity:** O(V + E_overlay) for the two full CH searches, plus O(path_length) per candidate.

---

## 6. Network Analysis

### Algebraic Connectivity

Defined in `gravel/analysis/algebraic_connectivity.h`.

```cpp
double algebraic_connectivity(const ArrayGraph& graph);
```

Computes the Fiedler value (second smallest eigenvalue of the graph Laplacian).

**Inputs:** ArrayGraph.
**Outputs:** `double`. Returns 0.0 for disconnected graphs.
**Complexity:** O(V^2) for eigendecomposition (uses Spectra library).
**Scale:** County/state only. National scale is infeasible.

### Edge Betweenness Centrality

Defined in `gravel/analysis/betweenness.h`.

#### BetweennessConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `sample_sources` | `uint32_t` | `0` | Number of source nodes to sample. 0 = exact (all sources). |
| `range_limit` | `double` | `0.0` | Dijkstra cutoff distance. 0 = unlimited. |
| `seed` | `uint64_t` | `42` | Random seed for source sampling. |

#### BetweennessResult

| Field | Type | Description |
|-------|------|-------------|
| `edge_scores` | `vector<double>` | Betweenness score per edge (indexed by CSR position). |
| `sources_used` | `uint32_t` | Number of source nodes actually processed. |

```cpp
BetweennessResult edge_betweenness(const ArrayGraph& graph,
                                    BetweennessConfig config = {});
```

**Algorithm:** Brandes' algorithm.
**Complexity:** Exact: O(V * (V + E)). Sampled: O(sample_sources * (V + E)). Range-limited cuts Dijkstra at `range_limit` distance.
**Parallelism:** OpenMP-parallelized over source nodes.
**Scale:** Exact for county scale, sampling-based for state/national.

### Kirchhoff Index

Defined in `gravel/analysis/kirchhoff.h`.

#### KirchhoffConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `num_probes` | `uint32_t` | `30` | Hutchinson's estimator probe count. |
| `seed` | `uint64_t` | `42` | Random seed. |

```cpp
double kirchhoff_index(const ArrayGraph& graph, KirchhoffConfig config = {});
```

Computes R_G = n * trace(L^+), where L^+ is the pseudoinverse of the graph Laplacian.

**Algorithm:** Stochastic trace estimation (Hutchinson's method) for large graphs. Direct computation for small graphs (<1000 nodes).
**Complexity:** O(num_probes * solver_cost). Solver cost depends on graph structure.

### Natural Connectivity

Defined in `gravel/analysis/natural_connectivity.h`.

```cpp
double natural_connectivity(const ArrayGraph& graph,
                             uint32_t num_probes = 20,
                             uint32_t lanczos_steps = 50,
                             uint64_t seed = 42);
```

Computes the natural connectivity: the logarithm of the average of the eigenvalues of the adjacency matrix exponentiated.

Formula: `lambda_bar = ln((1/n) * sum(exp(lambda_i)))`.

**Algorithm:** Stochastic Lanczos quadrature. Approximates the spectral sum without computing individual eigenvalues.
**Parameters:**
- `num_probes` (default 20): Number of random probes for stochastic estimation.
- `lanczos_steps` (default 50): Lanczos iteration depth.
- `seed` (default 42): Random seed.

**Complexity:** O(num_probes * lanczos_steps * E).

---

## 7. County Fragility Index

Defined in `gravel/analysis/county_fragility.h`. Computes a composite fragility score for a geographic region defined by a polygon boundary.

### CountyFragilityWeights

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `bridge_weight` | `double` | `0.30` | Weight for bridge density metric. |
| `connectivity_weight` | `double` | `0.25` | Weight for algebraic connectivity metric. |
| `accessibility_weight` | `double` | `0.25` | Weight for external accessibility metric. |
| `fragility_weight` | `double` | `0.20` | Weight for sampled route fragility metric. |

**Composite scoring formula:**

```
composite_index = bridge_weight * normalized_bridge_density
                + connectivity_weight * (1 - normalized_algebraic_connectivity)
                + accessibility_weight * (1 - normalized_accessibility)
                + fragility_weight * normalized_mean_fragility
```

All sub-metrics are normalized to [0, 1]. Higher composite index = more fragile.

### CountyFragilityConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `boundary` | `Polygon` | (required) | Geographic polygon defining the county/region. |
| `betweenness_samples` | `uint32_t` | `0` | Betweenness source samples. 0 = exact. |
| `od_sample_count` | `uint32_t` | `100` | O-D pairs for route fragility sampling. |
| `seed` | `uint64_t` | `42` | Random seed. |
| `weights` | `CountyFragilityWeights` | (see above) | Composite scoring weights. |

### CountyFragilityResult

| Field | Type | Description |
|-------|------|-------------|
| `composite_index` | `double` | Final composite fragility score [0, 1]. |
| `bridges` | `BridgeResult` | All bridges in the subgraph. |
| `betweenness` | `BetweennessResult` | Edge betweenness centrality. |
| `algebraic_connectivity` | `double` | Fiedler value of the subgraph. |
| `kirchhoff_index_value` | `double` | Kirchhoff index of the subgraph. |
| `accessibility` | `AccessibilityResult` | External accessibility analysis. |
| `sampled_fragilities` | `vector<FragilityResult>` | Per-route fragility for sampled O-D pairs. |
| `subgraph_nodes` | `uint32_t` | Node count of the extracted subgraph. |
| `subgraph_edges` | `uint32_t` | Edge count of the extracted subgraph. |
| `entry_point_count` | `uint32_t` | Number of entry/exit points to the subgraph. |

```cpp
CountyFragilityResult county_fragility_index(
    const ArrayGraph& full_graph,
    const ContractionResult& full_ch,
    const ShortcutIndex& full_idx,
    const CountyFragilityConfig& config);
```

**Pipeline:**
1. Extract subgraph within polygon boundary.
2. Find bridges (Tarjan's O(V+E)).
3. Compute algebraic connectivity.
4. Compute Kirchhoff index.
5. Compute edge betweenness centrality.
6. Analyze external accessibility (entry points, corridor fragility).
7. Sample O-D pairs and compute route fragility.
8. Combine into weighted composite score.

### Accessibility Analysis

Defined in `gravel/analysis/accessibility.h`.

#### EntryPoint

```cpp
struct EntryPoint {
    NodeID original_id = INVALID_NODE;
    NodeID subgraph_id = INVALID_NODE;
    std::vector<NodeID> external_neighbors;  // in original graph
};
```

#### AccessibilityResult

```cpp
struct AccessibilityResult {
    std::vector<EntryPoint> entry_points;
    std::vector<FragilityResult> corridor_fragilities;  // between entry point pairs
    double accessibility_score = 0.0;                   // aggregate metric
};
```

```cpp
AccessibilityResult analyze_accessibility(
    const ArrayGraph& full_graph,
    const ContractionResult& full_ch,
    const ShortcutIndex& full_idx,
    const SubgraphResult& subgraph);
```

Identifies entry points (subgraph nodes with external neighbors) and measures fragility of corridors between entry point pairs.

### O-D Sampling

Defined in `gravel/analysis/od_sampling.h`.

#### SamplingConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `total_samples` | `uint32_t` | `1000` | Total O-D pairs to generate. |
| `distance_strata` | `uint32_t` | `10` | Number of distance decile strata. |
| `long_distance_weight` | `double` | `3.0` | Over-sampling factor for long-distance pairs. |
| `long_distance_threshold` | `double` | `0.0` | Auto-detected if 0. |
| `seed` | `uint64_t` | `42` | Random seed. |

```cpp
std::vector<std::pair<NodeID, NodeID>> stratified_sample(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    SamplingConfig config = {});
```

Generates stratified O-D pairs for network-level fragility analysis. Stratifies by shortest-path distance to ensure coverage of both short/local and long/cross-region routes. Long-distance pairs are over-sampled by `long_distance_weight`.

---

## 8. Location Fragility

Defined in `gravel/analysis/location_fragility.h`. Measures isolation risk for a specific geographic location using Dijkstra + incremental SSSP on a simplified local subgraph. Produces a degradation curve showing how isolation worsens as edges are removed.

### LocationFragilityConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `center` | `Coord` | (required) | Geographic center point. |
| `radius_meters` | `double` | `80467.0` | Search radius (50 miles default). |
| `angular_bins` | `uint32_t` | `8` | Compass sectors for directional coverage. |
| `removal_fraction` | `float` | `0.10` | Fraction of shortest-path edges to remove (10%). |
| `sample_count` | `uint32_t` | `200` | Target nodes sampled for scoring. |
| `strategy` | `SelectionStrategy` | `MONTE_CARLO` | Edge selection strategy (see below). |
| `monte_carlo_runs` | `uint32_t` | `20` | MC trials (MONTE_CARLO only). |
| `betweenness_samples` | `uint32_t` | `100` | Betweenness sample sources (GREEDY_BETWEENNESS only). |
| `seed` | `uint64_t` | `42` | Random seed. |

### Selection Strategies

| Strategy | Description | Use Case |
|----------|-------------|----------|
| `MONTE_CARLO` | Random shuffle of SP edges, N independent trials. Default. | Cross-county comparative research. |
| `GREEDY_BETWEENNESS` | Remove highest-betweenness SP edges first. | High-traffic corridor failure. |
| `GREEDY_FRAGILITY` | Remove edge causing worst isolation per step. | Worst-case adversarial analysis. |

### LocationFragilityResult

| Field | Type | Description |
|-------|------|-------------|
| `isolation_risk` | `double` | Peak isolation risk at maximum removal [0, 1]. |
| `curve` | `vector<LocationKLevel>` | Degradation curve: curve[0] = max removal, curve[k_max] = restored. |
| `auc_normalized` | `double` | AUC of mean isolation risk, normalized by curve length. |
| `baseline_isolation_risk` | `double` | Isolation risk with no edges removed (~0). |
| `directional_coverage` | `double` | Fraction of sectors reachable at max removal [0, 1]. |
| `directional_fragility` | `vector<double>` | Per-bin mean distance inflation. |
| `directional_asymmetry` | `double` | HHI of directional fragility concentration. |
| `removal_sequence` | `vector<pair<NodeID,NodeID>>` | Ordered removal (greedy strategies only). |
| `reachable_nodes` | `uint32_t` | Nodes reachable at baseline. |
| `sp_edges_total` | `uint32_t` | Shortest-path edges identified. |
| `sp_edges_removed` | `uint32_t` | Edges removed (k_max). |
| `subgraph_nodes` | `uint32_t` | Simplified subgraph node count. |
| `subgraph_edges` | `uint32_t` | Simplified subgraph edge count. |

```cpp
LocationFragilityResult location_fragility(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const LocationFragilityConfig& config);
```

### Algorithm

1. Extract local subgraph within radius, simplify with `contract_degree2` (92% reduction).
2. CH distances from source to all simplified nodes.
3. Identify shortest-path edges via DAG criterion: edge (u,v,w) is on SP if `dist[u]+w = dist[v]`.
4. Remove `removal_fraction` of SP edges per strategy.
5. Run `IncrementalSSSP` on simplified graph — one blocked Dijkstra + incremental restores.
6. Score at each k-level using sampled target nodes only (not all nodes).

### Composite Score Formula

```
disconnected_frac = unreachable_nodes / reachable_baseline
inflation = mean(dist_blocked / dist_full) for connected nodes, clamped to [1, 6]
normalized_inflation = (inflation - 1) / 5
coverage_gap = 1 - (sectors_reachable / angular_bins)

isolation_risk = 0.5 * disconnected_frac + 0.3 * normalized_inflation + 0.2 * coverage_gap
```

### Performance

| Graph Size | Time (MC x20) | Notes |
|------------|---------------|-------|
| Swain County (200K nodes) | ~2s | Simplified to 14K nodes internally |
| 7x7 grid (49 nodes) | <1ms | No simplification needed |

---

## 9. Graph Simplification

Defined in `gravel/simplify/simplify.h` and `gravel/simplify/edge_labels.h`.

Three composable, independently optional stages that reduce graph size:

### Stage 1: Degree-2 Contraction (Lossless)

Merges chains of degree-2 nodes into single edges. Preserves all shortest-path distances exactly.
**Typical reduction:** 25-35% of nodes.
**Error:** Exactly 0%.

### Stage 2: Edge Category Filtering

Removes edges that fail a user-defined predicate. OSM road class filtering is the most common use case.
**Typical reduction:** 60-75% when dropping residential/service roads.

### Stage 3: CH-Level Pruning

Removes structurally unimportant nodes based on contraction hierarchy levels. Bridges are always preserved (unless `preserve_bridges = false`).
**Typical reduction:** 20-30% additional.
**Requires:** Pre-built CH.

### SimplificationConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `contract_degree2` | `bool` | `true` | Enable degree-2 chain contraction (lossless). |
| `edge_filter` | `function<bool(uint32_t)>` | `nullptr` | Predicate: given CSR edge index, return true to KEEP. Runs on original graph indices. |
| `ch_level_keep_fraction` | `double` | `1.0` | Fraction of nodes to keep by CH level. 1.0 = disabled, 0.5 = top 50%, 0.3 = top 30%. |
| `preserve_bridges` | `bool` | `true` | Never prune bridge endpoints regardless of CH level. |
| `estimate_degradation` | `bool` | `true` | Run degradation estimation after simplification. |
| `degradation_samples` | `uint32_t` | `1000` | O-D pairs for degradation estimation. |
| `seed` | `uint64_t` | `42` | Random seed. |

**Stage execution order:** edge filtering -> degree-2 contraction -> CH-level pruning.

### DegradationReport

| Field | Type | Description |
|-------|------|-------------|
| `od_pairs_sampled` | `uint32_t` | Number of O-D pairs tested. |
| `max_stretch` | `double` | Maximum stretch factor (d_simplified / d_original). |
| `p99_stretch` | `double` | 99th percentile stretch. |
| `p95_stretch` | `double` | 95th percentile stretch. |
| `p90_stretch` | `double` | 90th percentile stretch. |
| `median_stretch` | `double` | Median stretch. |
| `mean_stretch` | `double` | Mean stretch. |
| `pairs_connected_before` | `uint32_t` | Pairs reachable in original graph. |
| `pairs_connected_after` | `uint32_t` | Pairs reachable in simplified graph. |
| `pairs_disconnected` | `uint32_t` | Pairs reachable before but not after. |
| `connectivity_ratio` | `double` | `pairs_connected_after / pairs_connected_before`. |
| `original_bridges` | `uint32_t` | Bridge count in original graph. |
| `preserved_bridges` | `uint32_t` | Bridge count preserved in simplified graph. |
| `all_bridges_preserved` | `bool` | True if every bridge was preserved. |
| `stages` | `vector<StageReport>` | Per-stage node/edge counts. |

All stretch values are ratios >= 1.0 (1.0 = no degradation).

### SimplificationResult

| Field | Type | Description |
|-------|------|-------------|
| `graph` | `shared_ptr<ArrayGraph>` | The simplified graph. |
| `new_to_original` | `vector<NodeID>` | Simplified node ID -> original node ID. |
| `original_to_new` | `unordered_map<NodeID, NodeID>` | Original -> simplified mapping. |
| `original_nodes` | `uint32_t` | Original node count. |
| `original_edges` | `uint32_t` | Original edge count. |
| `simplified_nodes` | `uint32_t` | Simplified node count. |
| `simplified_edges` | `uint32_t` | Simplified edge count. |
| `degradation` | `DegradationReport` | Populated if `estimate_degradation` was true. |

```cpp
SimplificationResult simplify_graph(
    const ArrayGraph& graph,
    const ContractionResult* ch = nullptr,
    const ShortcutIndex* idx = nullptr,
    SimplificationConfig config = {});
```

### Edge Category Labels

Defined in `gravel/simplify/edge_labels.h`.

#### EdgeCategoryLabels

```cpp
struct EdgeCategoryLabels {
    std::vector<uint8_t> categories;  // one per directed edge in CSR order

    static EdgeCategoryLabels from_strings(
        const std::vector<std::string>& labels,
        const std::unordered_map<std::string, uint8_t>& rank_map,
        uint8_t default_rank = 255);

    static std::unordered_map<std::string, uint8_t> osm_road_ranks();
};
```

**OSM road class ranking** (lower = more important):

| Rank | Road classes |
|------|-------------|
| 0 | motorway, motorway_link |
| 1 | trunk, trunk_link |
| 2 | primary, primary_link |
| 3 | secondary, secondary_link |
| 4 | tertiary, tertiary_link |
| 5 | residential, unclassified |
| 6 | service, living_street |

```cpp
std::function<bool(uint32_t)> make_category_filter(
    const EdgeCategoryLabels& labels, uint8_t max_category);
```

Creates a predicate that keeps edges with category <= `max_category`. For example, `make_category_filter(labels, 4)` keeps tertiary roads and above.

### Internal Stage Functions

Exposed for testing:

```cpp
SimplificationResult contract_degree2(
    const ArrayGraph& graph,
    const std::unordered_set<NodeID>& bridge_endpoints = {});

SimplificationResult prune_by_ch_level(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    double keep_fraction,
    const std::unordered_set<NodeID>& bridge_endpoints = {});

SimplificationResult filter_edges(
    const ArrayGraph& graph,
    const std::function<bool(uint32_t)>& predicate);
```

---

## 10. Elevation and Closure Risk

### Elevation Data

Defined in `gravel/geo/elevation.h`.

#### ElevationData

```cpp
struct ElevationData {
    std::vector<double> node_elevation;  // meters, one per node (NaN if unknown)

    double edge_max_elevation(NodeID u, NodeID v) const;
    bool has_elevation(NodeID node) const;
};
```

**Functions:**

```cpp
// Load from SRTM HGT tile directory. Graph must have coordinates.
ElevationData load_srtm_elevation(const ArrayGraph& graph, const std::string& srtm_dir);

// From flat array (testing / external sources).
ElevationData elevation_from_array(std::vector<double> elevations);

// Serialization
void save_elevation(const ElevationData& elev, const std::string& path);
ElevationData load_elevation(const std::string& path);
```

SRTM tiles are 1-arc-second resolution HGT files named by their southwest corner (e.g., `N35W084.hgt`). The loader interpolates elevation for each graph node coordinate.

### Closure Risk

Defined in `gravel/analysis/closure_risk.h`. Classifies road segments by closure likelihood based on elevation and road class.

#### ClosureRiskTier

| Tier | Value | Criteria | Description |
|------|-------|----------|-------------|
| `LOW` | 0 | Low elevation, major road | Rarely closes. |
| `MODERATE` | 1 | Moderate elevation OR minor road | Occasional closures. |
| `HIGH` | 2 | High elevation AND secondary road | Seasonal closures. |
| `SEVERE` | 3 | Very high elevation AND minor road | Frequent/prolonged closures. |

#### ClosureRiskConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `moderate_elevation` | `double` | `900.0` | Meters (~3000 ft). |
| `high_elevation` | `double` | `1200.0` | Meters (~4000 ft). |
| `severe_elevation` | `double` | `1500.0` | Meters (~5000 ft). |
| `road_class_rank` | `map<string, uint8_t>` | (see defaults) | Road class name to rank. Unmapped edges default to rank 5. |

Default road class ranks match OSM conventions (motorway=0 through service=6).

#### ClosureRiskData

```cpp
struct ClosureRiskData {
    std::vector<ClosureRiskTier> edge_tiers;  // one per directed edge

    double tier_fraction(ClosureRiskTier tier) const;
    ClosureRiskTier max_tier_on_path(const std::vector<uint32_t>& edge_indices) const;
};
```

**Functions:**

```cpp
ClosureRiskData classify_closure_risk(
    const ArrayGraph& graph,
    const ElevationData& elevation,
    const std::vector<std::string>& edge_labels = {},
    ClosureRiskConfig config = ClosureRiskConfig::defaults());
```

```cpp
std::vector<double> seasonal_weight_multipliers(
    const ClosureRiskData& risk,
    double tier1_multiplier = 1.0,    // moderate: no penalty
    double tier2_multiplier = 1.5,    // high: 50% penalty
    double tier3_multiplier = 3.0);   // severe: 3x penalty
```

Returns a per-edge weight multiplier for seasonal (winter) analysis. Apply these to edge weights to simulate closure-prone conditions.

---

## 11. Coordinate Snapping

Defined in `gravel/snap/snapper.h` and `gravel/snap/edge_index.h`. Maps arbitrary geographic coordinates to the nearest road edge.

### EdgeIndex (R-tree)

Defined in `gravel/snap/edge_index.h`. Lightweight STR-packed R-tree over edge bounding boxes.

```cpp
class EdgeIndex {
public:
    static EdgeIndex build(const std::vector<Coord>& coords,
                           const std::vector<uint32_t>& offsets,
                           const std::vector<NodeID>& targets);

    std::vector<Candidate> query_nearest(Coord point, size_t k = 16) const;
    size_t size() const;

    void save(const std::string& path) const;    // .gravel.rtree format
    static EdgeIndex load(const std::string& path);
};
```

Bulk-loaded once, queried many times. No dynamic insertions. Candidates are sorted by bounding-box lower-bound distance.

### SnapResult

```cpp
struct SnapResult {
    NodeID edge_source = INVALID_NODE;
    NodeID edge_target = INVALID_NODE;
    double t = 0.0;                    // [0,1] along edge
    Coord snapped_coord = {};
    Weight dist_to_source = 0.0;       // t * edge_weight
    Weight dist_to_target = 0.0;       // (1-t) * edge_weight
    double snap_distance_m = 0.0;      // perpendicular distance in meters
    bool is_exact_node = false;        // t ~ 0 or t ~ 1
    Weight edge_weight = 0.0;          // full edge weight

    bool valid() const;
};
```

A `SnapResult` represents a phantom node on an edge. It splits the edge into two virtual segments with weights `dist_to_source` and `dist_to_target`. The `CHQuery` accepts `SnapResult` directly for phantom-to-phantom routing.

### SnapQualityReport

```cpp
struct SnapQualityReport {
    uint32_t total = 0;
    uint32_t succeeded = 0;
    uint32_t failed = 0;           // no edge within radius
    uint32_t exact_node = 0;       // snapped to existing node
    uint32_t warned = 0;           // snap_distance > warn_threshold

    double p50_distance_m = 0.0;
    double p90_distance_m = 0.0;
    double p95_distance_m = 0.0;
    double p99_distance_m = 0.0;
    double max_distance_m = 0.0;
};
```

```cpp
SnapQualityReport snap_quality(const std::vector<SnapResult>& results,
                                double warn_threshold_m = 200.0);
```

### Snapper

```cpp
class Snapper {
public:
    explicit Snapper(const ArrayGraph& graph);  // graph must have coordinates

    SnapResult snap(Coord point, double max_distance_m = 500.0) const;
    std::vector<SnapResult> snap_batch(const std::vector<Coord>& points,
                                        double max_distance_m = 500.0) const;
};
```

**Algorithm:**
1. Query R-tree for k=16 nearest edge candidates by bounding-box distance.
2. For each candidate, project the query point onto the edge segment (great-circle).
3. Return the edge with minimum perpendicular distance.

**Returns:** Invalid `SnapResult` if no edge is within `max_distance_m`.

---

## 12. Filter Pipeline

Defined in `gravel/fragility/fragility_filter.h`. Accelerates route fragility computation by screening out edges that can be proven non-critical without expensive blocked queries.

### FilterConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `ch_level_percentile` | `double` | `0.80` | Top 20% of CH levels pass screening (edges at lower CH levels are screened). |
| `skip_ratio_threshold` | `double` | `1.05` | Skip if ALT lower bound proves ratio < this threshold. |
| `use_ch_level_filter` | `bool` | `true` | Enable CH level screening stage. |
| `use_alt_filter` | `bool` | `true` | Enable ALT lower bound stage. |

### FilteredFragilityResult

Extends `FragilityResult` with filter statistics:

| Field | Type | Description |
|-------|------|-------------|
| `edges_screened` | `uint32_t` | Edges skipped by filters (no expensive computation needed). |
| `edges_computed` | `uint32_t` | Edges requiring exact blocked CH query. |

```cpp
FilteredFragilityResult filtered_route_fragility(
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const ArrayGraph& graph,
    const LandmarkData& landmarks,
    NodeID source, NodeID target,
    FilterConfig config = {});
```

**Pipeline stages (in order):**

1. **Bridge detection:** Edges that are bridges are immediately assigned infinite fragility ratio (no replacement path exists).
2. **CH level screening:** Edges whose both endpoints have CH level below the `ch_level_percentile` threshold are screened. Low-level CH nodes are structurally unimportant and likely have easy replacements.
3. **ALT lower bound:** Uses precomputed landmark distances to compute a lower bound on the replacement distance. If the lower bound proves the fragility ratio is below `skip_ratio_threshold`, the edge is skipped.
4. **Exact blocked query:** Remaining edges go through the full `BlockedCHQuery` for exact computation.

---

## 13. Landmarks (ALT Lower Bounds)

Defined in `gravel/algo/landmarks.h`.

### LandmarkData

```cpp
struct LandmarkData {
    uint32_t num_landmarks = 0;
    std::vector<std::vector<Weight>> dist_from;  // dist_from[l][v] = d(landmark_l, v)
    std::vector<std::vector<Weight>> dist_to;    // dist_to[l][v] = d(v, landmark_l)

    Weight lower_bound(NodeID s, NodeID t) const;
};
```

The `lower_bound` function computes: `max over landmarks l of |d(l,s) - d(l,t)|`.

```cpp
LandmarkData precompute_landmarks(const ArrayGraph& graph,
                                   uint32_t num_landmarks = 16,
                                   uint64_t seed = 42);
```

**Algorithm:** Farthest-first selection. Picks landmark nodes that are maximally spread out, then runs one full Dijkstra per landmark (forward and backward).
**Complexity:** O(num_landmarks * (V + E log V)).
**Memory:** O(num_landmarks * V * 2) weights (two distance arrays per landmark).
**Typical value:** 16 landmarks.

---

## 14. Geographic Utilities

Defined in `gravel/geo/geo_math.h`.

```cpp
double haversine_meters(Coord a, Coord b);
```

Great-circle distance between two coordinates in meters.

```cpp
bool point_in_polygon(Coord point, const std::vector<Coord>& polygon);
```

Ray-casting point-in-polygon test. Polygon is a closed ring (first == last) or auto-closed.

```cpp
struct ProjectionResult {
    Coord projected;       // closest point on segment
    double t;              // parameter along segment [0,1], clamped
    double distance_m;     // perpendicular distance in meters
};

ProjectionResult project_to_segment(Coord point, Coord seg_a, Coord seg_b);
```

Projects a point onto a line segment. Returns the closest point, the parameter t in [0,1], and the perpendicular distance in meters.

---

## 15. CLI Reference

Binary: `gravel`. All commands output JSON to stdout. Logs and progress go to stderr.

### Preprocessing

#### `build-graph`

Convert CSV to `.gravel.meta` binary format.

```
gravel build-graph --csv <file.csv> --output <file.gravel.meta>
    [--source-col <name>]      Column name for source (default: "source")
    [--target-col <name>]      Column name for target (default: "target")
    [--weight-col <name>]      Column name for weight (default: "weight")
    [--delimiter <char>]       Column delimiter (default: ",")
    [--bidirectional]          Add reverse edges
```

#### `build-ch`

Build contraction hierarchy from a `.gravel.meta` graph.

```
gravel build-ch --input <file.gravel.meta> --output <file.gravel.ch>
```

Shows progress percentage on stderr.

### Routing

#### `route`

Query a single shortest path.

```
gravel route --graph <.gravel.meta> --ch <.gravel.ch>
             --from <node_id> --to <node_id>
```

**Output:** `{ "from", "to", "distance", "path" }`. Distance is null if no path exists.

#### `matrix`

Compute distance matrix.

```
gravel matrix --ch <.gravel.ch>
              --origins <id,id,...> --destinations <id,id,...>
```

**Output:** `{ "origins", "destinations", "distances" }`. Distances are a 2D array; -1.0 for unreachable pairs.

#### `snap`

Snap a coordinate to the nearest road edge.

```
gravel snap --graph <.gravel.meta> --lat <lat> --lon <lon>
            [--max-distance <meters>]     Default: 500
```

**Output:** `{ "snapped_lat", "snapped_lon", "snap_distance_meters", "edge_source", "edge_target", "t", "is_exact_node" }`.

### Fragility

#### `route-fragility`

Edge fragility for every edge on the shortest s-t path.

```
gravel route-fragility --graph <.gravel.meta> --ch <.gravel.ch>
                       --from <node_id> --to <node_id>
```

**Output:** `{ "primary_distance", "primary_path", "edge_fragilities": [...], "bottleneck": {...} }`.

#### `batch-fragility`

Batch fragility for multiple O-D pairs.

```
gravel batch-fragility --graph <.gravel.meta> --ch <.gravel.ch>
                       --pairs <pairs.csv>
```

`pairs.csv` format: one `source,target` per line. Lines starting with `#` are comments.

**Output:** JSON array with per-pair `{ "from", "to", "primary_distance", "bottleneck_edge", "bottleneck_ratio", "num_path_edges" }`.

#### `county-fragility`

Full county-level fragility analysis.

```
gravel county-fragility --graph <.gravel.meta> --ch <.gravel.ch>
                        --polygon <geojson>
    [--samples <n>]              O-D sample count (default: 100)
    [--betweenness-samples <n>]  Betweenness sources, 0=exact (default: 0)
    [--seed <n>]                 Random seed (default: 42)
```

`--polygon` is a GeoJSON Feature file with `geometry.coordinates[0]` containing the ring. Coordinates are [lon, lat] per GeoJSON convention.

**Output:** `{ "composite_index", "subgraph_nodes", "subgraph_edges", "entry_point_count", "bridges_count", "algebraic_connectivity", "kirchhoff_index", "accessibility_score" }`.

#### `county-index`

Outputs just the composite index number (no JSON). Suitable for piping.

```
gravel county-index --graph <.gravel.meta> --ch <.gravel.ch>
                    --polygon <geojson>
    [--samples <n>]   [--seed <n>]
```

**Output:** A single floating-point number on stdout.

#### `location-fragility`

Isolation risk for a specific location.

```
gravel location-fragility --graph <.gravel.meta> --ch <.gravel.ch>
                          --lat <latitude> --lon <longitude>
    [--radius-miles <n>]   Search radius in miles (default: 50)
    [--samples <n>]        Destination samples (default: 100)
    [--seed <n>]           Random seed (default: 42)
```

**Output:** `{ "isolation_risk", "reachable_samples", "total_samples", "bridge_dependent_routes", "median_fragility_ratio", "p90_fragility_ratio", "max_fragility_ratio", "directional_coverage", "local_bridges_count", "algebraic_connectivity" }`.

### Simplification

#### `simplify`

Reduce graph size with configurable stages and degradation estimation.

```
gravel simplify --graph <.gravel.meta> [options]

Stages:
    --contract-degree2          Merge degree-2 chains (lossless, default: on)
    --no-degree2                Disable degree-2 contraction
    --ch <.gravel.ch>          Required for CH pruning and degradation
    --ch-keep-fraction <F>      Keep top F by CH level (e.g., 0.7 = top 70%)
    --no-preserve-bridges       Allow pruning bridge endpoints

Degradation:
    --degradation-samples <N>   O-D pairs to sample (default: 1000)
    --no-degradation            Skip degradation estimation
    --seed <N>                  Random seed (default: 42)

Output:
    --output <path>             Save simplified graph to .gravel.meta
```

**Output:** JSON with `{ "original_nodes", "original_edges", "simplified_nodes", "simplified_edges", "node_reduction_pct", "edge_reduction_pct", "degradation": {...} }`.

### Validation

#### `validate`

Validate CH correctness against reference Dijkstra.

```
gravel validate --graph <.gravel.meta> --ch <.gravel.ch>
    [--mode sampled|exhaustive]   Default: sampled
    [--pairs <n>]                 Number of pairs for sampled mode (default: 10000)
```

**Output:** `{ "passed", "pairs_tested", "mismatches", "max_absolute_error", "failures": [...] }`.

---

## 16. Scale Limits and Constraints

### Node and Edge Limits

| Constraint | Limit | Reason |
|------------|-------|--------|
| Max nodes | 2^32 - 2 (4.29 billion) | `NodeID = uint32_t`, `INVALID_NODE` reserves max value. |
| Max edges | 2^32 - 1 (4.29 billion) | `EdgeID = uint32_t`. |
| Practical node limit | ~50 million | Memory and CH build time. |

### Memory Formulas

**ArrayGraph:**
```
offsets:  (V + 1) * 4 bytes
targets:  E * 4 bytes
weights:  E * 8 bytes
coords:   V * 16 bytes  (if present)
Total:    ~12E + 20V bytes
```

**ContractionResult (additional to graph):**
```
up/down graphs:   ~2 * (V_overlay + 1) * 4 + 2 * E_overlay * (4 + 8 + 4) bytes
node_levels:      V * 4 bytes
order:            V * 4 bytes
unpack_map:       ~E_overlay * 16 bytes (hash map overhead)
```

E_overlay is typically 3-5x the original edge count.

**ShortcutIndex:**
```
~E_overlay * 24 bytes (reverse map entries + vectors)
```

**LandmarkData:**
```
2 * num_landmarks * V * 8 bytes
Example: 16 landmarks, 1M nodes = 256 MB
```

### Algorithm Complexity

| Algorithm | Time | Space | Scale Suitability |
|-----------|------|-------|------------------|
| CH build | O(V log V * witness_limit) | O(V + E_overlay) | Continental |
| CH query | O(k), k ~ 1000 | O(V) workspace | Continental |
| CH distance matrix | O(\|O\| * \|D\| * k) | O(V) per thread | Continental |
| Route fragility | O(path_len * k) | O(V + E_overlay) | Continental |
| Batch fragility | O(pairs * path_len * k) | O(V + E_overlay) per thread | Continental |
| Hershberger-Suri | O(m + n log n) | O(V + E) | County/state |
| Bernstein approx | O((m + n log n) * log n / eps) | O(V + E) | County/state |
| Bridges (Tarjan) | O(V + E) | O(V + E) | Any |
| Algebraic connectivity | O(V^2) eigendecomposition | O(V^2) | County only |
| Edge betweenness (exact) | O(V * (V + E)) | O(V + E) | County only |
| Edge betweenness (sampled) | O(samples * (V + E)) | O(V + E) | State/national |
| Kirchhoff index | O(probes * solver) | O(V^2) for direct, O(V) for stochastic | County/state |
| Natural connectivity | O(probes * steps * E) | O(V * steps) | State |
| Landmarks precompute | O(L * (V + E log V)) | O(L * V) | Continental |
| Snap (single) | O(log E) R-tree + O(k) projection | O(E) for index | Continental |
| Simplification | O(V + E) per stage | O(V + E) | Continental |

### Spectral Metric Limits

The spectral metrics (algebraic connectivity, Kirchhoff index, natural connectivity) use Eigen/Spectra for dense eigendecomposition or Lanczos iteration. Practical limits:

| Metric | Direct computation | Stochastic approximation |
|--------|--------------------|--------------------------|
| Algebraic connectivity | < ~50,000 nodes | Not available (uses Spectra) |
| Kirchhoff index | < 1,000 nodes (direct) | Any size (Hutchinson's) |
| Natural connectivity | N/A | Any size (Lanczos quadrature) |

For county-level analysis (typically 5,000-50,000 nodes after subgraph extraction), all spectral metrics are feasible. For state-scale or larger, only the stochastic approximations (Kirchhoff, natural connectivity) and sampled betweenness are practical.

---

## 17. v2.0 Enhancements

The following features were added in v2.0 to address methodological gaps identified during research use of v1.x. Each section explains the research motivation ("why") alongside the API.

---

### 17.1 Fragility Distribution Statistics

Defined in `gravel/analysis/county_fragility.h`. New fields on `CountyFragilityResult`.

**Why:** The v1.x composite index reduces a county's fragility to a single number, but the *shape* of the fragility distribution matters. A county where the median bottleneck ratio is 1.1 but the 99th percentile is 15.0 has a few catastrophically fragile routes hidden inside an otherwise resilient network. These tail routes are precisely the ones that fail during disasters. Percentile fields expose the distribution so researchers can distinguish "uniformly moderate fragility" from "mostly fine with a few catastrophic routes."

#### New Result Fields

| Field | Type | Description |
|-------|------|-------------|
| `fragility_p25` | `double` | 25th percentile bottleneck ratio across sampled O-D pairs. |
| `fragility_p50` | `double` | Median bottleneck ratio. The "typical" route fragility. |
| `fragility_p75` | `double` | 75th percentile. |
| `fragility_p90` | `double` | 90th percentile -- the "bad day" routes. |
| `fragility_p99` | `double` | 99th percentile -- worst-case routes in the sample. |

These are computed from the `sampled_fragilities` vector and populated automatically by `county_fragility_index()`.

#### Interpretation Guide

- **p50 near 1.0:** Most routes have good alternatives. The network is resilient for typical travel.
- **p90 >> p50:** A meaningful fraction of routes become much longer when their bottleneck fails. Indicates pockets of vulnerability.
- **p99 = INF:** At least 1% of sampled routes cross a bridge with no alternative. These routes become impossible if the bridge fails.
- **p90 close to p50:** Fragility is evenly distributed. No hidden tail risk.

#### Example

```cpp
auto result = county_fragility_index(graph, ch, idx, config);

// Distribution reveals hidden structure
std::cout << "Median bottleneck ratio: " << result.fragility_p50 << "\n";
std::cout << "90th percentile: " << result.fragility_p90 << "\n";
std::cout << "99th percentile: " << result.fragility_p99 << "\n";

// Flag counties with hidden tail risk
if (result.fragility_p99 > 5.0 * result.fragility_p50) {
    std::cout << "WARNING: tail routes are dramatically more fragile than typical\n";
}
```

---

### 17.2 Directional Asymmetry

Defined in `gravel/analysis/location_fragility.h`. New fields on `LocationFragilityResult`.

**Why:** A location with isolation_risk=0.4 might be equally fragile in all directions (uniform risk) or extremely fragile northward but resilient southward (asymmetric risk). For evacuation planning, the difference is critical: asymmetric fragility means some escape directions are far more vulnerable than others. The Herfindahl-Hirschman Index (HHI) of fragility concentration across compass bins quantifies this asymmetry in a single number.

#### New Result Fields

| Field | Type | Description |
|-------|------|-------------|
| `directional_fragility` | `vector<double>` | Per-bin average bottleneck fragility ratio. One entry per `angular_bins` (default 8). Reveals which compass directions have the most fragile routes. |
| `directional_asymmetry` | `double` | HHI of fragility concentration across directions. Range: [1/angular_bins, 1.0]. |

#### Interpretation Guide

With the default 8 angular bins:

| HHI Value | Meaning |
|-----------|---------|
| ~0.125 (= 1/8) | Perfectly uniform fragility across all directions. No directional bias. |
| 0.15 - 0.25 | Mild asymmetry. Some directions slightly more fragile. |
| 0.25 - 0.40 | Significant asymmetry. Fragility is concentrated in a few directions. Worth investigating which compass sectors drive the risk. |
| > 0.40 | Severe asymmetry. Fragility dominated by one or two directions. This is common for coastal or mountain-pass locations where one direction has no alternative routes. |

**Evacuation relevance:** An HHI of 0.4 with the high-fragility bin pointing east means the eastward evacuation corridor is much more vulnerable than other directions. Emergency planners should prioritize alternative routing or pre-positioned resources along that corridor.

#### Example

```cpp
auto result = location_fragility(graph, ch, idx, config);

// Per-direction fragility (one per compass bin)
for (size_t i = 0; i < result.directional_fragility.size(); ++i) {
    double bearing = i * (360.0 / result.directional_fragility.size());
    std::cout << "Bearing " << bearing << "deg: ratio=" << result.directional_fragility[i] << "\n";
}

// Asymmetry summary
std::cout << "Directional asymmetry (HHI): " << result.directional_asymmetry << "\n";
if (result.directional_asymmetry > 0.3) {
    std::cout << "Significant directional concentration of fragility\n";
}
```

---

### 17.3 Bridge Replacement Cost

Defined in `gravel/fragility/bridges.h`. New field on `BridgeResult` and new function `compute_bridge_costs()`.

**Why:** In v1.x, bridge analysis was binary: an edge is a bridge or it is not. But a bridge with a 30-second detour around a park is not the same threat as a bridge over a river gorge with no alternative. Replacement cost (the shortest detour when the bridge is removed) bridges the gap between binary bridge detection and actual criticality. A bridge with `replacement_cost = INF` truly disconnects the graph; one with `replacement_cost = 45.0` seconds has a minor detour.

#### New Result Field

| Field | Type | Description |
|-------|------|-------------|
| `replacement_costs` | `vector<Weight>` | Parallel to `bridges`. Shortest detour time (seconds) when this bridge is removed. `INF_WEIGHT` if the bridge disconnects the graph. Empty after `find_bridges()` alone -- populated by `compute_bridge_costs()`. |

#### Function

```cpp
void compute_bridge_costs(
    BridgeResult& bridges,
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx);
```

For each bridge {u, v}, runs a blocked CH query from u to v with {u,v} removed. The result populates `bridges.replacement_costs` in parallel with `bridges.bridges`.

| Parameter | Type | Description |
|-----------|------|-------------|
| `bridges` | `BridgeResult&` | In/out. Must have `bridges.bridges` populated (from `find_bridges()`). `replacement_costs` is populated on return. |
| `graph` | `const ArrayGraph&` | The graph. |
| `ch` | `const ContractionResult&` | Pre-built contraction hierarchy. |
| `idx` | `const ShortcutIndex&` | Shortcut index for blocked queries. |

#### Example

```cpp
auto bridges = find_bridges(graph);
compute_bridge_costs(bridges, graph, ch, idx);

for (size_t i = 0; i < bridges.bridges.size(); ++i) {
    auto [u, v] = bridges.bridges[i];
    auto cost = bridges.replacement_costs[i];
    if (cost == INF_WEIGHT) {
        std::cout << "Bridge " << u << "-" << v << ": DISCONNECTS graph\n";
    } else {
        std::cout << "Bridge " << u << "-" << v << ": detour=" << cost << "s\n";
    }
}
```

---

### 17.4 Bridge Classification

Defined in `gravel/analysis/bridge_classification.h`.

**Why:** When edges are filtered (e.g., keeping only tertiary+ roads), some edges that are NOT bridges in the full graph become bridges in the filtered subgraph. These "filter-induced bridges" are artifacts of the filtering decision, not genuine structural vulnerabilities. Reporting "county X has 12 bridges" when 8 of them are artifacts of dropping residential roads is misleading. This classification distinguishes real structural bridges from filter artifacts, which matters for both data quality reporting and research validity.

#### BridgeType

| Value | Description |
|-------|-------------|
| `STRUCTURAL` (0) | Bridge in both the full and filtered graph. A real structural vulnerability. |
| `FILTER_INDUCED` (1) | Bridge only in the filtered graph. An artifact: alternative paths exist via lower-class roads that were removed by filtering. |

#### BridgeClassification

| Field | Type | Description |
|-------|------|-------------|
| `bridges` | `vector<pair<NodeID, NodeID>>` | All bridges found in the filtered graph. |
| `types` | `vector<BridgeType>` | Parallel to `bridges`. Classification of each bridge. |
| `structural_count` | `uint32_t` | Count of `STRUCTURAL` bridges. |
| `filter_induced_count` | `uint32_t` | Count of `FILTER_INDUCED` bridges. |

#### Function

```cpp
BridgeClassification classify_bridges(
    const ArrayGraph& graph,
    const std::function<bool(uint32_t)>& edge_filter);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `graph` | `const ArrayGraph&` | The full (unfiltered) graph. |
| `edge_filter` | `function<bool(uint32_t)>` | Predicate: CSR edge index -> true to keep. Same predicate used for graph simplification edge filtering. |

**Algorithm:** Runs `find_bridges()` on both the full graph and a filtered subgraph, then classifies each filtered bridge by checking whether it also appears in the full graph's bridge set.

#### Example

```cpp
auto labels = EdgeCategoryLabels::from_strings(highway_tags, EdgeCategoryLabels::osm_road_ranks());
auto filter = make_category_filter(labels, 4);  // keep tertiary+

auto classification = classify_bridges(graph, filter);
std::cout << "Structural bridges: " << classification.structural_count << "\n";
std::cout << "Filter-induced (artifacts): " << classification.filter_induced_count << "\n";
std::cout << "Total in filtered graph: " << classification.bridges.size() << "\n";
```

**Methodology note:** Always report both counts when publishing bridge statistics on filtered graphs. The ratio `filter_induced_count / total` serves as a data-quality indicator: a high ratio suggests the filtering threshold is aggressive enough to create false bridge signals.

---

### 17.5 Population-Weighted OD Sampling

Defined in `gravel/analysis/od_sampling.h`. New field on `SamplingConfig`. Also available on `CountyFragilityConfig`.

**Why:** Uniform node sampling treats every graph node equally, but road networks have far more nodes along residential cul-de-sacs than near hospitals, shelters, and highway interchanges. This over-represents low-traffic dead-end roads relative to their actual human significance. Population-weighted sampling (using any external importance measure — population density, facility locations, traffic counts) ensures that fragility is measured for the routes that matter most.

#### New Config Field

**On `SamplingConfig`:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `node_weights` | `vector<double>` | empty | Optional per-node importance weights. When non-empty, must have `graph.node_count()` entries. Nodes are sampled proportionally to their weight. Higher weight = more likely to be selected as origin or destination. Empty = uniform sampling (v1.x behavior). |

**On `CountyFragilityConfig`:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `node_weights` | `vector<double>` | empty | Per-node importance weights indexed by ORIGINAL graph node IDs (before subgraph extraction). Weights for nodes outside the polygon boundary are ignored. Empty = uniform sampling. |

#### Example

```cpp
// Load importance weights (e.g., population density, facility locations, traffic counts)
std::vector<double> pop_weights(graph.node_count(), 0.0);
// ... populate from Census data ...

// Network-level sampling
SamplingConfig sconfig;
sconfig.total_samples = 1000;
sconfig.node_weights = pop_weights;
auto pairs = stratified_sample(graph, ch, sconfig);

// County-level analysis with population weighting
CountyFragilityConfig cconfig;
cconfig.boundary = county_polygon;
cconfig.node_weights = pop_weights;
auto result = county_fragility_index(graph, ch, idx, cconfig);
```

---

### 17.6 Scenario Fragility (Hazard Footprint)

Defined in `gravel/analysis/scenario_fragility.h`.

**Why:** Real-world disruptions don't happen to abstract networks — they happen to specific geographic regions. The question is not "how fragile is this network?" but "how fragile does this network become when a specific event removes edge set S?" This module computes county fragility on a degraded network (edges blocked by a hazard footprint polygon) and reports the delta versus baseline. Use cases include flood inundation zones, wildfire perimeters, landslide corridors, or any scenario where a set of edges becomes impassable.

#### Workflow

1. Define a hazard footprint (flood polygon, wildfire perimeter, etc.).
2. Convert the footprint to a set of blocked edges via `edges_in_polygon()`.
3. Run `scenario_fragility()` which computes baseline, removes edges, and recomputes.
4. Compare `delta_composite` and `relative_change` to measure the event's impact.

#### ScenarioConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `baseline` | `CountyFragilityConfig` | (required) | Baseline county fragility configuration (polygon, samples, weights). |
| `blocked_edges` | `vector<pair<NodeID, NodeID>>` | empty | Edges to treat as blocked in the scenario. Each pair is (u, v) in original graph node IDs. |
| `hazard_footprint` | `Polygon` | empty | Alternative: provide a hazard polygon and auto-detect blocked edges. If non-empty AND `blocked_edges` is empty, `edges_in_polygon()` is called automatically. |

#### ScenarioResult

| Field | Type | Description |
|-------|------|-------------|
| `baseline` | `CountyFragilityResult` | Full fragility result for the unmodified network. |
| `scenario` | `CountyFragilityResult` | Full fragility result with blocked edges removed. |
| `delta_composite` | `double` | `scenario.composite_index - baseline.composite_index`. Positive = fragility increased. |
| `relative_change` | `double` | `delta / baseline` (if baseline > 0). 0.25 means a 25% increase in fragility. |
| `edges_blocked` | `uint32_t` | Number of edges removed in the scenario. |
| `bridges_blocked` | `uint32_t` | Number of bridges among the blocked edges (critical failures). |

#### Functions

```cpp
ScenarioResult scenario_fragility(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const ScenarioConfig& config);
```

Computes county fragility on the original network (baseline), then removes the blocked edges, rebuilds the CH, and recomputes fragility (scenario). Reports the delta between the two.

```cpp
std::vector<std::pair<NodeID, NodeID>> edges_in_polygon(
    const ArrayGraph& graph,
    const Polygon& polygon);
```

Finds all graph edges whose BOTH endpoints fall within a polygon. Returns edges as (u, v) pairs. Useful for converting a hazard footprint into a set of blocked edges.

#### Example

```cpp
// Define a flood polygon
Polygon flood_zone;
flood_zone.vertices = {{35.4, -83.5}, {35.4, -83.3}, {35.5, -83.3}, {35.5, -83.5}, {35.4, -83.5}};

// Find affected edges
auto blocked = edges_in_polygon(graph, flood_zone);

// Run scenario analysis
ScenarioConfig cfg;
cfg.baseline.boundary = county_polygon;
cfg.baseline.od_sample_count = 200;
cfg.blocked_edges = blocked;

auto result = scenario_fragility(graph, ch, idx, cfg);
std::cout << "Baseline composite: " << result.baseline.composite_index << "\n";
std::cout << "Scenario composite: " << result.scenario.composite_index << "\n";
std::cout << "Delta: +" << result.delta_composite << "\n";
std::cout << "Relative change: " << (result.relative_change * 100) << "%\n";
std::cout << "Edges blocked: " << result.edges_blocked << "\n";
std::cout << "Bridges blocked: " << result.bridges_blocked << "\n";
```

**Alternative:** Provide `hazard_footprint` instead of `blocked_edges` to have edges auto-detected:

```cpp
ScenarioConfig cfg;
cfg.baseline.boundary = county_polygon;
cfg.hazard_footprint = flood_zone;  // edges_in_polygon() called internally
auto result = scenario_fragility(graph, ch, idx, cfg);
```

---

### 17.7 Ensemble Fragility (Sampling Variance)

Defined in `gravel/analysis/uncertainty.h`.

**Why:** The O-D sampling in `county_fragility_index()` uses a finite number of pairs (default 100). Different random seeds select different pairs, producing different composite scores. If the score changes substantially across seeds, the sample count is too low for stable rankings. The ensemble approach runs N independent realizations and reports mean +/- standard deviation, providing a confidence interval on the score. This is a standard robustness check per the OECD Handbook on Constructing Composite Indicators (2008).

#### EnsembleConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `base_config` | `CountyFragilityConfig` | (required) | The county fragility configuration to run repeatedly. |
| `num_runs` | `uint32_t` | `20` | Number of independent realizations. |
| `base_seed` | `uint64_t` | `42` | Seeds are `base_seed`, `base_seed+1`, ..., `base_seed+num_runs-1`. |

#### UncertaintyResult

| Field | Type | Description |
|-------|------|-------------|
| `ensemble` | `vector<CountyFragilityResult>` | All N individual results for detailed inspection. |
| `mean_composite` | `double` | Mean composite_index across runs. |
| `std_composite` | `double` | Standard deviation of composite_index. |
| `min_composite` | `double` | Minimum composite_index observed. |
| `max_composite` | `double` | Maximum composite_index observed. |
| `composite_p25` | `double` | 25th percentile. |
| `composite_p50` | `double` | Median. |
| `composite_p75` | `double` | 75th percentile. |
| `coefficient_of_variation` | `double` | `std / mean`. Values > 0.1 suggest the sample count is too low for stable rankings. |

#### Function

```cpp
UncertaintyResult ensemble_fragility(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const EnsembleConfig& config);
```

Runs `county_fragility_index()` N times with different random seeds. Measures sampling variance in the composite score.

#### Interpretation Guide

| CV Value | Interpretation |
|----------|---------------|
| < 0.05 | Highly stable. Rankings are robust to sampling noise. |
| 0.05 - 0.10 | Acceptable for most research. Report mean +/- SD. |
| > 0.10 | Unstable. Increase `od_sample_count` or `num_runs`. Rankings may change across random seeds. |

#### Example

```cpp
EnsembleConfig ecfg;
ecfg.base_config = county_config;
ecfg.num_runs = 20;
ecfg.base_seed = 42;

auto ens = ensemble_fragility(graph, ch, idx, ecfg);

std::cout << "Composite: " << ens.mean_composite << " +/- " << ens.std_composite << "\n";
std::cout << "CV: " << ens.coefficient_of_variation << "\n";
std::cout << "Range: [" << ens.min_composite << ", " << ens.max_composite << "]\n";

if (ens.coefficient_of_variation > 0.10) {
    std::cout << "WARNING: high sampling variance, increase od_sample_count\n";
}
```

---

### 17.8 Weight Sensitivity Analysis

Defined in `gravel/analysis/uncertainty.h`.

**Why:** The composite weights (bridge=0.30, connectivity=0.25, accessibility=0.25, fragility=0.20) are modeling choices, not physical constants. A county that ranks 10th under default weights but 3rd under slight perturbations has an unstable ranking. Weight sensitivity analysis perturbs each weight and measures how much the composite changes. This is a standard robustness check for composite indices (see OECD Handbook on Constructing Composite Indicators, 2008).

**Methodology:** One-at-a-time (OAT) perturbation. For each of the 4 composite weights, the weight is varied across a grid while the other 3 are held at base values and renormalized to sum to 1.0. The sensitivity is the approximate partial derivative of the composite score with respect to each weight.

#### WeightSensitivityConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `base_config` | `CountyFragilityConfig` | (required) | The county fragility configuration to perturb. |
| `perturbation_range` | `double` | `0.2` | Each weight varies by +/-this fraction. Default 0.2 means bridge_weight varies from 0.24 to 0.36 (+/-20% of 0.30). |
| `grid_points` | `uint32_t` | `5` | Evaluation points per weight dimension. 5 points means: -20%, -10%, 0%, +10%, +20%. |

#### WeightSensitivityResult

| Field | Type | Description |
|-------|------|-------------|
| `sensitivity_bridge` | `double` | Sensitivity of composite_index to bridge weight. Higher = score is more dependent on this weight choice. |
| `sensitivity_connectivity` | `double` | Sensitivity to connectivity weight. |
| `sensitivity_accessibility` | `double` | Sensitivity to accessibility weight. |
| `sensitivity_fragility` | `double` | Sensitivity to fragility weight. |
| `composite_min` | `double` | Minimum composite_index observed across all weight perturbations. |
| `composite_max` | `double` | Maximum composite_index observed across all perturbations. |
| `weight_grid` | `vector<CountyFragilityWeights>` | All evaluated weight configurations. |
| `composite_values` | `vector<double>` | Composite scores parallel to `weight_grid`. |

Sensitivity is computed as: `(composite_max - composite_min) / (2 * perturbation_range * base_weight)`.

#### Function

```cpp
WeightSensitivityResult weight_sensitivity(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const WeightSensitivityConfig& config);
```

#### Example

```cpp
WeightSensitivityConfig wcfg;
wcfg.base_config = county_config;
wcfg.perturbation_range = 0.2;
wcfg.grid_points = 5;

auto ws = weight_sensitivity(graph, ch, idx, wcfg);

std::cout << "Sensitivity to bridge weight: " << ws.sensitivity_bridge << "\n";
std::cout << "Sensitivity to connectivity: " << ws.sensitivity_connectivity << "\n";
std::cout << "Sensitivity to accessibility: " << ws.sensitivity_accessibility << "\n";
std::cout << "Sensitivity to fragility: " << ws.sensitivity_fragility << "\n";
std::cout << "Composite range: [" << ws.composite_min << ", " << ws.composite_max << "]\n";
```

**Interpretation:** If `sensitivity_bridge` is much larger than the others, the composite ranking depends heavily on how much weight is given to bridges. Report sensitivity values alongside rankings so that consumers of the analysis can assess robustness.

---

### 17.9 Edge Confidence

Defined in `gravel/analysis/edge_confidence.h`.

**Why:** OSM data quality varies: a road tagged in 2008 with no name is less reliable than a well-mapped motorway updated recently. If a bridge finding rests on a low-confidence edge, the finding itself is uncertain. Propagating confidence through fragility analysis lets you report "this county has 5 high-confidence bridges and 3 low-confidence bridges" rather than just "8 bridges." The confidence score is a heuristic, not ground truth, but it captures observable data quality signals.

#### EdgeConfidence

| Field | Type | Description |
|-------|------|-------------|
| `scores` | `vector<double>` | One per directed edge in CSR order. Range [0.0, 1.0]. Higher = more confident. |

**Method: `weight_multiplier(uint32_t edge_index)`**

Returns a travel-time penalty multiplier for low-confidence edges:

| Confidence | Multiplier | Effect |
|------------|------------|--------|
| 1.0 | 1.0 | No penalty. |
| 0.5 | 1.5 | 50% longer assumed travel time. |
| 0.0 | 2.0 | Double travel time (maximum uncertainty). |

Formula: `multiplier = 2.0 - confidence`.

#### Scoring Heuristic

`estimate_osm_confidence()` uses an additive heuristic, clamped to [0, 1]:

| Component | Points | Signal |
|-----------|--------|--------|
| Base | +0.20 | Every edge gets credit for existing in OSM. |
| Major road class (secondary or above) | +0.25 | Major roads receive more editing attention in OSM. |
| Has name tag | +0.20 | Named roads are more likely correct. |
| Has surface tag | +0.15 | Active maintenance signal -- someone cared enough to tag surface. |
| Has lane count | +0.10 | Additional detail suggests careful mapping. |
| Has maxspeed tag | +0.10 | Speed limit tags indicate active data curation. |

A well-tagged motorway scores 1.0. An unnamed, unclassified road with no tags scores 0.20.

#### Functions

```cpp
EdgeConfidence estimate_osm_confidence(
    const ArrayGraph& graph,
    const EdgeMetadata& metadata);
```

| Parameter | Type | Description |
|-----------|------|-------------|
| `graph` | `const ArrayGraph&` | The graph (for edge count). |
| `metadata` | `const EdgeMetadata&` | Edge metadata from `load_osm_graph_with_labels()`. |

```cpp
EdgeConfidence confidence_from_array(std::vector<double> values);
```

Create from a flat array for non-OSM data or external confidence sources.

#### Example

```cpp
auto osm = load_osm_graph_with_labels(osm_config);
auto confidence = estimate_osm_confidence(*osm.graph, osm.metadata);

// Use confidence to penalize low-quality edges in routing
for (uint32_t e = 0; e < osm.graph->edge_count(); ++e) {
    double penalty = confidence.weight_multiplier(e);
    // Apply penalty to edge weight for uncertainty-aware routing
}

// Report bridge confidence
auto bridges = find_bridges(*osm.graph);
for (size_t i = 0; i < bridges.bridges.size(); ++i) {
    auto [u, v] = bridges.bridges[i];
    // Find the CSR edge index for this bridge and check confidence
    std::cout << "Bridge " << u << "-" << v
              << " confidence=" << confidence.scores[/* edge_index */] << "\n";
}
```

---

### 17.10 Multi-Scale Tiled Fragility

Defined in `gravel/analysis/tiled_fragility.h`.

**Why:** County-level fragility is a single number, but "which part of the county drives the score?" matters for both analysis and policy. A county with composite_index=0.45 might have one corner with risk 0.8 (near a single-bridge mountain pass) and everywhere else at 0.2. The tiled decomposition reveals this spatial structure, which is invisible in the aggregate score. The output is directly mappable: each tile has a center coordinate and an isolation_risk value, suitable for GeoJSON heatmap visualization.

#### TileConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `min_lat` | `double` | (required) | Southern bound of the bounding box. |
| `max_lat` | `double` | (required) | Northern bound. |
| `min_lon` | `double` | (required) | Western bound. |
| `max_lon` | `double` | (required) | Eastern bound. |
| `tile_size_meters` | `double` | `16000.0` | Approximate tile side length (~10 miles). |
| `location_config` | `LocationFragilityConfig` | (defaults) | Base config for each tile. The `center` field is overridden per tile. |

#### TileResult

| Field | Type | Description |
|-------|------|-------------|
| `center` | `Coord` | Geographic center of this tile. |
| `fragility` | `LocationFragilityResult` | Full location fragility result at this tile center. |

#### TiledFragilityResult

| Field | Type | Description |
|-------|------|-------------|
| `tiles` | `vector<TileResult>` | All tile results. |
| `rows` | `uint32_t` | Number of tile rows in the grid. |
| `cols` | `uint32_t` | Number of tile columns in the grid. |
| `mean_isolation_risk` | `double` | Mean isolation_risk across all tiles. |
| `max_isolation_risk` | `double` | Maximum isolation_risk. |
| `min_isolation_risk` | `double` | Minimum isolation_risk. |
| `max_risk_location` | `Coord` | Tile center with the highest isolation risk. |

#### Function

```cpp
TiledFragilityResult tiled_fragility_analysis(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const TileConfig& config);
```

Runs `location_fragility()` at each tile center across a spatial grid. Parallelized with OpenMP when available.

| Parameter | Type | Description |
|-----------|------|-------------|
| `graph` | `const ArrayGraph&` | Full graph with coordinates. |
| `ch` | `const ContractionResult&` | Pre-built CH. |
| `idx` | `const ShortcutIndex&` | ShortcutIndex for the CH. |
| `config` | `const TileConfig&` | Tile grid configuration. |

#### Example

```cpp
TileConfig tcfg;
tcfg.min_lat = 35.0; tcfg.max_lat = 36.0;
tcfg.min_lon = -84.0; tcfg.max_lon = -83.0;
tcfg.tile_size_meters = 16000.0;  // ~10 mile tiles
tcfg.location_config.radius_meters = 40000.0;
tcfg.location_config.sample_count = 50;

auto tiled = tiled_fragility_analysis(graph, ch, idx, tcfg);

std::cout << "Grid: " << tiled.rows << " x " << tiled.cols << " tiles\n";
std::cout << "Mean isolation risk: " << tiled.mean_isolation_risk << "\n";
std::cout << "Max risk at: " << tiled.max_risk_location.lat
          << ", " << tiled.max_risk_location.lon << "\n";

// Export to GeoJSON for visualization
for (const auto& tile : tiled.tiles) {
    // Each tile.center + tile.fragility.isolation_risk -> GeoJSON point feature
}
```

**GeoJSON visualization note:** Each tile maps directly to a GeoJSON Point feature with `isolation_risk` as a property. Use a graduated color scale (green=0.0 to red=1.0) for heatmap rendering in QGIS, Mapbox, or Leaflet.

---

### 17.11 Edge Metadata (Generic Tag Storage)

Defined in `gravel/core/osm_graph.h`.

**Why:** v1.x `load_osm_graph()` discarded all OSM tags after computing edge weights and labels. The only surviving tag data was the `highway` label (collapsed to an integer rank). v2.0 preserves the raw tags -- highway, name, surface, bridge, tunnel, maxspeed, lanes, ref -- as a generic key-value store. This enables downstream analyses (bridge identification, road surface quality, edge confidence scoring) without re-parsing the PBF file.

#### EdgeMetadata

A generic per-edge metadata store indexed by CSR edge position. Holds arbitrary string key-value pairs for each directed edge.

| Field | Type | Description |
|-------|------|-------------|
| `tag_keys` | `vector<string>` | Available tag keys (e.g., `{"highway", "name", "surface"}`). |
| `tag_values` | `vector<vector<string>>` | Per-edge tag values. `tag_values[key_index][edge_index]`. Each inner vector has `graph->edge_count()` entries. |

**Methods:**

| Method | Return | Description |
|--------|--------|-------------|
| `get(key)` | `const vector<string>&` | All values for a specific tag key. Empty vector if the key does not exist. |
| `has(key)` | `bool` | Whether a tag key exists in the metadata. |

#### OSMLoadResult

| Field | Type | Description |
|-------|------|-------------|
| `graph` | `unique_ptr<ArrayGraph>` | The loaded graph. |
| `metadata` | `EdgeMetadata` | Per-edge metadata in CSR order. Contains all captured OSM tags. |

#### Function

```cpp
OSMLoadResult load_osm_graph_with_labels(const OSMConfig& config);
```

Loads an OSM graph with all edge tags preserved. Captures highway, name, surface, bridge, tunnel, maxspeed, lanes, and ref tags for every edge in CSR order.

| Parameter | Type | Description |
|-----------|------|-------------|
| `config` | `const OSMConfig&` | Same config as `load_osm_graph()`: PBF path, speed profile, bidirectional flag. |

#### Example

```cpp
OSMConfig osm_config;
osm_config.pbf_path = "tennessee.osm.pbf";
osm_config.speed_profile = SpeedProfile::car();

auto result = load_osm_graph_with_labels(osm_config);

// Access highway tags for road-class filtering
const auto& highway = result.metadata.get("highway");
auto labels = EdgeCategoryLabels::from_strings(highway, EdgeCategoryLabels::osm_road_ranks());
auto filter = make_category_filter(labels, 4);  // keep tertiary+

// Access road names
const auto& names = result.metadata.get("name");
for (uint32_t e = 0; e < result.graph->edge_count(); ++e) {
    if (!names[e].empty()) {
        // Edge e has a name
    }
}

// Access surface tags for quality analysis
const auto& surfaces = result.metadata.get("surface");

// Check if a tag exists
if (result.metadata.has("bridge")) {
    const auto& bridge_tags = result.metadata.get("bridge");
    // "yes" means the edge is tagged as a bridge in OSM
}
```

**Available tags from OSM:** highway, name, surface, bridge, tunnel, maxspeed, lanes, ref. Non-OSM graphs can use `EdgeMetadata` for any custom edge categorization by populating `tag_keys` and `tag_values` directly.

---

## 18. Progressive Elimination Fragility

Defined in `gravel/analysis/progressive_fragility.h`.

**Why:** All fragility metrics in sections 1--17 are built on single-edge failure: what happens if exactly one road closes? Real-world disruption is progressive -- a storm closes one road, then a second, then a third. Two counties with identical composite fragility at k=1 can have fundamentally different resilience profiles at k=2 or k=3. Progressive elimination answers the structural question that matters for comparative research: **how quickly does this network fall apart under progressive stress?**

The output is a **degradation curve** -- composite fragility indexed by k (number of edges removed). The curve shape, not any single point, is the primary analytical product.

---

### 18.1 The Degradation Curve

The degradation curve is a sequence of composite fragility scores:

```
f(k) = composite_fragility_index(subgraph \ S_k)
```

where `S_k` is the set of k edges removed according to the selection strategy. The curve runs from k=0 (baseline, no removals) to k=K_max (maximum removals configured by the caller).

Four qualitatively distinct curve shapes arise in practice:

| Curve Shape | Interpretation | Research Significance |
|-------------|---------------|----------------------|
| Flat then sharp jump | Resilient until a critical combination fails. Hidden threshold. | Most dangerous -- appears robust but has a specific breaking point. |
| Gradual linear rise | Each removal adds proportional fragility. No hidden thresholds. | Predictable degradation; community has time to adapt. |
| Steep initial rise then plateau | First failure is catastrophic; additional failures matter less. | Single bottleneck dominates; k=1 analysis captures most of the story. |
| Irregular / non-monotone | Fragility score is sampling-sensitive at this edge count. | Diagnostic signal -- increase OD samples or MC runs. Not a valid finding. |

Non-monotone curves are technically possible because the composite index is sampled, not exact. Genuine non-monotonicity indicates insufficient sampling, not a structural property.

---

### 18.2 Edge Selection Strategies

Three selection strategies model different failure assumptions. The caller selects via `selection_strategy`.

#### GREEDY_FRAGILITY -- Worst-Case Adversarial

At each step k=1..K_max, evaluates every remaining candidate edge and removes the one that causes the greatest composite fragility increase. Models adversarial or maximally damaging failure sequences.

**Research application:** Answers "what is the maximum damage a worst-case failure sequence could inflict?" The `removal_sequence` output identifies the specific edges that are the network's Achilles heel. Useful for infrastructure investment prioritization within a single subgraph.

Two recomputation modes are supported (see `recompute_mode`):

| Mode | Behavior | Cost per Step | Accuracy |
|------|----------|---------------|----------|
| `FULL_RECOMPUTE` (default) | Full `county_fragility_index` per candidate evaluation. | O(candidates * analysis_cost) | Exact |
| `FAST_APPROXIMATE` | Recompute only affected route fragilities; skip spectral metrics. | O(affected_routes * k) | Approximate; suitable for large subgraphs |

For K_max <= 5, `FULL_RECOMPUTE` is preferred for methodological defensibility.

#### GREEDY_BETWEENNESS -- High-Traffic Corridor Failure

Removes edges in descending order of edge betweenness centrality (computed once at baseline, held fixed). Models failure concentrated on high-traffic corridors -- the edges most likely to experience wear, congestion-related closures, or prioritized repair delays.

**Research application:** Answers "what happens when the most-used roads fail first?" Static ordering makes results interpretable and reproducible: the same edges are always removed in the same order, enabling cross-subgraph comparison. If a `BetweennessResult` is already available from a prior `county_fragility_index` call, pass it via `precomputed_betweenness` to skip recomputation.

#### MONTE_CARLO -- Stochastic Random Failure (Recommended)

Samples k edges uniformly at random, computes composite fragility, and repeats N times per k level to produce a distribution of outcomes. Models failure as an undirected stochastic process with no assumed mechanism.

**When to use:** Monte Carlo is the recommended strategy for broad comparative analysis. It makes no failure-mechanism assumptions, produces distributional outputs suitable for statistical comparison across subgraphs, and is the most methodologically defensible approach for published research. The IQR (p75 - p25) at each k level directly operationalizes structural robustness independent of any specific failure scenario.

**Reported per-k statistics (Monte Carlo only):**

| Field | Description |
|-------|-------------|
| `mean_composite` | Mean composite fragility across all MC runs at this k. |
| `std_composite` | Standard deviation. High SD at low k indicates a mix of critical and non-critical edges. |
| `p25`, `p50`, `p75`, `p90` | Percentile distribution. The p25--p90 spread is the most informative robustness signal. |
| `iqr` | p75 - p25. Narrow = consistent response to random failure. Wide = some combinations are catastrophic. |
| `fraction_disconnected` | Fraction of MC runs where any OD pair returned `INF_WEIGHT`. Directly measures disconnection probability. |

**Recommendation for comparative research:** Report `mean_composite` and `std_composite` in tables, `p50` in visualizations, and `fraction_disconnected` as a secondary fragility indicator. Use `iqr` as a dependent variable in regression models.

---

### 18.3 Configuration

#### SelectionStrategy Enum

```cpp
enum class SelectionStrategy {
    GREEDY_FRAGILITY,     // worst-case adversarial removal
    GREEDY_BETWEENNESS,   // highest-betweenness-first removal
    MONTE_CARLO,          // uniform random removal with repetition
};
```

#### RecomputeMode Enum

Used by `GREEDY_FRAGILITY` only.

```cpp
enum class RecomputeMode {
    FULL_RECOMPUTE,       // exact: full county_fragility_index per candidate
    FAST_APPROXIMATE,     // skip spectral metrics per candidate (faster)
};
```

#### ProgressiveFragilityConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `base_config` | `CountyFragilityConfig` | (required) | Base county fragility configuration: polygon boundary, OD sample count, weights. Applied at every k level including baseline. |
| `selection_strategy` | `SelectionStrategy` | `MONTE_CARLO` | Edge selection strategy. |
| `k_max` | `uint32_t` | `5` | Maximum edges to remove (absolute count). Ignored when `k_max_fraction > 0`. Silently capped at `subgraph_edges - 1`. |
| `k_max_fraction` | `float` | `0.0` | When > 0, sets k_max as a fraction of subgraph edges (e.g. `0.001` = 0.1%). Overrides `k_max`. Enables cross-network comparison on equal proportional terms. |
| `monte_carlo_runs` | `uint32_t` | `50` | Independent trials per k level (Monte Carlo only). |
| `base_seed` | `uint64_t` | `42` | Random seed. Trial i uses seed `base_seed + i`. |
| `edge_pool_filter` | `function<bool(uint32_t)>` | `nullptr` | Optional predicate restricting the random edge pool (Monte Carlo only). `nullptr` = all subgraph edges are candidates. Use to limit sampling to bridges, a road class, etc. |
| `recompute_mode` | `RecomputeMode` | `FULL_RECOMPUTE` | Greedy fragility only. Controls whether spectral metrics are recomputed per candidate evaluation. |
| `precomputed_betweenness` | `const BetweennessResult*` | `nullptr` | Greedy betweenness only. If non-null, skips betweenness recomputation and uses these scores. |
| `critical_k_threshold` | `double` | `0.7` | Fragility level that defines "critical" for `critical_k` detection. |
| `jump_detection_delta` | `double` | `0.15` | Minimum step increase (as fraction of f(0)) to flag as a discontinuous jump. |

#### K_max Guidance

Two ways to specify k_max:

**Absolute count (`k_max`):** Fixed number of edges to remove. Simple, but results are not directly comparable across networks of different sizes.

**Fractional (`k_max_fraction`):** Proportion of subgraph edges. Enables apples-to-apples comparison across networks:

```cpp
cfg.k_max_fraction = 0.001f;  // remove 0.1% of edges regardless of network size
```

| Context | Recommended k_max | k_max_fraction equivalent |
|---------|-------------------|--------------------------|
| Small rural subgraph (< 500 edges) | 3–5 | 0.006–0.010 |
| Typical county (500–5,000 edges) | 5–10 | 0.001–0.003 |
| Large urban subgraph (> 5,000 edges) | 10–20 | 0.001–0.005 |
| National comparative analysis | same fraction for all | **0.001 recommended** |

**Recommendation:** For cross-county or cross-region regression, use `k_max_fraction = 0.001`. This removes ~0.1% of each network's edges, keeping the structural impact proportional regardless of network density. A dense urban county (50K edges) and a sparse rural county (500 edges) are then both tested at the same proportional stress level.

For single-network analysis or infrastructure prioritization within one area, absolute `k_max` is more intuitive.

---

### 18.4 Result Structures

#### KLevelResult

Per-k-level statistics on the degradation curve.

| Field | Type | Populated For | Description |
|-------|------|---------------|-------------|
| `k` | `uint32_t` | all | Number of edges removed. |
| `mean_composite` | `double` | all | Mean composite fragility. Single value for greedy; mean across MC runs for Monte Carlo. |
| `std_composite` | `double` | MC only | Standard deviation across MC runs. |
| `p25`, `p50`, `p75`, `p90` | `double` | MC only | Percentile distribution of composite scores. |
| `iqr` | `double` | MC only | Interquartile range (p75 - p25). |
| `fraction_disconnected` | `double` | MC only | Fraction of MC runs with disconnected graph (any OD pair = `INF_WEIGHT`). |
| `run_values` | `vector<double>` | MC only | Full ensemble: all N composite values. |
| `removed_edge` | `pair<NodeID, NodeID>` | greedy only | Edge removed at this step. `{INVALID_NODE, INVALID_NODE}` at k=0. |

#### ProgressiveFragilityResult

| Field | Type | Description |
|-------|------|-------------|
| `curve` | `vector<KLevelResult>` | Degradation curve. `curve[0]` = baseline (k=0), `curve[k]` = result after k removals. Length = `k_max_used + 1`. |
| `auc_raw` | `double` | Raw area under the degradation curve. See [AUC Metrics](#185-auc-metrics). |
| `auc_normalized` | `double` | Scale-invariant degradation rate. |
| `auc_excess` | `double` | Excess degradation above baseline per removal step. |
| `critical_k` | `int32_t` | Smallest k where `mean_composite` exceeds `critical_k_threshold`. `-1` if never exceeded. |
| `jump_detected` | `bool` | Whether a discontinuous jump was detected in the curve. |
| `jump_at_k` | `int32_t` | k where jump occurred. `-1` if none. |
| `jump_magnitude` | `double` | Size of the jump (delta in composite index). |
| `removal_sequence` | `vector<pair<NodeID, NodeID>>` | Ordered edge removals (greedy strategies only). `removal_sequence[i]` = edge removed at k=i+1. Empty for Monte Carlo. |
| `strategy_used` | `SelectionStrategy` | Strategy that produced this result. |
| `subgraph_nodes` | `uint32_t` | Node count of extracted subgraph. |
| `subgraph_edges` | `uint32_t` | Edge count available for removal. |
| `k_max_used` | `uint32_t` | Actual K_max (may differ from config if subgraph is very small). |

---

### 18.5 AUC Metrics

The degradation curve is summarized as a scalar via area under the curve, with two normalizations for cross-subgraph comparison.

| Metric | Formula | Use Case |
|--------|---------|----------|
| `auc_raw` | `sum of f(k) for k=0..K_max` | Absolute degradation magnitude. Sensitive to both baseline and K_max. |
| `auc_normalized` | `auc_raw / (K_max + 1)` | Scale-invariant degradation rate. Comparable across subgraphs regardless of K_max. |
| `auc_excess` | `(auc_raw - f(0) * (K_max + 1)) / K_max` | Excess degradation above baseline per removal step. Controls for starting fragility. **Best for cross-subgraph regression.** |

`auc_excess` is the recommended primary dependent variable for comparative research. It captures degradation sensitivity independent of baseline fragility, allowing comparison between a high-fragility rural county and a low-fragility urban county on equal terms. A county with `auc_excess = 0.08` degrades 0.08 composite-index-units per edge removal on average above its baseline, regardless of whether that baseline is 0.2 or 0.6.

---

### 18.6 Critical K and Jump Detection

**Critical K:** The smallest k at which the fragility curve exceeds `critical_k_threshold`. Operationalizes a network's "breaking point" as a discrete value suitable for regression or clustering.

- `critical_k = 1` -- brittle. A single additional failure pushes past the threshold.
- `critical_k = 5` or `critical_k = -1` (never exceeded) -- structurally robust within the tested range.

**Jump detection:** Identifies discontinuous increases in the curve -- a delta between consecutive k values that exceeds `jump_detection_delta * f(0)`. A jump at k=2 is a qualitatively different finding from gradual linear increase, even if the final score at K_max is the same.

When `jump_detected = true`, `jump_at_k` identifies the step and `jump_magnitude` reports the delta. This directly supports research into "hidden thresholds" -- networks that appear robust until a critical combination fails.

---

### 18.7 CLI Reference

Command: `progressive-fragility`

```
gravel progressive-fragility --graph <.gravel.meta> --ch <.gravel.ch>
                              --polygon <geojson>
    --strategy <greedy-fragility|greedy-betweenness|monte-carlo>
    [--k-max <n>]                   Default: 5 (ignored if --k-max-fraction is set)
    [--k-max-fraction <f>]          Fraction of subgraph edges, e.g. 0.001 = 0.1%
    [--mc-runs <n>]                 Monte Carlo runs (default: 50)
    [--seed <n>]                    Base random seed (default: 42)
    [--recompute-mode <full|fast>]  Greedy fragility only (default: full)
    [--samples <n>]                 OD pairs per evaluation (default: 100)
    [--critical-threshold <f>]      Critical_k threshold (default: 0.7)
    [--jump-delta <f>]              Jump detection threshold (default: 0.15)
    [--output <path>]               Save JSON result to file
```

| Flag | Description | Default |
|------|-------------|---------|
| `--graph` | Path to `.gravel.meta` graph file. | (required) |
| `--ch` | Path to `.gravel.ch` contraction hierarchy file. | (required) |
| `--polygon` | GeoJSON polygon defining the analysis boundary. | (required) |
| `--strategy` | Selection strategy: `greedy-fragility`, `greedy-betweenness`, or `monte-carlo`. | (required) |
| `--k-max` | Maximum edges to remove (absolute). Ignored if `--k-max-fraction` is set. | 5 |
| `--k-max-fraction` | Remove this fraction of subgraph edges (e.g. `0.001`). Overrides `--k-max`. | unset |
| `--mc-runs` | Independent MC trials per k level. | 50 |
| `--seed` | Base random seed. Trial i uses seed + i. | 42 |
| `--recompute-mode` | `full` or `fast`. Greedy fragility only. | `full` |
| `--samples` | OD pairs per fragility evaluation. | 100 |
| `--critical-threshold` | Fragility level defining "critical" for `critical_k`. | 0.7 |
| `--jump-delta` | Minimum step increase (fraction of f(0)) to flag as jump. | 0.15 |
| `--output` | Write JSON result to this path instead of stdout. | stdout |

**Example -- all three strategies on the same county:**

```bash
# Monte Carlo (comparative research default)
gravel progressive-fragility --graph nc.gravel.meta --ch nc.gravel.ch \
    --polygon jackson_county.geojson --strategy monte-carlo \
    --k-max 5 --mc-runs 50 --samples 200

# Greedy betweenness (high-traffic corridor failure)
gravel progressive-fragility --graph nc.gravel.meta --ch nc.gravel.ch \
    --polygon jackson_county.geojson --strategy greedy-betweenness \
    --k-max 5

# Greedy fragility (worst-case adversarial)
gravel progressive-fragility --graph nc.gravel.meta --ch nc.gravel.ch \
    --polygon jackson_county.geojson --strategy greedy-fragility \
    --k-max 5 --recompute-mode full
```

---

### 18.8 Code Example

Monte Carlo progressive elimination (recommended for comparative research):

```cpp
#include "gravel/analysis/progressive_fragility.h"

ProgressiveFragilityConfig cfg;
cfg.base_config.boundary        = county_polygon;
cfg.base_config.od_sample_count = 50;
cfg.base_config.seed            = 42;

cfg.selection_strategy = SelectionStrategy::MONTE_CARLO;
cfg.k_max_fraction     = 0.001f;   // remove 0.1% of subgraph edges
cfg.monte_carlo_runs   = 30;
cfg.base_seed          = 42;

auto result = progressive_fragility(graph, ch, idx, cfg);

// Degradation curve: composite score 0=intact, 1=fully degraded
// composite = 0.6*(1-reachability) + 0.4*min(1, (avg_stretch-1)/5)
for (const auto& level : result.curve) {
    printf("k=%d  mean=%.4f  p50=%.4f  disconn=%.1f%%\n",
           level.k, level.mean_composite, level.p50,
           level.fraction_disconnected * 100.0);
}

// Summary metrics for cross-network regression
printf("k_max resolved: %d edges (%.3f%% of subgraph)\n",
       result.k_max_used,
       100.0 * result.k_max_used / result.subgraph_edges);
printf("AUC excess:     %.4f\n", result.auc_excess);
printf("Critical k:     %d\n",   result.critical_k);
if (result.jump_detected)
    printf("Jump at k=%d  magnitude=%.3f\n",
           result.jump_at_k, result.jump_magnitude);
```

---

### 18.9 Interpretation Guide

Recommended metrics by research question:

| Research Question | Primary Metric | Secondary Metric | Strategy |
|-------------------|---------------|-----------------|----------|
| How robust is this county to random disruption? | `auc_excess` | `iqr[k=3]` | MONTE_CARLO |
| Which specific edges are critical infrastructure? | `removal_sequence` | curve shape | GREEDY_FRAGILITY |
| How does the county handle high-traffic corridor failure? | curve shape | `critical_k` | GREEDY_BETWEENNESS |
| Does this county have a structural breaking point? | `jump_detected` + `jump_at_k` | `critical_k` | MONTE_CARLO or GREEDY_FRAGILITY |
| Cross-county regression: fragility as dependent variable | `auc_excess` | `fraction_disconnected[k=3]` | MONTE_CARLO |
| Ranking counties by resilience | `auc_excess` rank | `critical_k` | MONTE_CARLO |

**Interpreting `fraction_disconnected`:** This is the most direct measure of isolation risk. It answers "if k random roads close, what is the probability that some OD pair becomes completely unreachable?" A county can have high `auc_excess` (detours get long) but low `fraction_disconnected` (the graph stays connected). The reverse -- modest `auc_excess` but high `fraction_disconnected` -- indicates edges whose removal severs the graph entirely, which is the more severe failure mode. For populations dependent on single-road access (mountain communities, island connectors), `fraction_disconnected` at k=1 or k=2 is the most policy-relevant output.

---

### 18.10 Methodology Notes

#### How Edge Removal is Simulated

**Monte Carlo and Greedy Betweenness** use a reverse incremental SSSP approach operating directly on a compact local adjacency list built from the subgraph. The algorithm has two phases: an initial full Dijkstra establishing worst-case distances, followed by a sequence of incremental updates as edges are restored one at a time.

---

**Phase 1 — Initial Full SSSP (all k edges blocked)**

A compact local graph `adj[u]` is built from the subgraph's adjacency list with weights. Node IDs are compacted to a contiguous `[0, N_local)` range. OD pairs `(s, t)` are sampled from within this local graph.

For each unique source node `s`:

```
dist[s][v] = ∞  for all v ≠ s
dist[s][s] = 0

priority_queue pq  (min-heap of (distance, node))
pq.push( (0, s) )

while pq not empty:
    (d, u) = pq.pop()
    if d > dist[s][u]: continue          // stale entry, skip

    for each (v, w) in adj[u]:
        if edge_key(u,v) ∈ blocked_set: continue   // edge is removed
        if dist[s][u] + w < dist[s][v]:
            dist[s][v] = dist[s][u] + w
            pq.push( (dist[s][v], v) )
```

`blocked_set` holds the `edge_key` of every removed edge. `edge_key(u,v) = (min(u,v) << 32) | max(u,v)` — undirected, so one key covers both directions. The result is the exact shortest-distance tree from `s` in the graph with all k edges absent.

A parallel full SSSP is also run with an **empty** blocked set to produce `dist_full[s][v]` — the unblocked (baseline) distance from `s` to every node. This array serves as a strict lower bound throughout the incremental phase.

---

**Phase 2 — Incremental Restoration (add edges back one at a time)**

Edges are restored in reverse removal order: the last-removed edge is restored first, working back toward k=1. After each restoration the composite score is recorded, producing the degradation curve from k=k_max down to k=1.

For each restored edge `(u, v, w)` and each source `s`:

**Step A — Source skip check:**

```
if dist[s][u] == dist_full[s][u]  AND
   dist[s][v] == dist_full[s][v]:
       skip this source entirely
```

If both endpoints of the restored edge are already at their unblocked optimum, no path through this edge can improve any other node's distance from `s`. This is a strict condition — one endpoint being suboptimal is enough to require propagation.

**Step B — Seed check (is there an improvement at the edge itself?):**

```
can_improve_v = (dist[s][u] + w  <  dist[s][v])
can_improve_u = (dist[s][v] + w  <  dist[s][u])

if NOT can_improve_v AND NOT can_improve_u:
    skip (the restored edge is not on any improving path from s)
```

**Step C — Incremental Dijkstra propagation:**

If an improvement exists at either endpoint, seed the propagation queue with the improved node(s) and run a bounded Dijkstra:

```
if can_improve_v:
    dist[s][v] = dist[s][u] + w
    pq.push( (dist[s][v], v) )

if can_improve_u:
    dist[s][u] = dist[s][v] + w
    pq.push( (dist[s][u], u) )

while pq not empty:
    (d, node) = pq.pop()
    if d > dist[s][node]: continue         // stale entry

    for each (neighbor, edge_w) in adj[node]:
        if edge_key(node, neighbor) ∈ blocked_set: continue  // still blocked

        candidate = dist[s][node] + edge_w
        if candidate < dist[s][neighbor]:
            dist[s][neighbor] = candidate
            pq.push( (candidate, neighbor) )
```

---

**Stopping logic — why propagation is self-limiting**

The propagation stops at any node for two reasons:

1. **Stale entry:** When a node is popped from the queue, its tentative distance is compared against `dist[s][node]`. If `d > dist[s][node]`, a better path was already found and settled by an earlier pop — this entry is stale and discarded immediately. This is standard Dijkstra lazy deletion.

2. **No improvement:** When examining a neighbor, the condition `candidate < dist[s][neighbor]` must hold to enqueue it. If the restored edge's benefit does not cascade to a given neighbor (because that neighbor's current distance is already at least as good via another path), propagation simply does not continue through it.

The **key invariant** is that `dist[s][v] ≥ dist_full[s][v]` always holds — the blocked-graph distance can only equal or exceed the unblocked distance, never improve on it. As edges are restored, `dist[s][v]` can only decrease, never increase. This means:

- As more edges are restored across the k levels, `dist[s][v]` converges toward `dist_full[s][v]` for all v.
- Any node `v` where `dist[s][v] == dist_full[s][v]` is already optimal and **cannot be improved by any future restoration**. Propagation passing through such a node will fail the `candidate < dist[s][neighbor]` check against already-optimal neighbors and terminate.
- The propagation "footprint" — the set of nodes that actually get updated — shrinks naturally as more edges are restored. The algorithm does the least work in the final restoration steps, when most of the graph is already at its unblocked optimum.

---

**Composite score formula**

After each restoration step, OD pair distances are read directly from `dist[src_idx][target]`:

```
pair_dist[p] = dist[source_index_of(p)][target_of(p)]

reachable     = count of pairs where pair_dist[p] < ∞
reach_frac    = reachable / total_pairs
avg_ratio     = mean of (pair_dist[p] / primary_dist[p])  over reachable pairs

composite = 0.6 * (1 - reach_frac)
          + 0.4 * min(1.0,  (avg_ratio - 1.0) / 5.0)
```

- `reach_frac = 1.0` (all pairs connected) and `avg_ratio = 1.0` (no detour) both contribute 0, so composite = 0.0 at k=0 by construction.
- The 5.0 denominator in the stretch term normalises a 6× detour (avg_ratio = 6) to a full score of 1.0.
- The 0.6 / 0.4 weighting prioritises connectivity loss over stretch increase, reflecting that disconnection is the more severe failure mode.

---

**Complexity summary**

| Phase | Cost per run |
|-------|-------------|
| Full SSSP × sources (blocked) | O(sources × (N + E) log N) |
| Full SSSP × sources (unblocked, computed once, shared) | O(sources × (N + E) log N) |
| Incremental restore × k levels × sources | O(k × sources × Δ log N), where Δ = affected subgraph size per restoration |
| Read pair distances | O(k × pairs) |

Δ is typically a small fraction of N — often tens to hundreds of nodes — because most restorations only affect paths that specifically used the removed edge. As k decreases (more edges restored), Δ shrinks further as nodes converge to their unblocked optima.

**Greedy Fragility** uses `county_fragility_index` at each step, which includes the full spectral and accessibility pipeline. This is intentionally expensive — it produces the most methodologically rigorous worst-case sequence but is not recommended for large k_max or large subgraphs.

**No CH rebuilding is required** for any strategy. For K_max=50 with `monte_carlo_runs=30` on Swain County, NC (200K-node full graph, 392K-edge subgraph), total wall time is under 5 seconds.

#### Example Methodology Description

When documenting results that use progressive elimination, the following summarizes the approach:

> Progressive elimination fragility was computed for each subgraph using Monte Carlo random edge removal (N=50 trials per k level, K_max=5). At each trial, k edges were drawn uniformly at random from the subgraph edge set without replacement, and the composite fragility index was recomputed on the resulting degraded network. The normalized excess area under the degradation curve (`auc_excess`) was used as the primary comparison metric, as it captures degradation sensitivity independently of baseline fragility level. Weight sensitivity and sampling variance were assessed per OECD composite indicator guidelines (Nardo et al., 2008).

#### Reproducibility

All Monte Carlo runs are deterministic given a base seed. Trial `i` uses seed `base_seed + i`. To reproduce results exactly, specify the same `base_seed`, `k_max`, `monte_carlo_runs`, and `base_config` (including `od_sample_count` and `seed`).

---

## 19. AnalysisContext (Performance Cache)

Defined in `gravel/analysis/analysis_context.h`.

### Motivation

County fragility analysis involves several expensive setup steps: subgraph extraction, degree-2 simplification, bridge detection, and entry-point identification. When computing a degradation curve with many k levels (or running Monte Carlo with many trials), repeating these steps each time is wasteful — the underlying subgraph does not change between levels.

`AnalysisContext` caches these results once and makes them available across all calls in a batch.

---

### AnalysisContextConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `boundary` | `Polygon` | (required) | Analysis polygon. Same as `CountyFragilityConfig::boundary`. |
| `simplify` | `bool` | `true` | Apply degree-2 contraction before analysis. Reduces node count by ~70–90% with zero loss of routing accuracy. |

---

### AnalysisContext

| Field | Type | Description |
|-------|------|-------------|
| `raw_subgraph` | `SubgraphResult` | Extracted subgraph (pre-simplification). |
| `analysis_graph` | `shared_ptr<ArrayGraph>` | The graph used for metric computation. Simplified if `config.simplify = true`. |
| `analysis_to_original` | `vector<NodeID>` | Maps analysis graph node IDs → original full-graph node IDs. |
| `bridges` | `BridgeResult` | Precomputed bridge detection result. |
| `entry_points` | `vector<EntryPoint>` | Precomputed entry points for accessibility analysis. |
| `stats` | `AnalysisContextStats` | Build timing and node reduction statistics. |
| `simplified` | `bool` | Whether degree-2 simplification was applied. |

```cpp
bool valid() const;  // true if build succeeded and analysis_graph is non-null
```

---

### Build Function

```cpp
AnalysisContext build_analysis_context(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const AnalysisContextConfig& config);
```

Executes the full setup pipeline: extract subgraph → degree-2 simplify → detect bridges → identify entry points. Results are cached in the returned `AnalysisContext`.

---

### AnalysisContextStats

| Field | Type | Description |
|-------|------|-------------|
| `raw_node_count` | `uint32_t` | Subgraph nodes before simplification. |
| `analysis_node_count` | `uint32_t` | Nodes after simplification. |
| `reduction_fraction` | `double` | `1 - analysis/raw`. E.g. `0.927` = 92.7% fewer nodes. |
| `build_time_ms` | `double` | Total context build time in milliseconds. |

---

### Performance Characteristics

Measured on Swain County, NC (200K full-graph nodes, ~6K subgraph nodes):

| Step | Time | Notes |
|------|------|-------|
| Subgraph extraction | ~5 ms | Polygon-point-in-polygon test for all nodes |
| Degree-2 simplification | ~58 ms | 195K → 14K nodes (92.7% reduction) |
| Bridge detection (Tarjan) | ~1 ms | On simplified graph |
| Entry point identification | ~1 ms | On simplified graph |
| **Total context build** | **~63 ms** | One-time cost, amortised across all analyses |

---

### Usage Pattern

```cpp
// Build once for a given region
AnalysisContextConfig ctx_cfg;
ctx_cfg.boundary = county_polygon;
ctx_cfg.simplify = true;

auto ctx = build_analysis_context(graph, ch, idx, ctx_cfg);
printf("Built: %d → %d nodes (%.1f%% reduction) in %.1fms\n",
       ctx.stats.raw_node_count,
       ctx.stats.analysis_node_count,
       ctx.stats.reduction_fraction * 100.0,
       ctx.stats.build_time_ms);

// Reuse for multiple analyses
CountyFragilityConfig cfg;
cfg.boundary = county_polygon;
// ... configure weights, OD samples, etc.

// Pass context to skip rebuild
auto result = county_fragility_index(graph, ch, idx, cfg, &ctx);
```

---

## 20. Performance Optimizations

This section documents the key performance properties of the major analysis paths, based on profiling against Swain County, NC (200K nodes, 400K edges).

### Algebraic Connectivity — Connected-Component Early Exit

The algebraic connectivity solver (Spectra eigendecomposition) was previously called on subgraphs that were already disconnected, causing 41-second stalls (finding a near-zero eigenvalue in a sparse 195K-node matrix).

**Fix:** BFS connectivity pre-check before any eigensolve. For disconnected graphs, returns 0.0 immediately (correct: a disconnected graph has λ₂ = 0 by definition). For graphs with > 50K nodes, uses a degree-based heuristic rather than the full eigensolve.

| Case | Before | After |
|------|--------|-------|
| Disconnected subgraph | 41.7 s | 0.000017 s |
| Connected, < 50K nodes | ~0.5–2 s | ~0.5–2 s (unchanged) |
| Connected, > 50K nodes | timeout | heuristic, < 1 ms |

### Accessibility Analysis — Fast Mode for Large Subgraphs

For subgraphs with > 5,000 nodes, accessibility analysis switches from full `route_fragility` per entry-point pair (expensive: one blocked-CH query per path edge per pair) to direct CH distance queries (one query per pair). Reachability fraction is computed exactly; per-edge fragility is omitted in fast mode.

| Case | Before | After |
|------|--------|-------|
| Large subgraph (5K+ nodes) | > 10 min | 0.002 s |
| Small subgraph (< 5K nodes) | < 1 s | < 1 s (unchanged) |

### Progressive Elimination — Reverse Incremental SSSP

The original progressive elimination computed a full `county_fragility_index` at each k level, which on large subgraphs cost ~10s per level. See [Section 18.10](#1810-methodology-notes) for the current incremental SSSP algorithm.

| Strategy | Old approach | New approach | Speedup |
|----------|-------------|--------------|---------|
| Monte Carlo | `BlockedCHQuery` per OD pair per k level | Incremental SSSP on subgraph | ~50–200× |
| Greedy Betweenness | `county_fragility_index` × k_max calls | Incremental SSSP on subgraph | 138× (497s → 3.6s at k=50) |
| Greedy Fragility | `county_fragility_index` × candidates × k | unchanged | — |

---

## 21. Sub-Library Architecture (v2.1)

Gravel is organized into six sub-libraries with a strict one-directional dependency graph. The CLI and Python bindings link all libraries; consumers building custom pipelines can link only what they need.

### 19.1 Sub-Library Definitions

| Library | Purpose | Dependencies |
|---------|---------|--------------|
| `gravel-core` | Graph representation, basic routing, utilities, edge sampling, incremental SSSP | C++ stdlib, OpenMP |
| `gravel-ch` | Contraction hierarchy construction and query | gravel-core |
| `gravel-simplify` | Graph simplification, bridge detection | gravel-core, gravel-ch |
| `gravel-fragility` | All fragility/analysis, spectral metrics, progressive elimination | gravel-core, gravel-ch, gravel-simplify, Eigen, Spectra, nlohmann_json |
| `gravel-geo` | Geographic operations, OSM loading, region assignment, GeoJSON loading | gravel-core, gravel-simplify, nlohmann_json |
| `gravel-us` | US-specific specializations (TIGER loaders, FIPS conventions) | gravel-geo |

### 19.2 Dependency Rules

Any include that crosses a boundary in the wrong direction is a link error.

- `gravel-core` → nothing
- `gravel-ch` → gravel-core only
- `gravel-simplify` → gravel-core, gravel-ch
- `gravel-fragility` → gravel-core, gravel-ch, gravel-simplify (NOT gravel-geo, gravel-us)
- `gravel-geo` → gravel-core, gravel-simplify (NOT gravel-fragility, gravel-us)
- `gravel-us` → gravel-geo (NOT gravel-fragility)

### 19.3 External Dependency Isolation

| External Dependency | Confined To | Build Flag |
|---------------------|-------------|------------|
| osmium (OSM parsing) | gravel-geo | `GRAVEL_USE_OSMIUM=ON` |
| Eigen + Spectra | gravel-fragility | Always on |
| nlohmann/json | gravel-geo, gravel-fragility | Always on |
| OpenMP | gravel-core (propagates) | Auto-detected |

---

## 22. EdgeSampler (`gravel/core/edge_sampler.h`)

General-purpose edge sampling for graph analysis. Used by progressive fragility, betweenness approximation, and border edge characterization.

### Sampling Strategies

| Strategy | Description |
|----------|-------------|
| `UNIFORM_RANDOM` | Baseline: sample edges uniformly at random |
| `STRATIFIED_BY_CLASS` | Proportional (or equal) representation per road class |
| `IMPORTANCE_WEIGHTED` | Sample proportional to a weight vector (e.g., betweenness scores) |
| `SPATIALLY_STRATIFIED` | Grid-based geographic coverage |
| `CLUSTER_DISPERSED` | Greedy farthest-point: maximize spatial spread between sampled edges |

### Usage

```cpp
EdgeSampler sampler(graph);

SamplerConfig cfg;
cfg.strategy = SamplingStrategy::IMPORTANCE_WEIGHTED;
cfg.weights = betweenness_scores;
cfg.target_count = 100;
cfg.seed = 42;

auto indices = sampler.sample(cfg);           // CSR edge indices
auto pairs = sampler.sample_pairs(cfg);       // (source, target) pairs
```

### Integration with Progressive Fragility

Set `ProgressiveFragilityConfig::sampler_config` to use EdgeSampler for candidate edge selection instead of using all subgraph edges.

---

## 23. IncrementalSSSP (`gravel/core/incremental_sssp.h`)

Reusable reverse incremental SSSP engine for progressive edge removal analysis. Manages a multi-source distance matrix with efficient incremental updates.

### Usage

```cpp
auto lg = build_local_graph(subgraph);
std::unordered_set<uint64_t> blocked;
for (auto& e : removal_set)
    blocked.insert(IncrementalSSSP::edge_key(e.u, e.v));

IncrementalSSSP engine(lg, sources, blocked);

// Initial state: all edges blocked, full Dijkstra computed
Weight d = engine.dist(src_idx, target);

// Restore edges one at a time (reverse of removal order)
engine.restore_edge(u, v, weight);

// Distances updated incrementally — propagation stops when no improvement
```

### Complexity

| Phase | Cost | Notes |
|-------|------|-------|
| Construction (blocked SSSP) | O(sources × (N+E) log N) | One Dijkstra per source |
| Construction (unblocked SSSP) | O(sources × (N+E) log N) | Lower bounds, computed once |
| `restore_edge()` per source | O(Δ log N) | Δ = affected nodes, typically tens |

---

## 24. RegionAssignment (`gravel/geo/region_assignment.h`)

Assigns graph nodes to geographic boundary regions using point-in-polygon containment.

### Pipeline

```cpp
// 1. Load boundary polygons from GeoJSON
auto regions = load_regions_geojson("counties.geojson", {
    .region_id_property = "GEOID",
    .label_property = "NAMELSAD"
});

// 2. Assign nodes to regions
auto assignment = assign_nodes_to_regions(graph, regions);

// 3. Get boundary nodes for simplification protection
auto protection = boundary_nodes(graph, assignment);

// 4. Simplify with boundary protection
auto simplified = contract_degree2(graph, bridge_eps, protection);
```

### GeoJSON Coordinate Order

GeoJSON coordinates are `[longitude, latitude]` (x, y). Gravel `Coord` is `{lat, lon}`. The GeoJSON loader performs the swap internally — callers always receive Gravel conventions.

### TIGER Loaders (`gravel/us/tiger_loader.h`)

| Function | TIGER Layer | region_id Property | label Property |
|----------|-------------|-------------------|----------------|
| `load_tiger_counties()` | Counties | GEOID (5-digit FIPS) | NAMELSAD |
| `load_tiger_states()` | States | STATEFP | NAME |
| `load_tiger_cbsas()` | CBSAs (MSAs) | CBSAFP | NAME |
| `load_tiger_places()` | Places (cities/CDPs) | GEOID (7-digit) | NAMELSAD |
| `load_tiger_urban_areas()` | Urban Areas | UACE10 | NAME10 |

---

## 25. Boundary-Aware Degree-2 Contraction

The `contract_degree2()` function accepts an optional `boundary_protection` parameter. Nodes in this set are never contracted, preserving inter-regional connectivity.

### Correct Pipeline Order

1. Load graph
2. Assign regions (`assign_nodes_to_regions`)
3. Filter edges by road class (`filter_edges`)
4. Compute `boundary_nodes()` on the **filtered** graph (not the full graph)
5. `contract_degree2(graph, bridge_endpoints, boundary_protection)`
6. Optional: CH-level pruning

Step 4 must use the filtered graph. Computing boundary_nodes on the full graph may protect nodes that become isolated after filtering.

---

## 26. Border Edge Summarization (`gravel/geo/border_edges.h`)

Summarizes edges crossing region boundaries. For each pair of regions, counts directed edges, sums weights, and records min/max weight.

```cpp
BorderEdgeResult summarize_border_edges(const ArrayGraph& graph, const RegionAssignment& assignment);
```

### BorderEdgeResult

| Field | Type | Description |
|-------|------|-------------|
| `pair_summaries` | `unordered_map<RegionPair, BorderEdgeSummary>` | Per-region-pair statistics. |
| `total_border_edges` | `uint32_t` | Total border edges across all pairs. |
| `connected_pairs` | `uint32_t` | Distinct region pairs connected by edges. |
| `unassigned_edges` | `uint32_t` | Edges touching unassigned nodes. |

### BorderEdgeSummary

| Field | Type | Description |
|-------|------|-------------|
| `edge_count` | `uint32_t` | Directed edges crossing this boundary. |
| `total_weight` | `double` | Sum of edge weights. |
| `min_weight` | `double` | Lightest crossing edge. |
| `max_weight` | `double` | Heaviest crossing edge. |

**Complexity:** O(V + E) — single pass over all edges.

---

## 27. Geographic Graph Coarsening (`gravel/geo/graph_coarsening.h`)

Collapses each region to a single node, producing a compact meta-graph where edges represent inter-regional connections. Edge weights are the minimum border crossing weight.

```cpp
CoarseningResult coarsen_graph(
    const ArrayGraph& graph,
    const RegionAssignment& assignment,
    const BorderEdgeResult& border_edges,
    const CoarseningConfig& config = {});
```

### CoarseningConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `compute_centroids` | `bool` | `true` | Compute centroid coordinates for region nodes. |
| `min_border_edges` | `uint32_t` | `1` | Minimum edges between regions to create a coarsened edge. |

### CoarseningResult

| Field | Type | Description |
|-------|------|-------------|
| `graph` | `unique_ptr<ArrayGraph>` | Coarsened graph (node i = region i). |
| `region_ids` | `vector<string>` | Region ID per coarsened node. |
| `region_labels` | `vector<string>` | Region label per coarsened node. |
| `node_counts` | `vector<uint32_t>` | Original nodes per region. |
| `internal_edge_counts` | `vector<uint32_t>` | Internal edges per region. |

### Pipeline

```cpp
auto regions = load_tiger_counties("counties.geojson");
auto assignment = assign_nodes_to_regions(graph, regions);
auto border = summarize_border_edges(graph, assignment);
auto coarsened = coarsen_graph(graph, assignment, border);
// coarsened.graph has ~3200 nodes (one per county) instead of millions
```

---

## 28. FIPS Crosswalk and Typed Wrappers (`gravel/us/`)

### CountyAssignment (`gravel/us/county_assignment.h`)

Typed wrapper around `RegionAssignment` for US FIPS counties.

```cpp
CountyAssignment ca = assign_counties(graph, "tiger_counties.geojson");
ca.fips_code(node);     // "37173"
ca.county_name(node);   // "Swain County"
ca.state_fips(node);    // "37"
```

### CBSAAssignment (`gravel/us/cbsa_assignment.h`)

Typed wrapper for CBSA (Metropolitan/Micropolitan Statistical Area) regions.

```cpp
CBSAAssignment ca = assign_cbsas(graph, "tiger_cbsas.geojson");
ca.cbsa_code(node);     // "11700"
ca.cbsa_name(node);     // "Asheville, NC"
```

### FIPSCrosswalk (`gravel/us/fips_crosswalk.h`)

County-to-CBSA-to-state lookup table built from overlapping region assignments.

```cpp
auto xwalk = build_fips_crosswalk(county_assignment, &cbsa_assignment);
xwalk.county_to_state("37173");       // "37"
xwalk.county_to_cbsa("37173");        // "11700" or ""
xwalk.counties_in_state("37");        // ["37001", "37003", ..., "37199"]
xwalk.counties_in_cbsa("11700");      // ["37021", "37087", "37089", "37115"]
```

---

## 29. Reduced Graph (`gravel/simplify/reduced_graph.h`)

Reduces a partitioned graph into a compact representation preserving inter-regional structure:
- One **central node** per region (selected by geometric centroid, degree, or caller-provided)
- All **border nodes** (nodes with edges crossing region boundaries)
- **Intra-region edges**: central → each border node, weighted by precomputed CH distance on the original graph
- **Inter-region edges**: original border-crossing edges, preserved unchanged

The reduced graph has 10–1000× fewer nodes than the original, making inter-regional analysis fast even at national scale.

**Region-agnostic.** The core function `build_reduced_graph()` takes a plain `std::vector<int32_t>` region index and works with any partition scheme — US counties, European NUTS codes, or custom clusters. The `build_reduced_geography_graph()` in `gravel/geo/geography_skeleton.h` is a thin adapter for `RegionAssignment`.

### ReducedGraphConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `method` | `Centrality` | `GEOMETRIC_CENTROID` | How to pick central nodes. |
| `precomputed_centrals` | `vector<NodeID>` | `{}` | For `PROVIDED` method: caller's choice (1 per region). |
| `seed` | `uint64_t` | `42` | Random seed (reserved for future methods). |

`Centrality` enum:
- `GEOMETRIC_CENTROID` — node closest to polygon centroid (fast, deterministic; requires `RegionInfo::boundary`)
- `HIGHEST_DEGREE` — node with most outgoing edges in the region
- `PROVIDED` — caller-supplied list (e.g., highest-betweenness nodes computed externally)

### ReducedGraph

| Field | Type | Description |
|-------|------|-------------|
| `graph` | `unique_ptr<ArrayGraph>` | The reduced graph. |
| `node_region` | `vector<string>` | Region ID per reduced node (empty if unassigned). |
| `is_central` | `vector<bool>` | True for central nodes, false for border nodes. |
| `central_of` | `unordered_map<string, NodeID>` | Region ID → central node ID in reduced graph. |
| `reduced_to_original` | `vector<NodeID>` | Reduced ID → original node ID. |
| `original_to_reduced` | `unordered_map<NodeID, NodeID>` | Original ID → reduced ID. |
| `inter_region_edges` | `unordered_map<RegionPair, vector<pair<NodeID, NodeID>>>` | Per-pair list of inter-region directed edges (in reduced node IDs). |

### Functions

```cpp
// Generic (gravel-simplify) — works with any region partition
ReducedGraph build_reduced_graph(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const std::vector<int32_t>& node_region,    // -1 for unassigned, else region index
    const std::vector<RegionInfo>& regions,     // {region_id, optional boundary}
    const ReducedGraphConfig& config = {});

// Geography adapter (gravel-geo) — wraps with RegionAssignment
ReducedGraph build_reduced_geography_graph(
    const ArrayGraph& graph,
    const ContractionResult& ch,
    const RegionAssignment& assignment,
    const BorderEdgeResult& border,
    const ReducedGraphConfig& config = {});
```

### Pipeline (Python)

```python
# Pick high-betweenness central per county (PROVIDED method)
bet = gravel.edge_betweenness(graph, gravel.BetweennessConfig())
import numpy as np
node_scores = np.array(bet.node_scores)
region_index = np.array(assignment.region_index)
centrals = []
for ridx in range(len(assignment.regions)):
    candidates = np.where(region_index == ridx)[0]
    centrals.append(int(candidates[np.argmax(node_scores[candidates])]))

cfg = gravel.ReducedGraphConfig()
cfg.method = gravel.ReducedGraphConfig.Centrality.PROVIDED
cfg.precomputed_centrals = centrals
reduced = gravel.build_reduced_geography_graph(graph, ch, assignment, border, cfg)
```

---

## 30. Inter-Region Fragility (`gravel/fragility/inter_region_fragility.h`)

Progressive edge-removal fragility analysis on the reduced geography graph. For each adjacent pair (A, B):

1. Baseline = CH distance from `central_A` to `central_B` on intact reduced graph
2. For each Monte Carlo trial:
   - Pick `k_max` random inter-region edges from shared(A, B)
   - Block all `k_max`, run IncrementalSSSP from `central_A`
   - Record dist[`central_B`] at k = k_max (worst case)
   - Restore edges one at a time, record at each k
3. Aggregate: per-k mean travel time, std, disconnection fraction; AUC of inflation and disconnection

### InterRegionFragilityConfig

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `k_max` | `uint32_t` | `20` | Max inter-region edges to block per pair (capped by shared edges). |
| `monte_carlo_runs` | `uint32_t` | `10` | MC trials per pair. |
| `seed` | `uint64_t` | `42` | Random seed. |

### InterRegionPairResult

| Field | Type | Description |
|-------|------|-------------|
| `source_region`, `target_region` | `string` | Region IDs (e.g., FIPS codes). |
| `baseline_seconds` | `Weight` | Intact-network central-to-central travel time. |
| `curve` | `vector<InterRegionLevel>` | Degradation curve. `curve[0]` = all blocked, `curve[k_max]` = restored. |
| `auc_inflation` | `double` | Mean travel-time inflation (ratio − 1) over the curve. |
| `auc_disconnection` | `double` | Mean disconnection fraction over the curve. ∈ [0, 1]. |
| `shared_border_edges` | `uint32_t` | Total inter-region edges between this pair. |
| `k_used` | `uint32_t` | `min(k_max, shared_border_edges)`. |

### Function

```cpp
InterRegionFragilityResult inter_region_fragility(
    const ReducedGraph& reduced,
    const InterRegionFragilityConfig& config = {});
```

### Performance

| Operation | Time |
|-----------|------|
| Build reduced graph (Delaware, 709K nodes → 245 reduced) | <0.1s |
| Inter-region fragility (2 pairs, MC=5, k=10) | <0.1s |
| Full state pipeline (download + CH + skeleton + fragility) | ~30–40s |
| National (50+ states, 8,547 adjacent pairs incl. 1,082 cross-state) | ~22 hrs measured |

### Actual national results (April 2026)

- 3,221 counties analyzed for per-county isolation fragility in 3.1 hours
- 8,547 adjacent county pairs analyzed for inter-county fragility in ~22 hours
- 1,082 cross-state pairs correctly captured via adjacency-driven pipeline with `osmium merge`
- Full results in `data/sample-results/`
