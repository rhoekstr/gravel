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

### conda (recommended for C++ dependencies)

```bash
conda install -c conda-forge gravel-fragility
```

### pip (source build — requires C++ compiler)

```bash
pip install gravel-fragility
```

For OSM data loading, install libosmium separately (`brew install libosmium` on macOS, `apt install libosmium2-dev` on Debian/Ubuntu).

### From source

```bash
git clone https://github.com/rhoekstr/gravel.git
cd gravel
cmake -B build -DGRAVEL_BUILD_PYTHON=ON -DGRAVEL_USE_OSMIUM=ON
cmake --build build -j
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

Measured on an Apple M-series laptop (see `bench/baselines/routing_performance.md`):

| Operation | 200K-node graph (Swain Co.) | 593K-node graph (Buncombe Co.) |
|-----------|-----------------------------:|--------------------------------:|
| OSM PBF load | 0.36s | 0.99s |
| CH build | 0.85s | 3.92s |
| CH distance query | 3.8 µs | 8.8 µs |
| CH route (with path unpacking) | 82.7 µs | 144.8 µs |
| Distance matrix cell (OpenMP) | 5.5 µs | 6.3 µs |
| Route fragility (per path edge) | ~60 ms | ~124 ms |
| Location fragility (MC=20) | 2.1 s | ~8 s |

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
- CMake 3.20+
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
  version = {2.2.0}
}
```

## About the Author

Built by Robert Hoekstra — more projects and writing at [awrylabs.com](https://awrylabs.com/).
