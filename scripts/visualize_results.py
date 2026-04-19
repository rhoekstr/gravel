#!/usr/bin/env python3
"""Generate visualizations of the national isolation fragility results.

Usage:
    python scripts/visualize_results.py [--input CSV] [--geojson PATH] [--output-dir DIR]

Defaults to the sample data in data/sample-results/ if no --input is given.
"""

import argparse
import csv
import json
from pathlib import Path

import plotly.express as px
import plotly.graph_objects as go
from plotly.subplots import make_subplots

SCRIPT_DIR = Path(__file__).parent
PROJECT_DIR = SCRIPT_DIR.parent

parser = argparse.ArgumentParser()
parser.add_argument("--input", default=None,
                    help="Fragility CSV (defaults to sample results, then output/)")
parser.add_argument("--geojson", default="/tmp/us_counties.geojson",
                    help="Path to US counties GeoJSON (FIPS-keyed)")
parser.add_argument("--output-dir", default=None,
                    help="Where to write HTML files (defaults to same dir as --input)")
args = parser.parse_args()

# Resolve input CSV
if args.input:
    csv_path = Path(args.input)
else:
    # Prefer user's own run, fall back to packaged sample data
    candidates = [
        PROJECT_DIR / "output" / "county_isolation_fragility.csv",
        PROJECT_DIR / "data" / "sample-results" / "county_isolation_fragility.csv",
    ]
    csv_path = next((c for c in candidates if c.exists()), None)
    if csv_path is None:
        raise SystemExit("No fragility CSV found. Run scripts/national_fragility.py first, "
                         "or pass --input path/to/results.csv")

out_dir = Path(args.output_dir) if args.output_dir else csv_path.parent
out_dir.mkdir(parents=True, exist_ok=True)
print(f"Input:      {csv_path}")
print(f"Output dir: {out_dir}")

# Load the national county GeoJSON (FIPS-keyed)
with open(args.geojson) as f:
    counties_geojson = json.load(f)

# Load our results CSV
results = {}
with open(csv_path) as f:
    for row in csv.DictReader(f):
        if row["isolation_risk"]:
            results[row["fips"]] = {
                "fips": row["fips"],
                "county_name": row["county_name"].strip(),
                "state_name": row["state_name"],
                "state_fips": row["state_fips"],
                "isolation_risk": float(row["isolation_risk"]),
                "auc_normalized": float(row["auc_normalized"]) if row["auc_normalized"] else 0,
                "reachable_nodes": int(row["reachable_nodes"]) if row["reachable_nodes"] else 0,
                "sp_edges_total": int(row["sp_edges_total"]) if row["sp_edges_total"] else 0,
                "directional_coverage": float(row["directional_coverage"]) if row["directional_coverage"] else 0,
                "directional_asymmetry": float(row["directional_asymmetry"]) if row["directional_asymmetry"] else 0,
                "graph_nodes": int(row["graph_nodes"]) if row["graph_nodes"] else 0,
            }

print(f"Loaded {len(results)} county results")

# Build a list of rows matching the GeoJSON order for choropleth
fips_list = []
risk_list = []
hover_text = []

for feat in counties_geojson["features"]:
    fips = str(feat.get("id", "")).zfill(5)
    if fips in results:
        r = results[fips]
        fips_list.append(fips)
        risk_list.append(r["isolation_risk"])
        hover_text.append(
            f"<b>{r['county_name']}</b><br>"
            f"{r['state_name']}<br>"
            f"FIPS: {fips}<br>"
            f"Isolation risk: {r['isolation_risk']:.3f}<br>"
            f"AUC: {r['auc_normalized']:.3f}<br>"
            f"Reachable nodes: {r['reachable_nodes']:,}<br>"
            f"Graph nodes: {r['graph_nodes']:,}<br>"
            f"Directional coverage: {r['directional_coverage']:.2f}"
        )

# ============================================================
# VIZ 1: National choropleth map
# ============================================================
print("Building national choropleth...")

fig = go.Figure(go.Choropleth(
    geojson=counties_geojson,
    locations=fips_list,
    z=risk_list,
    featureidkey="id",
    colorscale="RdYlGn_r",  # reversed: green=low risk, red=high
    zmin=0,
    zmax=0.8,
    marker_line_width=0.2,
    marker_line_color="white",
    colorbar=dict(
        title="Isolation<br>Risk",
        thickness=15,
        len=0.7,
        x=1.02,
    ),
    text=hover_text,
    hoverinfo="text",
))

fig.update_geos(
    scope="usa",
    showcoastlines=True,
    coastlinecolor="gray",
    showland=True,
    landcolor="rgb(240,240,240)",
    showlakes=True,
    lakecolor="rgb(220,230,250)",
)

fig.update_layout(
    title=dict(
        text="<b>US County Road Network Isolation Risk</b><br>"
             "<sub>Gravel v2.1 — Dijkstra + IncrementalSSSP fragility analysis</sub>",
        x=0.5,
        xanchor="center",
    ),
    width=1400,
    height=800,
    margin=dict(l=0, r=0, t=80, b=0),
)

output_html = out_dir / "national_fragility_map.html"
fig.write_html(output_html)
print(f"  Saved {output_html}")

# ============================================================
# VIZ 2: Distribution histogram + state rankings
# ============================================================
print("Building distribution + rankings figure...")

# State aggregations
from collections import defaultdict
import statistics

by_state = defaultdict(list)
for r in results.values():
    by_state[r["state_name"]].append(r["isolation_risk"])

state_stats = [
    (name, statistics.mean(v), statistics.median(v), len(v),
     statistics.quantiles(v, n=4)[2] if len(v) > 3 else max(v))
    for name, v in by_state.items()
]
state_stats.sort(key=lambda x: x[1], reverse=True)

fig2 = make_subplots(
    rows=2, cols=2,
    subplot_titles=(
        "Distribution of Isolation Risk (N=3,221 counties)",
        "State-Level Mean Risk (ranked)",
        "Most Vulnerable Counties",
        "Most Resilient Counties (excluding zero)",
    ),
    specs=[[{"type": "histogram"}, {"type": "bar"}],
           [{"type": "bar"}, {"type": "bar"}]],
    vertical_spacing=0.15,
    horizontal_spacing=0.12,
)

# Histogram
fig2.add_trace(
    go.Histogram(
        x=[r["isolation_risk"] for r in results.values()],
        nbinsx=40,
        marker_color="steelblue",
        showlegend=False,
    ),
    row=1, col=1,
)

# State means
fig2.add_trace(
    go.Bar(
        x=[s[0] for s in state_stats],
        y=[s[1] for s in state_stats],
        marker_color=[s[1] for s in state_stats],
        marker_colorscale="RdYlGn_r",
        marker_cmin=0, marker_cmax=0.7,
        showlegend=False,
        text=[f"{s[1]:.2f}" for s in state_stats],
        textposition="outside",
    ),
    row=1, col=2,
)

# Top 20 most vulnerable
top = sorted(results.values(), key=lambda r: r["isolation_risk"], reverse=True)[:20]
fig2.add_trace(
    go.Bar(
        y=[f"{r['county_name']} ({r['state_name'][:2]})" for r in reversed(top)],
        x=[r["isolation_risk"] for r in reversed(top)],
        orientation="h",
        marker_color="crimson",
        showlegend=False,
        text=[f"{r['isolation_risk']:.3f}" for r in reversed(top)],
        textposition="outside",
    ),
    row=2, col=1,
)

# Top 20 most resilient (excluding zero)
nonzero = [r for r in results.values() if r["isolation_risk"] > 0.01]
bot = sorted(nonzero, key=lambda r: r["isolation_risk"])[:20]
fig2.add_trace(
    go.Bar(
        y=[f"{r['county_name']} ({r['state_name'][:2]})" for r in reversed(bot)],
        x=[r["isolation_risk"] for r in reversed(bot)],
        orientation="h",
        marker_color="forestgreen",
        showlegend=False,
        text=[f"{r['isolation_risk']:.3f}" for r in reversed(bot)],
        textposition="outside",
    ),
    row=2, col=2,
)

fig2.update_xaxes(title_text="Isolation Risk", row=1, col=1)
fig2.update_yaxes(title_text="Count", row=1, col=1)
fig2.update_xaxes(title_text="", row=1, col=2, tickangle=-60)
fig2.update_yaxes(title_text="Mean Isolation Risk", row=1, col=2)
fig2.update_xaxes(title_text="Isolation Risk", row=2, col=1)
fig2.update_xaxes(title_text="Isolation Risk", row=2, col=2)

fig2.update_layout(
    title=dict(
        text="<b>County Isolation Risk — Statistical Summary</b>",
        x=0.5, xanchor="center",
    ),
    height=1100,
    width=1400,
    showlegend=False,
)

output_stats = out_dir / "national_fragility_stats.html"
fig2.write_html(output_stats)
print(f"  Saved {output_stats}")

# ============================================================
# VIZ 3: Correlation — graph size vs risk
# ============================================================
print("Building correlation scatter...")

fig3 = px.scatter(
    x=[r["graph_nodes"] for r in results.values()],
    y=[r["isolation_risk"] for r in results.values()],
    color=[r["state_name"] for r in results.values()],
    hover_name=[f"{r['county_name']}, {r['state_name']}" for r in results.values()],
    log_x=True,
    labels={"x": "Graph Nodes (log scale)", "y": "Isolation Risk"},
    title="<b>County Road Network Size vs Isolation Risk</b><br>"
          "<sub>Larger networks don't necessarily mean lower risk</sub>",
    height=700,
    width=1400,
)
fig3.update_traces(marker=dict(size=5, opacity=0.6))
fig3.update_layout(showlegend=False)

output_scatter = out_dir / "national_fragility_scatter.html"
fig3.write_html(output_scatter)
print(f"  Saved {output_scatter}")

print(f"\nAll visualizations saved to {out_dir}/")
print("  national_fragility_map.html     — Choropleth of all 3,221 counties")
print("  national_fragility_stats.html   — Distribution + rankings")
print("  national_fragility_scatter.html — Graph size vs risk")
