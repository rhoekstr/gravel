# Routing and Route Fragility Benchmarks

Generated: 2026-07-01 (regenerated for the 2.4.0 cycle)

## Hardware & build

Apple M-series laptop, 10 cores. Gravel built **Release** (`-O3`) with OpenMP enabled
(Apple Clang + Homebrew `libomp`) and libosmium. Reproduce with:

```bash
cmake -B build-perf -DCMAKE_BUILD_TYPE=Release -DGRAVEL_BUILD_PYTHON=ON -DGRAVEL_USE_OSMIUM=ON
cmake --build build-perf -j --target _gravel
python scripts/benchmark_routing.py
```

Numbers will vary by CPU. This table is a documentation baseline; the machine-pinned
Google-Benchmark **regression gate** lives in `perf_baseline.json` (`gravel_perf`, refreshed
separately) and is not updated here.

## Summary

| Graph | Nodes | Edges | Load | CH build | CH dist (µs) | CH route (µs) | Matrix cell (µs) | Route frag (ms/edge) |
|-------|------:|------:|-----:|---------:|-------------:|--------------:|-----------------:|---------------------:|
| swain_county.osm.pbf | 200,418 | 398,707 | 0.43s | 0.78s | 3.5 | 80.5 | 0.6 | 13.3 |
| buncombe_county.osm.pbf | 592,880 | 1,189,606 | 0.96s | 3.81s | 7.8 | 112.8 | 1.3 | 27.5 |

## Parallel scaling (Swain Co., this machine)

Distance matrix (120×120) and `route_fragility` (single 109-edge path), best-of-3, by thread count:

| Threads | Matrix cell (µs) | speedup | route_fragility (s) | speedup |
|--------:|-----------------:|--------:|--------------------:|--------:|
| 1  | 1.60 | 1.0× | 1.68 | 1.0× |
| 2  | 0.85 | 1.9× | 0.89 | 1.9× |
| 4  | 0.46 | 3.5× | 0.48 | 3.5× |
| 10 | 0.32 | 5.0× | 0.34 | 5.0× |

Both parallel kernels scale ~5× on 10 cores (memory-bandwidth-bound past ~4 threads).

## What changed since the 2026-04-19 baseline

- **Distance matrix cell:** 5.5 → 0.6 µs (Swain), 6.3 → 1.3 µs (Buncombe).
- **Route fragility per edge:** ~60 → 13.3 ms (Swain), ~124 → 27.5 ms (Buncombe).
- Single-threaded ops (load, CH build, point queries) are unchanged.

Cause: macOS gained working OpenMP in 2.3.0 (Apple Clang needs Homebrew `libomp`; the April
run was effectively serial on macOS), and `route_fragility` was parallelized across path edges
in the same release. The 1-thread column above reproduces the old serial regime; the 10-thread
column is what 2.4.0 wheels deliver. Attribution is causal, not hardware variance — the same
machine shows the full 1→10-thread curve.

## Detail

**CH distance query**: single `q.distance(s, t)` call; no path unpacking.

**CH route query**: `q.route(s, t)` with path unpacking. Cost scales with path length.

**Distance matrix**: `q.distance_matrix(sources, targets)` — parallelized via OpenMP.

**Route fragility**: `route_fragility(ch, idx, g, s, t)` — computes a replacement-path distance
for every edge on the shortest path via `BlockedCHQuery`, parallelized across path edges. Cost ≈
(path length) × (per-edge blocked query).

Per-edge blocked query cost (10 threads):

- **swain_county.osm.pbf**: 13287.7 µs/edge (path avg 130 edges → 1731.8 ms/route)
- **buncombe_county.osm.pbf**: 27521.2 µs/edge (path avg 139 edges → 3834.6 ms/route)

**Location fragility** (`location_fragility`, MC=20, 50-mile radius, centroid center, default
sampling): Swain 0.11 s, Buncombe 1.0 s. Config-sensitive (sample count, radius) — not directly
comparable to earlier runs whose config differed.
