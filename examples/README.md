# Gravel Examples

Runnable examples for common use cases, in both Python and C++.

## Python Examples

All Python examples assume the `gravel` package is installed:

```bash
pip install gravel-fragility
# Or from source: pip install -e .
```

### [01_basic_routing.py](python/01_basic_routing.py)
Build a synthetic graph, compute a contraction hierarchy, run shortest-path queries.

```bash
python python/01_basic_routing.py
```

### [02_location_fragility.py](python/02_location_fragility.py)
Compute isolation fragility for a geographic point using real OSM data.

```bash
# First download an OSM PBF file, then:
python python/02_location_fragility.py path/to/county.osm.pbf
```

### [03_county_analysis.py](python/03_county_analysis.py)
Assign graph nodes to counties, summarize border edges, compute per-county fragility.

```bash
python python/03_county_analysis.py county.osm.pbf counties.geojson
```

## C++ Examples

C++ examples link directly against the Gravel sub-libraries. Build the main project first (`cmake --build build`), then compile each example.

### [01_basic_routing.cpp](cpp/01_basic_routing.cpp)
Basic routing with synthetic graphs and contraction hierarchy.

```bash
cd cpp
g++ -std=c++20 -I../../include 01_basic_routing.cpp \
    -L../../build -lgravel-core -lgravel-ch \
    -o basic_routing
./basic_routing
```

### [02_location_fragility.cpp](cpp/02_location_fragility.cpp)
Location fragility analysis with OSM data. Requires libosmium.

```bash
cd cpp
g++ -std=c++20 -I../../include 02_location_fragility.cpp \
    -L../../build -lgravel -losmium -lz -lbz2 -lexpat \
    -o location_fragility
./location_fragility county.osm.pbf
```

## National US Pipeline

The full production pipeline lives in `scripts/`:

```bash
# Run the national analysis (downloads ~10GB of OSM data, takes ~3 hours)
python ../scripts/national_fragility.py --output-dir output/

# Visualize the results
python ../scripts/visualize_results.py
```

See the main [README](../README.md) for pipeline details.
