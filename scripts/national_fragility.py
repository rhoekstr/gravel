#!/usr/bin/env python3
"""National county-level isolation fragility pipeline.

Downloads state OSM PBFs from Geofabrik, extracts per-county road networks
with 10km buffer, runs location_fragility on each, and outputs CSV by FIPS.

Usage:
    python scripts/national_fragility.py [--output-dir DIR] [--states FIPS,FIPS,...] [--skip-download]

Requires: gravel (built with GRAVEL_USE_OSMIUM=ON), osmium CLI, shapely, geopandas
"""

import argparse
import csv
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

import geopandas as gpd
import pyproj
from shapely.geometry import mapping
from shapely.ops import transform

# Add gravel to path
SCRIPT_DIR = Path(__file__).parent
PROJECT_DIR = SCRIPT_DIR.parent
sys.path.insert(0, str(PROJECT_DIR / "build" / "python"))
sys.path.insert(0, str(PROJECT_DIR / "python"))

import gravel
from geofabrik_urls import STATE_FIPS_TO_SLUG, TERRITORY_URLS, get_pbf_url
from state_names import STATE_FIPS_TO_NAME

# Adaptive radius by state FIPS
RADIUS_OVERRIDE = {
    "02": 80000,  # Alaska boroughs — enormous, sparse
    "72": 20000,  # Puerto Rico — small, dense
}
DEFAULT_RADIUS = 30000  # 30km for contiguous US + Hawaii


def buffer_polygon_meters(geom, meters):
    """Buffer a WGS84 polygon by meters using local azimuthal projection."""
    centroid = geom.centroid
    local_crs = pyproj.CRS(f"+proj=aeqd +lat_0={centroid.y} +lon_0={centroid.x}")
    to_local = pyproj.Transformer.from_crs("EPSG:4326", local_crs, always_xy=True).transform
    to_wgs = pyproj.Transformer.from_crs(local_crs, "EPSG:4326", always_xy=True).transform
    return transform(to_wgs, transform(to_local, geom).buffer(meters))


def download_pbf(url, dest, retries=3):
    """Download a PBF file with retry."""
    for attempt in range(retries):
        try:
            print(f"    Downloading {url}...")
            urllib.request.urlretrieve(url, dest)
            size_mb = os.path.getsize(dest) / 1e6
            print(f"    Downloaded {size_mb:.1f} MB")
            return True
        except Exception as e:
            print(f"    Attempt {attempt+1} failed: {e}")
            if attempt < retries - 1:
                time.sleep(5 * (attempt + 1))
    return False


def extract_county_pbf(state_pbf, county_geojson_path, output_pbf):
    """Extract county region from state PBF using osmium."""
    cmd = [
        "osmium", "extract",
        "--polygon", county_geojson_path,
        "--strategy", "complete_ways",
        "--overwrite",
        "-o", output_pbf,
        state_pbf,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"osmium extract failed: {result.stderr}")


def analyze_county(county_pbf, centroid_lat, centroid_lon, radius_meters, mc_runs=20):
    """Run location_fragility on a county PBF extract. Returns result dict or None."""
    try:
        g = gravel.load_osm_graph(county_pbf, gravel.SpeedProfile.car())
    except Exception as e:
        return {"notes": f"load_error: {e}"}

    graph_nodes = g.node_count
    graph_edges = g.edge_count

    if graph_nodes < 10:
        return {
            "graph_nodes": graph_nodes, "graph_edges": graph_edges,
            "notes": "insufficient_road_data",
        }

    try:
        ch = gravel.build_ch(g)
    except Exception as e:
        return {
            "graph_nodes": graph_nodes, "graph_edges": graph_edges,
            "notes": f"ch_build_error: {e}",
        }

    cfg = gravel.LocationFragilityConfig()
    cfg.center = gravel.Coord(centroid_lat, centroid_lon)
    cfg.radius_meters = radius_meters
    cfg.monte_carlo_runs = mc_runs
    cfg.removal_fraction = 0.10
    cfg.sample_count = 200
    cfg.seed = 42

    try:
        result = gravel.location_fragility(g, ch, cfg)
    except Exception as e:
        return {
            "graph_nodes": graph_nodes, "graph_edges": graph_edges,
            "notes": f"fragility_error: {e}",
        }

    return {
        "isolation_risk": result.isolation_risk,
        "auc_normalized": result.auc_normalized,
        "baseline_isolation_risk": result.baseline_isolation_risk,
        "reachable_nodes": result.reachable_nodes,
        "sp_edges_total": result.sp_edges_total,
        "sp_edges_removed": result.sp_edges_removed,
        "subgraph_nodes": result.subgraph_nodes,
        "subgraph_edges": result.subgraph_edges,
        "directional_coverage": result.directional_coverage,
        "directional_asymmetry": result.directional_asymmetry,
        "graph_nodes": graph_nodes,
        "graph_edges": graph_edges,
        "radius_meters": radius_meters,
        "mc_runs": mc_runs,
        "notes": "",
    }


def load_county_boundaries(geojson_path):
    """Load county boundaries, return GeoDataFrame with FIPS, name, geometry."""
    gdf = gpd.read_file(geojson_path)

    # Handle the plotly-format GeoJSON (id field = FIPS, properties have NAME, LSAD, STATE)
    if "id" in gdf.columns and "GEOID" not in gdf.columns:
        gdf["fips"] = gdf["id"].astype(str).str.zfill(5)
    elif "GEOID" in gdf.columns:
        gdf["fips"] = gdf["GEOID"].astype(str).str.zfill(5)
    else:
        raise ValueError("Cannot find FIPS code column in GeoJSON")

    gdf["state_fips"] = gdf["fips"].str[:2]

    if "NAME" in gdf.columns and "LSAD" in gdf.columns:
        gdf["county_name"] = gdf["NAME"] + " " + gdf["LSAD"]
    elif "NAMELSAD" in gdf.columns:
        gdf["county_name"] = gdf["NAMELSAD"]
    else:
        gdf["county_name"] = gdf["fips"]

    return gdf[["fips", "state_fips", "county_name", "geometry"]]


def main():
    parser = argparse.ArgumentParser(description="National county fragility pipeline")
    parser.add_argument("--output-dir", default="output", help="Output directory for CSVs")
    parser.add_argument("--states", default=None, help="Comma-separated state FIPS codes to process (default: all)")
    parser.add_argument("--counties-geojson", default="/tmp/us_counties.geojson",
                        help="Path to US counties GeoJSON")
    parser.add_argument("--mc-runs", type=int, default=20, help="Monte Carlo runs per county")
    parser.add_argument("--skip-download", action="store_true", help="Skip PBF downloads (use cached)")
    parser.add_argument("--pbf-dir", default="/tmp/gravel_pbf", help="Directory for PBF downloads")
    parser.add_argument("--keep-pbf", action="store_true", help="Keep state PBFs after processing")
    args = parser.parse_args()

    os.makedirs(args.output_dir, exist_ok=True)
    os.makedirs(args.pbf_dir, exist_ok=True)

    # Load county boundaries
    print("Loading county boundaries...")
    counties = load_county_boundaries(args.counties_geojson)
    print(f"  {len(counties)} counties loaded")

    # Determine which states to process
    all_state_fips = sorted(counties["state_fips"].unique())
    if args.states:
        target_states = [s.strip().zfill(2) for s in args.states.split(",")]
    else:
        target_states = [s for s in all_state_fips
                         if s in STATE_FIPS_TO_SLUG or s in TERRITORY_URLS]
    print(f"  Processing {len(target_states)} states: {', '.join(target_states)}")

    # Load checkpoint
    checkpoint_path = os.path.join(args.output_dir, "checkpoint.json")
    completed = set()
    if os.path.exists(checkpoint_path):
        with open(checkpoint_path) as f:
            completed = set(json.load(f))
        print(f"  Resuming: {len(completed)} counties already completed")

    # Open CSV
    csv_path = os.path.join(args.output_dir, "county_isolation_fragility.csv")
    csv_exists = os.path.exists(csv_path) and len(completed) > 0
    csv_fields = [
        "fips", "county_name", "state_fips", "state_name",
        "isolation_risk", "auc_normalized", "baseline_isolation_risk",
        "reachable_nodes", "sp_edges_total", "sp_edges_removed",
        "subgraph_nodes", "subgraph_edges",
        "directional_coverage", "directional_asymmetry",
        "graph_nodes", "graph_edges", "radius_meters", "mc_runs", "notes",
    ]
    csv_file = open(csv_path, "a" if csv_exists else "w", newline="")
    writer = csv.DictWriter(csv_file, fieldnames=csv_fields)
    if not csv_exists:
        writer.writeheader()

    total_counties = 0
    total_time = 0

    for state_fips in target_states:
        state_name = STATE_FIPS_TO_NAME.get(state_fips, f"State {state_fips}")
        state_counties = counties[counties["state_fips"] == state_fips]
        pending = [r for _, r in state_counties.iterrows() if r["fips"] not in completed]

        if not pending:
            print(f"\n[{state_fips}] {state_name}: all {len(state_counties)} counties done, skipping")
            continue

        print(f"\n{'='*60}")
        print(f"[{state_fips}] {state_name}: {len(pending)}/{len(state_counties)} counties to process")
        print(f"{'='*60}")

        # Download state PBF
        state_pbf = os.path.join(args.pbf_dir, f"{state_fips}.osm.pbf")
        if not os.path.exists(state_pbf) and not args.skip_download:
            url = get_pbf_url(state_fips)
            if not download_pbf(url, state_pbf):
                print(f"  FAILED to download {state_name}, skipping")
                continue
        elif not os.path.exists(state_pbf):
            print("  PBF not found and --skip-download set, skipping")
            continue

        radius = RADIUS_OVERRIDE.get(state_fips, DEFAULT_RADIUS)

        for county in pending:
            fips = county["fips"]
            name = county["county_name"]
            geom = county["geometry"]
            centroid = geom.centroid

            print(f"  {fips} {name}...", end=" ", flush=True)
            t0 = time.time()

            try:
                # Buffer polygon
                buffered = buffer_polygon_meters(geom, 10000)

                # Write buffered polygon as GeoJSON for osmium
                with tempfile.NamedTemporaryFile(mode="w", suffix=".geojson", delete=False) as tmp:
                    tmp_geojson = tmp.name
                    feature = {
                        "type": "Feature",
                        "geometry": mapping(buffered),
                        "properties": {},
                    }
                    json.dump({"type": "FeatureCollection", "features": [feature]}, tmp)

                # Extract from state PBF
                county_pbf = os.path.join(args.pbf_dir, f"{fips}.osm.pbf")
                extract_county_pbf(state_pbf, tmp_geojson, county_pbf)
                os.unlink(tmp_geojson)

                # Analyze
                result = analyze_county(
                    county_pbf, centroid.y, centroid.x, radius, args.mc_runs)

                # Clean up county PBF
                if os.path.exists(county_pbf):
                    os.unlink(county_pbf)

            except Exception as e:
                result = {"notes": f"error: {e}"}

            dt = time.time() - t0
            total_time += dt
            total_counties += 1

            # Build CSV row
            row = {
                "fips": fips,
                "county_name": name,
                "state_fips": state_fips,
                "state_name": state_name,
            }
            for field in csv_fields[4:]:
                row[field] = result.get(field, "")

            writer.writerow(row)
            csv_file.flush()

            # Update checkpoint
            completed.add(fips)
            with open(checkpoint_path, "w") as f:
                json.dump(sorted(completed), f)

            risk = result.get("isolation_risk", "N/A")
            notes = result.get("notes", "")
            if notes:
                print(f"{dt:.1f}s — {notes}")
            else:
                print(f"{dt:.1f}s — risk={risk:.3f}")

        # Clean up state PBF
        if not args.keep_pbf and os.path.exists(state_pbf):
            os.unlink(state_pbf)

    csv_file.close()

    print(f"\n{'='*60}")
    print(f"COMPLETE: {total_counties} counties in {total_time:.0f}s ({total_time/3600:.1f} hrs)")
    print(f"Output: {csv_path}")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
