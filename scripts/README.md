# Scripts

Runnable scripts for the Gravel project.

## Analysis pipelines

- **`national_fragility.py`** — Per-county isolation risk. Downloads state OSM
  PBFs from Geofabrik, extracts per-county road networks with 10km buffer, runs
  `location_fragility` on each, outputs a CSV by FIPS. Sample output is in
  `data/sample-results/county_isolation_fragility.csv`.

- **`national_inter_county.py`** — Inter-county fragility using the geography
  skeleton approach. For each state, builds a reduced graph (one central node
  per county + border nodes, ~hundreds of nodes total), then runs progressive
  edge-removal fragility for all adjacent county pairs. Output CSV has one row
  per adjacent pair: baseline + degraded travel times, AUC metrics, and shared
  border edge counts.

- **`visualize_results.py`** — Generates interactive HTML choropleth, distribution,
  and scatter visualizations from a national fragility CSV.

## Build and perf

- **`perf_check.py`** — Compares Google Benchmark JSON output against a baseline
  in `bench/baselines/perf_baseline.json`. Exits non-zero on regressions.

- **`pgo_build.sh`** — Two-pass profile-guided optimization build.

## Helpers

- **`geofabrik_urls.py`** — State FIPS → Geofabrik PBF download URL mapping.
- **`state_names.py`** — State FIPS → state name mapping.
