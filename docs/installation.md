# Installation

Gravel is available via conda-forge (recommended for C++ dependencies) and PyPI (source build).

## conda (recommended)

```bash
conda install -c conda-forge gravel-routing
```

This installs the full library including OSM support (via libosmium from conda-forge).

## pip

```bash
pip install gravel-routing
```

**Note:** The PyPI wheels do not include OSM loading support (libosmium is not available on PyPI). For OSM support via pip, install libosmium on your system first and build from source:

### macOS

```bash
brew install libosmium
pip install gravel-routing --no-binary gravel-routing
```

### Debian / Ubuntu

```bash
sudo apt-get install libosmium2-dev libeigen3-dev nlohmann-json3-dev
pip install gravel-routing --no-binary gravel-routing
```

### RHEL / Fedora

```bash
sudo dnf install libosmium-devel eigen3-devel nlohmann-json-devel
pip install gravel-routing --no-binary gravel-routing
```

## From source

Clone the repository:

```bash
git clone https://github.com/rhoekstr/gravel.git
cd gravel
```

Install system dependencies (see above), then:

```bash
# Build C++ library + Python bindings + CLI
cmake -B build \
    -DGRAVEL_BUILD_PYTHON=ON \
    -DGRAVEL_BUILD_CLI=ON \
    -DGRAVEL_USE_OSMIUM=ON \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Install Python package
pip install -e .
```

## Verify installation

```python
import gravel

# Check version
print(gravel.__version__)  # 2.1.0

# Try a simple grid graph
g = gravel.make_grid_graph(10, 10)
ch = gravel.build_ch(g)
print(f"Built CH for {g.node_count}-node graph")
```

## Optional dependencies

For visualization and the national pipeline script:

```bash
pip install gravel-routing[viz,pipeline]
```

This pulls in `plotly`, `geopandas`, `shapely`, and `pyproj`.

## Requirements

- **C++20** compiler (GCC 11+, Clang 14+, MSVC 2022+)
- **CMake 3.20+**
- **Python 3.10+** (for Python bindings)

## Troubleshooting

### "Could not find libosmium"

libosmium is a system library that isn't distributable via PyPI. Install it through your OS package manager (see above) or use the conda-forge distribution.

### Build fails on Windows

Ensure you have Visual Studio 2022 with C++ tools installed. Native Windows builds are supported via cibuildwheel; if building from source fails, file an issue with the full CMake log.

### Python import fails with "module _gravel not found"

The C++ extension didn't build or isn't in your Python path. Check that `pip install -e .` completed successfully and your virtual environment is active.
