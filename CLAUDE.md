# CLAUDE.md

The single reference for working in this repository — what Gravel is, its architecture, the
invariants, and how to build/test it. **Canonical, version-controlled copy.** Keep it in sync with
the code (and with `REFERENCE.md` / `CHANGELOG.md` / `docs/PRD.md`).

> Gravel is **published** on PyPI (`gravel-fragility`; current **2.4.0**). Changes here ship to real
> users — preserve the public API and the wheel build, or bump versions deliberately. (conda-forge is
> stale/unmaintained — PyPI is the only live channel.)

## The one rule that can't bend (READ FIRST): the sub-library DAG

Gravel is **six linkable libraries with a strict, one-directional dependency DAG**. An include that
crosses a boundary the wrong way is a **build error**, not a style note. Link/compile only what you
need; put new code in the lowest layer that suffices and never reach upward.

| Library | May depend on | Must NOT depend on |
|---|---|---|
| `gravel-core` | stdlib, OpenMP | everything else |
| `gravel-ch` | core | simplify, fragility, geo, us |
| `gravel-simplify` | core, ch | fragility, geo, us |
| `gravel-fragility` | core, ch, simplify | geo, us |
| `gravel-geo` | core, simplify | fragility, us |
| `gravel-us` | geo | fragility |

Full rationale in [`docs/PRD.md`](docs/PRD.md) → "Architecture Overview".

## Overview — what Gravel is

A C++20 library (with Python bindings) that computes how vulnerable road/infrastructure networks are
to edge failures — *"how isolated does this place become when N% of its roads fail?"* Built on
**contraction hierarchies** for fast shortest paths plus a Dijkstra / incremental-SSSP pipeline for
edge-removal analysis. ~2s isolation fragility on a 200K-node county graph. Apache-2.0. Dual purpose:
a network-fragility research tool and a workforce-planning resource (Awry Labs). (Originally a
disaster-sociology dissertation covariate; that tie ended with a null result — it's a general tool now.)

## Architecture — where code lives

Public headers in `include/gravel/<module>/`, implementations in `src/<module>/` (parallel trees).
Modules map onto the six linkable libraries above:

| Path | What |
|---|---|
| `core/` | graph representation (structure-of-arrays), basic routing, OpenMP |
| `ch/` | contraction hierarchy + blocked queries |
| `simplify/` | graph simplification, bridges, degree-2 collapse |
| `fragility/` | all fragility analysis (route / location / county / scenario / progressive / tiled); Eigen + Spectra |
| `geo/` | OSM loading (libosmium), regions, snapping, point-in-polygon |
| `us/` | US TIGER/Census specializations |
| `algo/ · analysis/ · io/ · snap/ · validation/` | shared algorithms, analysis orchestration, I/O (incl. optional Arrow/Parquet), snapping, input validation |
| `include/gravel/gravel.h` | umbrella header |
| `python/bindings.cpp` | pybind11 bindings → the `gravel` module (`python/gravel/__init__.py`) |
| `python/gravel/interop.py` | pure-Python NetworkX / GeoPandas adapters (`gravel[interop]` extra) |
| `python/gravel/hazards.py` | hazard footprints → per-edge failure probabilities (floodplain/NFHL → `stochastic_fragility`) |
| `python/gravel/viz.py` | fragility results → plot-ready per-edge failure traces / `GeoDataFrame` (viz data bridge) |
| `cli/cmd_*.cpp` | command-line tools (`build_graph`, `build_ch`, `batch_fragility`, …) |
| `tests/test_*.cpp` | Catch2 unit tests (+ `python/tests/` pytest) |
| `bench/ · scripts/` | benchmarks + national-run scripts (`scripts/national_fragility.py`) |
| `examples/{cpp,python}/` | sample programs / notebooks |

OSM support is **optional and detected**: `GRAVEL_USE_OSMIUM=AUTO` enables it when libosmium is
present, off gracefully otherwise. Guard runtime use with `gravel.HAS_OSM` (Python) /
`GRAVEL_HAS_OSMIUM` (C++) — never assume OSM is compiled in. Apache Arrow (`GRAVEL_USE_ARROW`) is
similarly optional for Parquet output; guard runtime use with `gravel.HAS_ARROW` (Python) /
`GRAVEL_HAS_ARROW` (C++). Per-edge OSM tags reach Python via `load_osm_graph_with_metadata` →
`EdgeMetadata`, aligned in CSR edge order with `Graph.to_coo()`.

**OpenMP is optional and detected too** (`cmake/OpenMPDetect.cmake`) — it powers the parallel kernels
(fragility, betweenness, distance matrices, `route_fragility` over path edges, MC runs). On Apple
Clang it's found via Homebrew `libomp` (`brew install libomp`); without it the build is **silently
serial**, so check `gravel.HAS_OPENMP` (Python) / `GRAVEL_HAS_OPENMP` (C++), and `gravel.max_threads()`
/ `set_max_threads(n)`. For reproducible covariates use `BetweennessConfig.deterministic=True`
(bit-identical across thread counts). When parallelizing *across* analyses with processes
(`national_fragility.py --jobs N`), cap each worker's threads to avoid oversubscription.

## Build & test

```bash
# Debug build with tests + Python bindings
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DGRAVEL_BUILD_TESTS=ON -DGRAVEL_BUILD_PYTHON=ON
cmake --build build -j
ctest --test-dir build --output-on-failure      # C++ (Catch2)
cd python && pytest tests/                        # Python bindings

# OSM-dependent regression tests need libosmium (brew install libosmium protozero):
cmake -B build -DGRAVEL_USE_OSMIUM=ON
```

Options: `GRAVEL_BUILD_{CLI,PYTHON,TESTS,BENCH}`, `GRAVEL_USE_OSMIUM` (ON/OFF/AUTO), `GRAVEL_USE_ARROW`.
`tests/test_real_osm.cpp` and `tests/test_performance_profile.cpp` require `GRAVEL_USE_OSMIUM=ON` —
don't break them without a good reason.

## Conventions

- **C++20** — concepts, ranges, `std::span`; `#pragma once`; **structure-of-arrays** on hot paths; no
  raw `new`/`delete` (use `std::make_unique`); `const` by default; Doxygen on every public symbol.
  Public symbols in `gravel::`, implementation details in `gravel::internal::`. Include order: own
  header → project → system.
- **Python** — PEP 8 (ruff); type hints + docstrings on the public API.
- **Commits** — Conventional Commits, scoped: `feat(location_fragility): …`, `fix(ch_query): …`,
  `perf(bridges): …`. Branch from `main`; each logical change is its own commit; `ctest` green before a PR.
- **The public API is a shipped contract.** Changing it means: update `REFERENCE.md`, add a
  `CHANGELOG.md` "Unreleased" entry, and keep the pybind11 surface in sync. Every feature gets tests.

## Documentation set (keep current, don't proliferate)

| Doc | Update when you change… |
|---|---|
| **CLAUDE.md** (this) | the module map, the DAG, build/test, conventions |
| `README.md` | the front-door overview, install, headline performance |
| `REFERENCE.md` | any public API (functions, types) |
| `docs/PRD.md` | architecture / requirements, the DAG rationale |
| `CHANGELOG.md` | every user-visible change (Keep-a-Changelog, SemVer) |
| `CONTRIBUTING.md` | dev setup / style / PR process |

Settled decisions live in these docs — update the doc **in the same change** as the code.
