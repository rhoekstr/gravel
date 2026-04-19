# Gravel Documentation

**Fast road network fragility analysis at scale.**

```{toctree}
:caption: Getting Started
:maxdepth: 2

getting_started
installation
quickstart
```

```{toctree}
:caption: User Guide
:maxdepth: 2

PRD
concepts
pipeline
```

```{toctree}
:caption: API Reference
:maxdepth: 2

api/python
api/cpp
```

```{toctree}
:caption: Examples
:maxdepth: 1

examples/location_fragility
examples/national_analysis
examples/scenario_analysis
```

```{toctree}
:caption: Project
:maxdepth: 1

../CHANGELOG
../CONTRIBUTING
```

## Overview

Gravel is a C++ library with first-class Python bindings that quantifies how vulnerable road networks are to edge failures. It combines contraction-hierarchy routing with replacement-path fragility analysis to produce composite isolation scores for geographic regions.

### Why Gravel?

Traditional routing libraries answer "what is the shortest path?" Gravel answers "how does that path degrade when edges fail?" This matters for:

- Disaster preparedness and emergency management
- Infrastructure planning and redundancy analysis
- Transportation equity research
- Resilience engineering

### Quick Example

```python
import gravel

# Load a road network
graph = gravel.load_osm_graph("county.osm.pbf", gravel.SpeedProfile.car())
ch = gravel.build_ch(graph)

# Compute isolation fragility
cfg = gravel.LocationFragilityConfig()
cfg.center = gravel.Coord(35.43, -83.45)
cfg.radius_meters = 30000

result = gravel.location_fragility(graph, ch, cfg)
print(f"Isolation risk: {result.isolation_risk:.3f}")
```

## Features

- **Fast CH construction** — 0.7s for 200K-node graphs
- **Scalable fragility** — ~2s per county at full US scale
- **Multiple strategies** — Monte Carlo, greedy betweenness, greedy fragility
- **Geographic awareness** — point-in-polygon region assignment, boundary-aware simplification
- **US-ready** — TIGER/Line loaders, FIPS crosswalks built-in
- **Production-grade** — 200+ unit tests, real-world validation

## Indices and tables

- {ref}`genindex`
- {ref}`modindex`
- {ref}`search`
