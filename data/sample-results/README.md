# Sample Results

Real output from running the Gravel fragility pipeline.

## `county_isolation_fragility.csv`

Isolation risk scores for all **3,221 US counties** (including Alaska, Hawaii, Puerto Rico), generated April 2026 using Gravel v2.1 and OpenStreetMap data from Geofabrik.

**How it was produced:**
```bash
python scripts/national_fragility.py --output-dir output/ --mc-runs 15
```

**Runtime:** 3.1 hours on an Apple M-series laptop.

## Schema

| Column | Description |
|--------|-------------|
| `fips` | 5-digit FIPS code (state + county) |
| `county_name` | County name (e.g., "Swain County") |
| `state_fips` | 2-digit state FIPS |
| `state_name` | Full state name |
| `isolation_risk` | Composite isolation score ∈ [0, 1]. Higher = more vulnerable. |
| `auc_normalized` | Area under the degradation curve, normalized by length |
| `baseline_isolation_risk` | Score with zero edges removed (~0) |
| `reachable_nodes` | Nodes reachable from county centroid within radius |
| `sp_edges_total` | Shortest-path edges identified |
| `sp_edges_removed` | Edges removed (k_max) — 10% of sp_edges_total |
| `subgraph_nodes` | Simplified subgraph node count after degree-2 contraction |
| `subgraph_edges` | Simplified subgraph edge count |
| `directional_coverage` | Fraction of 8 compass sectors still reachable at max removal |
| `directional_asymmetry` | HHI of per-sector fragility concentration |
| `graph_nodes` | Nodes in the extracted county PBF |
| `graph_edges` | Edges in the extracted county PBF |
| `radius_meters` | Search radius used (30km default, 80km for AK, 20km for PR) |
| `mc_runs` | Monte Carlo trials per county |
| `notes` | Error notes if any (empty for successful runs) |

## Key findings

**Most vulnerable states (mean county risk):**
1. New Hampshire (0.638)
2. Maine (0.571)
3. Rhode Island (0.570)
4. Connecticut (0.563)
5. Vermont (0.514)

**Most resilient states (mean county risk):**
1. Kansas (0.146)
2. Nebraska (0.162)
3. Iowa (0.163)
4. North Dakota (0.165)
5. South Dakota (0.179)

The pattern is clear: Great Plains grid-states (flat, rectangular road networks with lots of redundancy) score lowest. Forested/mountain/coastal states with constrained geography score highest.

## Visualizations

The HTML visualizations are not committed to the repo (they're large and regeneratable). To produce them:

```bash
python scripts/visualize_results.py
```

This creates:
- `national_fragility_map.html` — interactive choropleth of all US counties
- `national_fragility_stats.html` — distribution + rankings
- `national_fragility_scatter.html` — graph size vs. risk

## Validation

- Swain County, NC (FIPS 37173): risk=0.65, matches the regression test target
- DC: risk=0.055, matches the low-risk urban-area expectation
- Los Angeles County: risk=0.456, matches expectation for a large but well-connected metro

---

## `inter_county_fragility.csv`

Inter-county fragility for **8,547 adjacent county pairs** (including 1,082 cross-state pairs), generated April 2026 using the adjacency-driven pipeline.

**How it was produced:**
```bash
python scripts/national_inter_county.py --mc-runs 10 --k-max 20
```

**Runtime:** ~22 hours on an Apple M-series laptop.

### Schema

| Column | Description |
|--------|-------------|
| `source_fips`, `target_fips` | 5-digit FIPS codes |
| `baseline_minutes` | Intact-network travel time between county centrals |
| `shared_border_edges` | Inter-county roads crossing the boundary |
| `k_used` | Edges actually blocked (min of k_max=20 and shared) |
| `auc_inflation` | AUC of (mean_degraded / baseline − 1) across the curve |
| `auc_disconnection` | AUC of disconnection fraction. ∈ [0, 1]. Higher = more fragile. |
| `worst_case_minutes` | Mean travel time at maximum removal |
| `worst_case_disconnected_frac` | Fraction of MC trials fully disconnected at max removal |
| `restored_minutes` | Travel time with all edges restored (should equal baseline) |
| `states` | Concatenated state FIPS (e.g., "01+13" for AL+GA cross-state) |
| `is_cross_state` | Boolean — whether this pair crosses a state border |

### Key findings

**Most fragile connections (highest disconnection risk):**
- Anchorage → Kenai Peninsula, AK (auc_disc=0.333, only 2 shared edges)
- Dorchester → Wicomico, MD (auc_disc=0.200, Eastern Shore with limited crossings)
- Mountain/island counties dominate the top fragile list

**Most redundant connections:**
- West Virginia, Oklahoma, Texas pairs with 20-28 shared border edges
- Grid road networks (Plains states) have substantial redundancy

### Cross-state pair handling

1,082 of the 8,547 pairs (~13%) cross a state border. These are captured by the
adjacency-driven pipeline which merges the relevant state OSM PBFs with
`osmium merge` before running the analysis. Examples:

- 27 AL/GA pairs (Chattahoochee River border)
- 22 GA/SC pairs (Savannah River)
- 19 FL/GA pairs
- 9 FL/AL pairs (Perdido River, Gulf Coast)
