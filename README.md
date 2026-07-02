# Gravel

**Fast road network fragility analysis at scale.**

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Python 3.10+](https://img.shields.io/badge/python-3.10+-blue.svg)](https://www.python.org/downloads/)
[![C++20](https://img.shields.io/badge/C++-20-orange.svg)](https://en.cppreference.com/w/cpp/20)

Gravel is a C++ library (with Python bindings) for computing how vulnerable road networks are to edge failures. Given a graph, it answers questions like:

- *"How isolated does this location become when 10% of its roads fail?"*
- *"Which counties are most dependent on a single critical route?"*
- *"What's the composite fragility score for every US county?"*

The library is built around contraction hierarchies for fast shortest-path queries, and a Dijkstra + incremental SSSP pipeline for edge-removal analysis. On a 200K-node county graph, it computes isolation fragility in ~2 seconds.

## Installation

### pip

```bash
pip install gravel-fragility
```

Binary wheels for Linux (x86_64, aarch64), macOS (x86_64, arm64), and Windows (AMD64) × Python 3.10–3.13. OSM loaders (`load_osm_graph`, `OSMConfig`, `SpeedProfile`) ship enabled on every wheel from v2.2.2 onward — no extra system dependencies required.

### conda-forge

Not currently available. The conda-forge feedstock is out of date and does **not** track recent
releases — install via `pip` (above) for the current version. (If the feedstock is revived, a
`conda install` path will return here.)

### From source

```bash
git clone https://github.com/rhoekstr/gravel.git
cd gravel
cmake -B build -DGRAVEL_BUILD_PYTHON=ON
cmake --build build -j
```

The default `GRAVEL_USE_OSMIUM=AUTO` enables OSM loaders when `libosmium` is present on the system and disables them gracefully when it isn't. CMake prints a clear status message either way. To hard-require libosmium (fail configure if missing), pass `-DGRAVEL_USE_OSMIUM=ON`. To opt out entirely, `-DGRAVEL_USE_OSMIUM=OFF`.

Install libosmium with:
- **macOS**: `brew install libosmium protozero`
- **Debian/Ubuntu**: `sudo apt install libosmium2-dev`
- **conda**: `conda install -c conda-forge libosmium`
- **vcpkg (Windows)**: `vcpkg install libosmium protozero`

### Checking OSM availability at runtime

```python
import gravel
if gravel.HAS_OSM:
    graph = gravel.load_osm_graph("county.osm.pbf", gravel.SpeedProfile.car())
else:
    # Running on a build without OSM support (e.g., source build without libosmium).
    raise RuntimeError("gravel was built without OSM support")
```

## Quick Start

### Python

```python
import gravel

# Load a road network (from OSM PBF)
graph = gravel.load_osm_graph("county.osm.pbf", gravel.SpeedProfile.car())

# Build contraction hierarchy (one-time cost)
ch = gravel.build_ch(graph)

# Compute isolation fragility for a location
cfg = gravel.LocationFragilityConfig()
cfg.center = gravel.Coord(35.43, -83.45)  # Bryson City, NC
cfg.radius_meters = 30000  # 30km
cfg.monte_carlo_runs = 20

result = gravel.location_fragility(graph, ch, cfg)
print(f"Isolation risk: {result.isolation_risk:.3f}")
print(f"Reachable nodes: {result.reachable_nodes}")
print(f"Directional coverage: {result.directional_coverage:.2f}")
```

### C++

```cpp
#include <gravel/gravel.h>

auto graph = gravel::load_osm_graph({"county.osm.pbf", gravel::SpeedProfile::car()});
auto ch = gravel::build_ch(*graph);

gravel::LocationFragilityConfig cfg;
cfg.center = {35.43, -83.45};
cfg.radius_meters = 30000;
cfg.monte_carlo_runs = 20;

auto result = gravel::location_fragility(*graph, ch, cfg);
std::cout << "Isolation risk: " << result.isolation_risk << "\n";
```

## Key Features

### Sub-library architecture
Six independent libraries with a strict dependency DAG — link only what you need:

| Library | Purpose | Dependencies |
|---------|---------|--------------|
| `gravel-core` | Graph representation, basic routing | stdlib, OpenMP |
| `gravel-ch` | Contraction hierarchy + blocked queries | gravel-core |
| `gravel-simplify` | Graph simplification, bridges | + gravel-ch |
| `gravel-fragility` | All fragility analysis (Eigen/Spectra) | + gravel-simplify |
| `gravel-geo` | OSM loading, regions, snapping (libosmium) | + gravel-simplify |
| `gravel-us` | US TIGER/Census specializations | + gravel-geo |

### Analysis modules

- **Route fragility** — per-edge replacement path analysis
- **Location fragility** — isolation risk for a geographic point (new Dijkstra+IncrementalSSSP)
- **County fragility** — composite index combining bridges, connectivity, accessibility, fragility
- **Scenario fragility** — event-conditional analysis (hazard footprints)
- **Progressive elimination** — degradation curve with Monte Carlo / greedy strategies
- **Tiled analysis** — spatial fragility fields for visualization
- **Region assignment** — node-to-polygon mapping (point-in-polygon)
- **Graph coarsening** — collapse regions into meta-nodes

### Performance

Measured on an Apple M-series laptop, 10 cores, Release build (2026-07-01; see
`bench/baselines/routing_performance.md`):

| Operation | 200K-node graph (Swain Co.) | 593K-node graph (Buncombe Co.) |
|-----------|-----------------------------:|--------------------------------:|
| OSM PBF load | 0.43s | 0.96s |
| CH build | 0.78s | 3.81s |
| CH distance query | 3.5 µs | 7.8 µs |
| CH route (with path unpacking) | 80.5 µs | 112.8 µs |
| Distance matrix cell (OpenMP, 10 threads) | 0.6 µs | 1.3 µs |
| Route fragility (per path edge, OpenMP) | ~13 ms | ~28 ms |
| Location fragility (MC=20, 50-mi radius) | 0.11 s | 1.0 s |

The parallel kernels (distance matrix, route fragility) scale ~5× from 1→10 threads on
this machine. macOS builds only gained working OpenMP in 2.3.0 (Apple Clang needs Homebrew
`libomp`) and `route_fragility` was parallelized in the same release — so on macOS those two
rows are roughly **5–9× faster than pre-2.3.0**. Single-threaded operations (load, CH build,
point queries) are unchanged. Numbers vary by CPU; the `perf_baseline.json` Google-Benchmark
regression gate is refreshed separately via `gravel_perf`.

At-scale benchmarks:
- **National per-county isolation fragility** (3,221 counties): 3.1 hours
- **National inter-county fragility** (8,547 adjacent pairs incl. cross-state): ~22 hours

## Documentation

- **[REFERENCE.md](REFERENCE.md)** — complete API reference (all functions, all types)
- **[docs/PRD.md](docs/PRD.md)** — product requirements and architecture
- **[docs/](docs/)** — full documentation site (also on GitHub Pages)
- **[examples/](examples/)** — Python notebooks and C++ sample programs

## Example: National US County Analysis

```python
# Run fragility analysis on all ~3,221 US counties
python scripts/national_fragility.py --output-dir output/

# Results are in output/county_isolation_fragility.csv
# Visualize with:
python scripts/visualize_results.py
```

Sample findings from the national run (April 2026):

| Most vulnerable states | Mean risk |
|-----------------------|-----------|
| New Hampshire | 0.638 |
| Maine | 0.571 |
| Rhode Island | 0.570 |
| Connecticut | 0.563 |

| Most resilient states | Mean risk |
|----------------------|-----------|
| Kansas | 0.146 |
| Nebraska | 0.162 |
| Iowa | 0.163 |
| North Dakota | 0.165 |

The Great Plains grid-states score lowest — flat land with rectangular road networks have extensive redundancy. Mountain and coastal states score highest — constrained geography forces single-path corridors.

## Requirements

**Runtime:**
- C++20 compiler (GCC 11+, Clang 14+, MSVC 2022+)
- CMake 3.24+
- Python 3.10+ (for bindings)

**Optional:**
- libosmium (for OSM PBF loading)
- Apache Arrow (for Parquet output)

**Bundled (via CMake FetchContent):**
- pybind11
- Eigen + Spectra
- nlohmann/json
- Catch2 (tests)

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Bug reports and feature requests welcome via GitHub Issues.

## License

Apache 2.0 — see [LICENSE](LICENSE). Free for commercial and research use.

## Citation

If you use Gravel in academic work, please cite:

```bibtex
@software{gravel2026,
  author = {Hoekstra, Robert},
  title = {Gravel: Fast Road Network Fragility Analysis},
  year = {2026},
  url = {https://github.com/rhoekstr/gravel},
  version = {2.4.0}
}
```

## About

Gravel is an [Awry Labs](https://awrylabs.com/) project — see the [Gravel project page](https://awrylabs.com/gravel.html) for an overview. Also from Awry Labs: [Kindling](https://awrylabs.com/kindling.html).

Built by Robert Hoekstra — more projects and writing at [awrylabs.com](https://awrylabs.com/).
