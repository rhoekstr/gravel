#!/usr/bin/env python3
"""National inter-county fragility pipeline (adjacency-driven, cross-state aware).

Approach:
  1. Compute county adjacency upfront via geometric polygon touching
     (this catches cross-state pairs that the per-state approach misses).
  2. Group adjacent pairs by required state set:
       - Single-state groups: pairs where both counties are in the same state
       - Two-state groups: cross-state border pairs
       - Rare three-state groups (corner counties): handled with osmium merge
  3. For each group:
       - Download (and cache) the required state PBFs
       - Merge with `osmium merge` if multiple states needed
       - Extract just the involved counties + 5km buffer
       - Build skeleton, run inter-geography fragility on those pairs
       - Append rows to the CSV
  4. State PBFs are kept on disk during the run and cleaned up at the end.

Output: output/inter_county_fragility.csv with one row per adjacent pair
(including cross-state pairs).

Usage:
    python scripts/national_inter_county.py [--mc-runs N] [--k-max N]
"""

import argparse
import csv
import json
import subprocess
import sys
import time
import urllib.request
from collections import defaultdict
from pathlib import Path

import geopandas as gpd
import numpy as np
import pyproj
from shapely.geometry import mapping
from shapely.ops import transform

SCRIPT_DIR = Path(__file__).parent
PROJECT_DIR = SCRIPT_DIR.parent
sys.path.insert(0, str(PROJECT_DIR / "build" / "python"))
sys.path.insert(0, str(PROJECT_DIR / "python"))

import gravel
from geofabrik_urls import STATE_FIPS_TO_SLUG, TERRITORY_URLS, get_pbf_url

# ============================================================
# I/O utilities
# ============================================================

def download_pbf(url, dest, retries=3):
    for attempt in range(retries):
        try:
            print(f"    Downloading {url}...", flush=True)
            urllib.request.urlretrieve(url, dest)
            return True
        except Exception as e:
            print(f"    Attempt {attempt+1} failed: {e}", flush=True)
            if attempt < retries - 1:
                time.sleep(5 * (attempt + 1))
    return False


def ensure_state_pbf(state_fips, pbf_dir):
    """Download state PBF if not already cached. Returns the path or None on failure."""
    pbf_path = pbf_dir / f"{state_fips}.osm.pbf"
    if pbf_path.exists():
        return pbf_path
    url = get_pbf_url(state_fips)
    if not download_pbf(url, str(pbf_path)):
        return None
    return pbf_path


def buffer_polygon_meters(geom, meters):
    centroid = geom.centroid
    local_crs = pyproj.CRS(f"+proj=aeqd +lat_0={centroid.y} +lon_0={centroid.x}")
    to_local = pyproj.Transformer.from_crs("EPSG:4326", local_crs, always_xy=True).transform
    to_wgs = pyproj.Transformer.from_crs(local_crs, "EPSG:4326", always_xy=True).transform
    return transform(to_wgs, transform(to_local, geom).buffer(meters))


def osmium_merge(input_pbfs, output_pbf):
    """Merge multiple PBFs into one."""
    cmd = ["osmium", "merge", "--overwrite", "-o", str(output_pbf)] + [str(p) for p in input_pbfs]
    subprocess.run(cmd, capture_output=True, text=True, check=True)


def osmium_extract(input_pbf, polygon_geojson_path, output_pbf):
    """Extract a polygon-bounded region from a PBF."""
    cmd = [
        "osmium", "extract",
        "--polygon", str(polygon_geojson_path),
        "--strategy", "complete_ways",
        "--overwrite",
        "-o", str(output_pbf),
        str(input_pbf),
    ]
    subprocess.run(cmd, capture_output=True, text=True, check=True)


# ============================================================
# Adjacency computation
# ============================================================

def compute_adjacency(counties_gdf):
    """Compute county-pair adjacency from polygon geometry.

    Returns a dict: frozenset({fips_a, fips_b}) -> set of state_fips involved.
    Uses spatial index for efficiency.
    """
    print(f"Computing county adjacency for {len(counties_gdf)} counties...", flush=True)
    t0 = time.time()

    # Build spatial index
    sindex = counties_gdf.sindex

    adjacency = {}
    for idx, row in counties_gdf.iterrows():
        # Get candidates whose bounding box intersects ours
        candidates = list(sindex.intersection(row["geometry"].bounds))
        for cidx in candidates:
            if cidx <= idx:
                continue
            other = counties_gdf.iloc[cidx]
            if row["geometry"].touches(other["geometry"]) or \
               row["geometry"].intersects(other["geometry"]):
                # Both touches and shared-boundary intersects count
                key = frozenset([row["fips"], other["fips"]])
                states = frozenset([row["state_fips"], other["state_fips"]])
                adjacency[key] = states

    print(f"  Found {len(adjacency)} adjacent pairs in {time.time()-t0:.1f}s", flush=True)

    # Stats
    intra = sum(1 for s in adjacency.values() if len(s) == 1)
    cross = sum(1 for s in adjacency.values() if len(s) == 2)
    triple = sum(1 for s in adjacency.values() if len(s) >= 3)
    print(f"  Intra-state: {intra}, Cross-state: {cross}, 3+ states: {triple}", flush=True)

    return adjacency


def group_pairs_by_state_set(adjacency):
    """Group pairs by the set of states required to process them."""
    groups = defaultdict(list)
    for pair, state_set in adjacency.items():
        groups[state_set].append(pair)
    return groups


# ============================================================
# Per-group processing
# ============================================================

def counties_to_regions_geojson(counties_gdf, output_path):
    features = []
    for _, row in counties_gdf.iterrows():
        geom = row["geometry"]
        if geom.geom_type == "MultiPolygon":
            geom = max(geom.geoms, key=lambda g: g.area)
        features.append({
            "type": "Feature",
            "properties": {"GEOID": row["fips"], "NAMELSAD": row["county_name"]},
            "geometry": mapping(geom),
        })
    with open(output_path, "w") as f:
        json.dump({"type": "FeatureCollection", "features": features}, f)


def select_betweenness_centrals(graph, assignment, betweenness_samples=500, seed=42):
    """Pick the highest-betweenness node within each region."""
    bc = gravel.BetweennessConfig()
    bc.sample_sources = betweenness_samples
    bc.seed = seed
    bet = gravel.edge_betweenness(graph, bc)

    region_index = np.array(assignment.region_index, dtype=np.int32)
    node_scores = np.array(bet.node_scores, dtype=np.float64)

    INVALID = 0xFFFFFFFE
    centrals = []
    for ridx in range(len(assignment.regions)):
        mask = region_index == ridx
        candidate_nodes = np.where(mask)[0]
        if len(candidate_nodes) == 0:
            centrals.append(INVALID)
            continue
        scores = node_scores[candidate_nodes]
        centrals.append(int(candidate_nodes[np.argmax(scores)]))
    return centrals


def process_group(state_set, pairs, counties_gdf, pbf_dir, work_dir, writer,
                  mc_runs, k_max, betweenness_samples):
    """Process one group of pairs that share the same required state set.

    state_set: frozenset of state FIPS codes (1, 2, or rarely 3 states)
    pairs: list of frozenset({fips_a, fips_b}) — adjacent county pairs in this group
    """
    state_list = sorted(state_set)
    state_label = "+".join(state_list)
    print(f"\n{'='*60}", flush=True)
    print(f"GROUP [{state_label}] — {len(pairs)} adjacent pairs", flush=True)
    print(f"{'='*60}", flush=True)

    # Get all FIPS codes involved in this group
    involved_fips = set()
    for pair in pairs:
        involved_fips.update(pair)
    involved_counties = counties_gdf[counties_gdf["fips"].isin(involved_fips)]
    print(f"  Counties to process: {len(involved_counties)}", flush=True)

    # Ensure all needed state PBFs are available
    state_pbfs = []
    for sfips in state_list:
        p = ensure_state_pbf(sfips, pbf_dir)
        if p is None:
            print(f"  Failed to obtain PBF for state {sfips}, skipping group", flush=True)
            return 0
        state_pbfs.append(p)

    # Source PBF: merge if multi-state, otherwise use directly
    if len(state_pbfs) == 1:
        source_pbf = state_pbfs[0]
    else:
        source_pbf = work_dir / f"merged_{state_label}.osm.pbf"
        t0 = time.time()
        try:
            osmium_merge(state_pbfs, source_pbf)
            print(f"  Merged {len(state_pbfs)} state PBFs in {time.time()-t0:.1f}s", flush=True)
        except Exception as e:
            print(f"  osmium merge failed: {e}", flush=True)
            return 0

    # Extract just the involved counties + 5km buffer
    extract_pbf = work_dir / f"extract_{state_label}.osm.pbf"
    union = involved_counties.geometry.union_all()
    buffered = buffer_polygon_meters(union, 5000)
    poly_geojson = work_dir / f"poly_{state_label}.geojson"
    with open(poly_geojson, "w") as f:
        json.dump({"type": "FeatureCollection",
                   "features": [{"type": "Feature", "geometry": mapping(buffered), "properties": {}}]}, f)
    try:
        t0 = time.time()
        osmium_extract(source_pbf, poly_geojson, extract_pbf)
        print(f"  Extracted region in {time.time()-t0:.1f}s", flush=True)
    except Exception as e:
        print(f"  osmium extract failed: {e}", flush=True)
        return 0

    # Load + CH
    try:
        t0 = time.time()
        g = gravel.load_osm_graph(str(extract_pbf), gravel.SpeedProfile.car())
        print(f"  Loaded {g.node_count:,} nodes, {g.edge_count:,} edges in {time.time()-t0:.1f}s", flush=True)

        t0 = time.time()
        ch = gravel.build_ch(g)
        print(f"  CH built in {time.time()-t0:.1f}s", flush=True)
    except Exception as e:
        print(f"  Load/CH failed: {e}", flush=True)
        return 0

    # Build regions for these counties
    regions_path = work_dir / f"regions_{state_label}.geojson"
    counties_to_regions_geojson(involved_counties, regions_path)
    regions = gravel.load_regions_geojson(str(regions_path))

    t0 = time.time()
    assignment = gravel.assign_nodes_to_regions(g, regions)
    print(f"  Assigned in {time.time()-t0:.1f}s ({assignment.unassigned_count:,} unassigned)", flush=True)

    t0 = time.time()
    border = gravel.summarize_border_edges(g, assignment)
    print(f"  Border analysis: {border.total_border_edges} edges, {border.connected_pairs} pairs in {time.time()-t0:.1f}s", flush=True)

    if border.connected_pairs == 0:
        print("  No adjacent pairs found in graph, skipping", flush=True)
        return 0

    t0 = time.time()
    centrals = select_betweenness_centrals(g, assignment, betweenness_samples)
    print(f"  Betweenness + central selection in {time.time()-t0:.1f}s", flush=True)

    cfg = gravel.ReducedGraphConfig()
    cfg.method = gravel.ReducedGraphConfig.Centrality.PROVIDED
    cfg.precomputed_centrals = centrals

    t0 = time.time()
    reduced = gravel.build_reduced_geography_graph(g, ch, assignment, border, cfg)
    print(f"  Reduced graph: {reduced.graph.node_count} nodes, {reduced.graph.edge_count} edges in {time.time()-t0:.1f}s", flush=True)

    inter_cfg = gravel.InterRegionFragilityConfig()
    inter_cfg.k_max = k_max
    inter_cfg.monte_carlo_runs = mc_runs
    inter_cfg.seed = 42

    t0 = time.time()
    result = gravel.inter_region_fragility(reduced, inter_cfg)
    print(f"  Fragility analysis: {len(result.pairs)} pair results in {time.time()-t0:.1f}s", flush=True)

    # Filter to ONLY the pairs we wanted from this group
    # (the result includes all pairs found in the graph, which may include extra
    # adjacent pairs for counties that happen to be in this region)
    target_pairs = {frozenset(p) for p in pairs}
    written = 0
    for p in result.pairs:
        pair_key = frozenset([p.source_region, p.target_region])
        if pair_key not in target_pairs:
            continue
        writer.writerow({
            "source_fips": p.source_region,
            "target_fips": p.target_region,
            "baseline_minutes": f"{p.baseline_seconds / 60.0:.3f}",
            "shared_border_edges": p.shared_border_edges,
            "k_used": p.k_used,
            "auc_inflation": f"{p.auc_inflation:.4f}",
            "auc_disconnection": f"{p.auc_disconnection:.4f}",
            "worst_case_minutes": (f"{p.curve[0].mean_seconds / 60.0:.3f}"
                                    if p.curve and p.curve[0].mean_seconds > 0 else ""),
            "worst_case_disconnected_frac": (f"{p.curve[0].disconnected_frac:.4f}"
                                              if p.curve else ""),
            "restored_minutes": (f"{p.curve[-1].mean_seconds / 60.0:.3f}"
                                  if p.curve and p.curve[-1].mean_seconds > 0 else ""),
            "states": "+".join(state_list),
            "is_cross_state": len(state_list) > 1,
        })
        written += 1

    print(f"  Wrote {written}/{len(pairs)} target pairs to CSV", flush=True)

    # Cleanup work files (keep state PBFs cached)
    for f in [extract_pbf, poly_geojson, regions_path]:
        if f.exists():
            f.unlink()
    if len(state_pbfs) > 1 and source_pbf.exists():
        source_pbf.unlink()  # merged file is per-group

    return written


# ============================================================
# Main
# ============================================================

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", default="output")
    parser.add_argument("--counties-geojson", default="/tmp/us_counties.geojson")
    parser.add_argument("--pbf-dir", default="/tmp/gravel_pbf")
    parser.add_argument("--mc-runs", type=int, default=10)
    parser.add_argument("--k-max", type=int, default=20)
    parser.add_argument("--betweenness-samples", type=int, default=500)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--cleanup-pbfs", action="store_true",
                        help="Delete cached state PBFs at end of run")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    pbf_dir = Path(args.pbf_dir)
    work_dir = Path("/tmp/gravel_work")
    output_dir.mkdir(parents=True, exist_ok=True)
    pbf_dir.mkdir(parents=True, exist_ok=True)
    work_dir.mkdir(parents=True, exist_ok=True)

    print("Loading county boundaries...", flush=True)
    counties = gpd.read_file(args.counties_geojson)
    if "id" in counties.columns:
        counties["fips"] = counties["id"].astype(str).str.zfill(5)
    counties["state_fips"] = counties["fips"].str[:2]
    if "NAME" in counties.columns and "LSAD" in counties.columns:
        counties["county_name"] = counties["NAME"] + " " + counties["LSAD"]
    print(f"  {len(counties)} counties loaded", flush=True)

    # Filter to states we have download URLs for
    valid_states = set(STATE_FIPS_TO_SLUG.keys()) | set(TERRITORY_URLS.keys())
    counties = counties[counties["state_fips"].isin(valid_states)].reset_index(drop=True)
    print(f"  {len(counties)} counties in supported states/territories", flush=True)

    # Compute adjacency upfront — this gives us cross-state pairs
    adjacency = compute_adjacency(counties)
    groups = group_pairs_by_state_set(adjacency)
    print(f"\nGrouped into {len(groups)} state-set groups", flush=True)

    # Sort groups: process single-state groups first (cheaper), then multi-state
    sorted_groups = sorted(groups.items(), key=lambda kv: (len(kv[0]), sorted(kv[0])))

    # Setup output CSV with checkpointing
    csv_path = output_dir / "inter_county_fragility.csv"
    done_pairs = set()
    mode = "a" if args.resume and csv_path.exists() else "w"
    if args.resume and csv_path.exists():
        with open(csv_path) as f:
            for row in csv.DictReader(f):
                done_pairs.add(frozenset([row["source_fips"], row["target_fips"]]))
        print(f"  Resuming: {len(done_pairs)} pairs already done", flush=True)

    fieldnames = [
        "source_fips", "target_fips",
        "baseline_minutes",
        "shared_border_edges",
        "k_used",
        "auc_inflation", "auc_disconnection",
        "worst_case_minutes", "worst_case_disconnected_frac",
        "restored_minutes",
        "states", "is_cross_state",
    ]

    total_pairs_written = 0
    with open(csv_path, mode, newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        if mode == "w":
            writer.writeheader()

        for state_set, pairs in sorted_groups:
            # Skip pairs already done (resume)
            pending = [p for p in pairs if p not in done_pairs]
            if not pending:
                continue

            try:
                n = process_group(state_set, pending, counties, pbf_dir, work_dir,
                                  writer, args.mc_runs, args.k_max, args.betweenness_samples)
                total_pairs_written += n
                f.flush()
            except Exception as e:
                print(f"  ERROR processing group {state_set}: {e}", flush=True)
                import traceback
                traceback.print_exc()

    if args.cleanup_pbfs:
        print("\nCleaning up cached PBFs...", flush=True)
        for p in pbf_dir.glob("*.osm.pbf"):
            p.unlink()

    print(f"\n{'='*60}", flush=True)
    print(f"COMPLETE: {total_pairs_written} pair rows written to {csv_path}", flush=True)
    print(f"{'='*60}", flush=True)


if __name__ == "__main__":
    main()
