#!/usr/bin/env python3
"""Benchmark routing and route fragility on real OSM data.

Measures:
  1. Graph load time
  2. CH build time
  3. Single CH query (point-to-point routing) — µs per query
  4. Batch distance matrix — per-query cost
  5. Route fragility (per-edge analysis) — per-pair cost at scale

Writes Markdown summary to bench/baselines/routing_performance.md.

Usage:
    python scripts/benchmark_routing.py [--pbf PATH]
"""

import argparse
import random
import statistics
import sys
import time
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
PROJECT_DIR = SCRIPT_DIR.parent
sys.path.insert(0, str(PROJECT_DIR / "build" / "python"))
sys.path.insert(0, str(PROJECT_DIR / "python"))

import gravel


def timeit(label, fn, *args, **kw):
    t0 = time.perf_counter()
    result = fn(*args, **kw)
    dt = time.perf_counter() - t0
    print(f"  {label}: {dt:.3f}s")
    return result, dt


def bench_graph(pbf_path: Path):
    """Run the full benchmark suite on a PBF."""
    print(f"\n=== {pbf_path.name} ===")
    t0 = time.perf_counter()
    g = gravel.load_osm_graph(str(pbf_path), gravel.SpeedProfile.car())
    t_load = time.perf_counter() - t0
    print(f"  Load:          {t_load:.2f}s  ({g.node_count:,} nodes, {g.edge_count:,} edges)")

    t0 = time.perf_counter()
    ch = gravel.build_ch(g)
    t_ch = time.perf_counter() - t0
    print(f"  Build CH:      {t_ch:.2f}s")

    q = gravel.CHQuery(ch)
    idx = gravel.ShortcutIndex(ch)

    # --- Single-pair routing benchmark ---
    random.seed(42)
    n = g.node_count
    pairs = [(random.randint(0, n - 1), random.randint(0, n - 1)) for _ in range(1000)]
    pairs = [p for p in pairs if p[0] != p[1]][:1000]

    # Warm up
    for s, t in pairs[:10]:
        q.distance(s, t)

    # Benchmark: distance only (no path unpacking)
    t0 = time.perf_counter()
    count = 0
    total = 0.0
    for s, t in pairs:
        d = q.distance(s, t)
        if d < 1e15:
            count += 1
            total += d
    t_dist = time.perf_counter() - t0
    us_per_query = (t_dist / len(pairs)) * 1e6
    print(f"  CH distance x1000: {t_dist*1000:.1f}ms  "
          f"({us_per_query:.1f} µs/query, {count} reachable)")

    # Benchmark: route (with path unpacking)
    t0 = time.perf_counter()
    path_len_sum = 0
    for s, t in pairs[:200]:  # 200 routes (path unpacking is slower)
        r = q.route(s, t)
        if r.distance < 1e15:
            path_len_sum += len(r.path)
    t_route = time.perf_counter() - t0
    us_per_route = (t_route / 200) * 1e6
    avg_path = path_len_sum / 200
    print(f"  CH route x200:     {t_route*1000:.1f}ms  "
          f"({us_per_route:.1f} µs/query, avg {avg_path:.0f} nodes)")

    # Benchmark: distance matrix (with OpenMP)
    sources = list({p[0] for p in pairs[:100]})
    targets = list({p[1] for p in pairs[:100]})
    t0 = time.perf_counter()
    matrix = q.distance_matrix(sources, targets)
    t_matrix = time.perf_counter() - t0
    n_mat = len(sources) * len(targets)
    us_per_mat = (t_matrix / n_mat) * 1e6
    print(f"  Distance matrix {len(sources)}x{len(targets)}: {t_matrix*1000:.1f}ms  "
          f"({us_per_mat:.1f} µs/cell)")

    # --- Route fragility benchmark ---
    # route_fragility is slow (per-edge blocked CH queries). Benchmark
    # with nearby pairs (short paths) for a reasonable per-edge timing.
    # Use 3 short-path OD pairs rather than random (which may hit 500+ edge paths).
    # Pick the 3 shortest-path pairs by path length (route fragility cost scales
    # linearly with path length — blocking each edge = one CH query).
    candidate_routes = []
    for s, t in pairs[:200]:
        r = q.route(s, t)
        if r.distance < 1e15:
            candidate_routes.append((s, t, len(r.path)))
    candidate_routes.sort(key=lambda x: x[2])
    short_pairs = [(s, t) for s, t, _ in candidate_routes[:3]]

    t_rf_samples = []
    edges_analyzed = []
    print(f"  Running route_fragility on {len(short_pairs)} short-path pairs...", flush=True)
    for s, t in short_pairs:
        t0 = time.perf_counter()
        fr = gravel.route_fragility(ch, idx, g, s, t)
        dt = time.perf_counter() - t0
        t_rf_samples.append(dt)
        edges_analyzed.append(len(fr.edge_fragilities))
        print(f"    pair ({s}→{t}): {dt:.1f}s, {len(fr.edge_fragilities)} edges", flush=True)
    if not t_rf_samples:
        t_rf_samples = [0.0]
        edges_analyzed = [0]

    avg_rf = statistics.mean(t_rf_samples)
    med_rf = statistics.median(t_rf_samples)
    avg_edges = statistics.mean(edges_analyzed) if edges_analyzed else 0
    print(f"  Route fragility x5: avg {avg_rf*1000:.1f}ms  median {med_rf*1000:.1f}ms  "
          f"(avg {avg_edges:.0f} edges/route)")

    # Cost per edge blocked
    per_edge_us = (avg_rf / max(1, avg_edges)) * 1e6
    print(f"    → per-edge blocked query: {per_edge_us:.1f} µs")

    # Return summary dict
    return {
        "pbf": pbf_path.name,
        "nodes": g.node_count,
        "edges": g.edge_count,
        "t_load": t_load,
        "t_ch": t_ch,
        "us_per_distance": us_per_query,
        "us_per_route": us_per_route,
        "us_per_matrix_cell": us_per_mat,
        "avg_route_fragility_ms": avg_rf * 1000,
        "avg_path_edges": avg_edges,
        "us_per_edge_blocked": per_edge_us,
    }


def write_markdown_report(results, out_path):
    lines = [
        "# Routing and Route Fragility Benchmarks",
        "",
        f"Generated: {time.strftime('%Y-%m-%d %H:%M:%S')}",
        "",
        "## Hardware",
        "",
        "Run on the Gravel development machine. Numbers will vary by CPU.",
        "",
        "## Summary",
        "",
        "| Graph | Nodes | Edges | Load | CH build | CH dist (µs) | CH route (µs) | Matrix cell (µs) | Route frag (ms) |",
        "|-------|------:|------:|-----:|---------:|-------------:|--------------:|-----------------:|----------------:|",
    ]
    for r in results:
        lines.append(
            f"| {r['pbf']} | {r['nodes']:,} | {r['edges']:,} | {r['t_load']:.2f}s | "
            f"{r['t_ch']:.2f}s | {r['us_per_distance']:.1f} | "
            f"{r['us_per_route']:.1f} | {r['us_per_matrix_cell']:.1f} | "
            f"{r['avg_route_fragility_ms']:.1f} |"
        )

    lines.extend([
        "",
        "## Detail",
        "",
        "**CH distance query**: single `q.distance(s, t)` call; no path unpacking.",
        "",
        "**CH route query**: `q.route(s, t)` with path unpacking. Cost scales with path length.",
        "",
        "**Distance matrix**: `q.distance_matrix(sources, targets)` — parallelized via OpenMP.",
        "",
        "**Route fragility**: `route_fragility(ch, idx, g, s, t)` — "
        "computes a replacement-path distance for every edge on the shortest path "
        "via `BlockedCHQuery`. Cost ≈ (path length) × (per-edge blocked query).",
        "",
        "Per-edge blocked query cost:",
        "",
    ])
    for r in results:
        lines.append(
            f"- **{r['pbf']}**: {r['us_per_edge_blocked']:.1f} µs/edge "
            f"(path avg {r['avg_path_edges']:.0f} edges → {r['avg_route_fragility_ms']:.1f} ms/route)"
        )

    out_path.write_text("\n".join(lines) + "\n")
    print(f"\nWrote {out_path}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pbf", nargs="*",
                        default=["tests/data/swain_county.osm.pbf",
                                 "tests/data/buncombe_county.osm.pbf"],
                        help="PBF files to benchmark")
    parser.add_argument("--output", default="bench/baselines/routing_performance.md")
    args = parser.parse_args()

    results = []
    for pbf in args.pbf:
        pbf_path = Path(pbf)
        if not pbf_path.exists():
            print(f"WARNING: {pbf} not found, skipping")
            continue
        results.append(bench_graph(pbf_path))

    if results:
        write_markdown_report(results, Path(args.output))


if __name__ == "__main__":
    main()
