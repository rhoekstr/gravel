# Installation

Gravel is distributed on **PyPI**. Binary wheels ship for Linux (x86_64, aarch64),
macOS (arm64), and Windows (AMD64) across Python 3.10–3.13, **with OSM loading built
in** (libosmium is bundled into the wheels from v2.2.2 onward — no system libraries
required).

> **conda-forge is not currently a supported channel.** The feedstock is stale; install
> from PyPI. (If it is revived, this page will note it.)

## pip (recommended)

```bash
pip install gravel-fragility
```

That's it — the wheel includes OSM support. Verify with `gravel.HAS_OSM` (see below).

### Optional extras

```bash
pip install "gravel-fragility[interop]"   # NetworkX / GeoPandas adapters
pip install "gravel-fragility[viz]"        # static + interactive maps
```

- **`[interop]`** → `networkx`, `geopandas`, `shapely`, `pyproj` (the `gravel.interop` adapters).
- **`[viz]`** → `matplotlib`, `lonboard`, `pyarrow`, `geopandas`, `shapely`, `pyproj` (the
  `gravel.viz` renderers: `plot_fragility`, `interactive_map`, `animate_failure`).

## From source

A source build is only needed to develop Gravel or to target a platform without a wheel.
OSM support is auto-detected (`GRAVEL_USE_OSMIUM=AUTO`); install libosmium first to enable it.

```bash
git clone https://github.com/rhoekstr/gravel.git
cd gravel
# System deps for OSM (optional): macOS `brew install libosmium protozero`;
# Debian/Ubuntu `sudo apt-get install libosmium2-dev`; conda `conda install -c conda-forge libosmium`.
cmake -B build \
    -DGRAVEL_BUILD_PYTHON=ON \
    -DGRAVEL_BUILD_CLI=ON \
    -DGRAVEL_USE_OSMIUM=AUTO \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
pip install -e .
```

(Eigen is vendored under `third_party/`, so no system Eigen is required.)

## Verify installation

```python
import gravel

print(gravel.__version__)
print("OSM support:", gravel.HAS_OSM)          # True on PyPI wheels
print("OpenMP:", gravel.HAS_OPENMP)

g = gravel.make_grid_graph(10, 10)
ch = gravel.build_ch(g)
print(f"Built CH for {g.node_count}-node graph")
```

## Requirements

- **C++20** compiler (GCC 11+, Clang 14+, MSVC 2022+) — source builds only
- **CMake 3.24+** — source builds only
- **Python 3.10+**

## Troubleshooting

### `gravel.HAS_OSM` is `False`

You're on a build without libosmium (a source build where it wasn't found). PyPI wheels
ship with OSM enabled; from source, install libosmium (see above) and pass
`-DGRAVEL_USE_OSMIUM=ON` to fail fast if it's missing.

### Build fails on Windows

Ensure Visual Studio 2022 with C++ tools is installed. If a source build fails, file an
issue with the full CMake log.

### Python import fails with "module _gravel not found"

The C++ extension didn't build or isn't on your Python path. Check that `pip install -e .`
completed successfully and your virtual environment is active.
