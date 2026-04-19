# Routing and Route Fragility Benchmarks

Generated: 2026-04-19 10:03:43

## Hardware

Run on the Gravel development machine. Numbers will vary by CPU.

## Summary

| Graph | Nodes | Edges | Load | CH build | CH dist (µs) | CH route (µs) | Matrix cell (µs) | Route frag (ms) |
|-------|------:|------:|-----:|---------:|-------------:|--------------:|-----------------:|----------------:|
| swain_county.osm.pbf | 200,418 | 398,707 | 0.36s | 0.85s | 3.8 | 82.7 | 5.5 | 7757.2 |
| buncombe_county.osm.pbf | 592,880 | 1,189,606 | 0.99s | 3.92s | 8.8 | 144.8 | 6.3 | 17337.9 |

## Detail

**CH distance query**: single `q.distance(s, t)` call; no path unpacking.

**CH route query**: `q.route(s, t)` with path unpacking. Cost scales with path length.

**Distance matrix**: `q.distance_matrix(sources, targets)` — parallelized via OpenMP.

**Route fragility**: `route_fragility(ch, idx, g, s, t)` — computes a replacement-path distance for every edge on the shortest path via `BlockedCHQuery`. Cost ≈ (path length) × (per-edge blocked query).

Per-edge blocked query cost:

- **swain_county.osm.pbf**: 59518.2 µs/edge (path avg 130 edges → 7757.2 ms/route)
- **buncombe_county.osm.pbf**: 124435.0 µs/edge (path avg 139 edges → 17337.9 ms/route)
