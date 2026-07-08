"""Multi-state transmission grid × multi-hazard map (US East Coast by default).

End-to-end use of the 2.6 + 2.7 datasets stack, across a whole region:

  GridSFM power grids  (gravel.datasets.gridsfm — one model per state, buses/branches
                        with coordinates), loaded and merged into one graph
  +  FEMA NRI multi-hazard county surface  (gravel.datasets.nri, one pull for all states)
  ->  per-branch exposure = the higher NRI rating of the branch's two endpoint counties
  ->  one standalone HTML file: a county choropleth + the grid overlay, with a
      hazard-layer toggle (composite + five hazards), hover, and pan/zoom. No
      external tiles/scripts/fonts — fully offline.

GridSFM ships per-state models (all 48 contiguous states), so an "East Coast grid" is
the 14 Atlantic-seaboard state grids merged. They're independent per-state networks
(GridSFM's interconnected ``eastern`` region is the alternative, but it spans the whole
Eastern Interconnection); at regional zoom the state grids tile together geographically.

Requires the ``[datasets]`` extra (geopandas/shapely/pyproj) and network access
(Hugging Face for the grid cases, FEMA ArcGIS for NRI). Runs for any set of states::

    python examples/python/09_power_grid_multihazard.py                 # East Coast (default)
    python examples/python/09_power_grid_multihazard.py florida georgia south_carolina

The NRI product requires FEMA attribution on any derived map (``nri.ATTRIBUTION``);
this example writes it into the page footer.
"""
from __future__ import annotations

import json
import math
import os
import sys
import tempfile

import geopandas as gpd
import numpy as np
from gravel.datasets import gridsfm, nri
from shapely.geometry import Point

# Atlantic seaboard, Maine -> Florida (GridSFM state-model names).
EAST_COAST = [
    "maine", "new_hampshire", "massachusetts", "rhode_island", "connecticut",
    "new_york", "new_jersey", "delaware", "maryland", "virginia",
    "north_carolina", "south_carolina", "georgia", "florida",
]

# (key, NRI rating field, human label). Composite first (default layer).
LAYERS = [
    ("composite", "RISK_RATNG", "Composite Risk"),
    ("hurricane", "HRCN_RISKR", "Hurricane"),
    ("inland_flood", "IFLD_RISKR", "Inland Flooding"),
    ("coastal_flood", "CFLD_RISKR", "Coastal Flooding"),
    ("landslide", "LNDS_RISKR", "Landslide"),
    ("tornado", "TRND_RISKR", "Tornado"),
]
RANK = {
    "Very Low": 1, "Relatively Low": 2, "Relatively Moderate": 3,
    "Relatively High": 4, "Very High": 5,
}
PROB = [0.0, 0.01, 0.03, 0.07, 0.15, 0.30]  # index -> illustrative annual P(fail)


def ridx(r) -> int:
    return RANK.get(str(r).strip(), 0)


def main(states, out_path, region_label) -> None:
    cache = os.path.join(tempfile.gettempdir(), "gravel-gridsfm-cache")

    # ---- 1. load each state's GridSFM grid and merge into one graph ----
    coords_parts, src_parts, tgt_parts, cap_parts = [], [], [], []
    offset = 0
    loaded = []
    for st in states:
        try:
            (model_path,), _prov = gridsfm.fetch(cache, st, hour="16h")
            g, cap = gridsfm.load(model_path)
        except Exception as exc:  # a missing state model shouldn't sink the whole map
            print(f"  ! skipping {st}: {type(exc).__name__}: {str(exc)[:80]}", flush=True)
            continue
        c = np.asarray(g.node_coordinates())  # (n, 2) = (lat, lon)
        s, t, _w = g.to_coo()
        coords_parts.append(c)
        src_parts.append(np.asarray(s) + offset)
        tgt_parts.append(np.asarray(t) + offset)
        cap_parts.append(np.asarray(cap, dtype=np.float64))
        offset += g.node_count
        loaded.append(st)
        print(f"  {st}: {g.node_count} buses, {g.edge_count} edges", flush=True)

    if not loaded:
        raise SystemExit("no state grids loaded")
    coords = np.vstack(coords_parts)
    src = np.concatenate(src_parts)
    tgt = np.concatenate(tgt_parts)
    cap = np.concatenate(cap_parts)
    print(f"merged: {len(coords)} buses across {len(loaded)} states", flush=True)

    # ---- 2. one FEMA NRI pull covering all loaded states ----
    fields = "STATE,COUNTY,STCOFIPS,RISK_RATNG," + ",".join(
        f for _, f, _ in LAYERS if f != "RISK_RATNG"
    )
    names = sorted({s.replace("_", " ").title() for s in loaded})  # NRI 'STATE' = full name
    where = "STATE IN (" + ",".join(f"'{n}'" for n in names) + ")"
    print(f"fetching NRI counties for {len(names)} states ...", flush=True)
    gdf, nprov = nri.fetch(geography="county", where=where, out_fields=fields)
    gdf = gdf.to_crs("EPSG:4326").reset_index(drop=True)
    print(f"  NRI: {len(gdf)} counties, release {nprov.resolved_version}", flush=True)

    # ---- 3. assign each bus to its county, then per-layer rating index ----
    buses = gpd.GeoDataFrame(
        geometry=[Point(lon, lat) for lat, lon in coords], crs="EPSG:4326"
    )
    joined = gpd.sjoin(buses, gdf, predicate="within", how="left")
    joined = joined[~joined.index.duplicated(keep="first")].reindex(range(len(coords)))
    node_idx = {}
    for key, field, _ in LAYERS:
        col = joined[field] if field in joined else None
        node_idx[key] = np.array(
            [ridx(col.iloc[i]) if col is not None else 0 for i in range(len(coords))],
            dtype=np.int8,
        )

    # per-edge exposure = max rating of the two endpoints; dedupe undirected branches
    branch = {}
    for e in range(len(src)):
        u, v = int(src[e]), int(tgt[e])
        k = (u, v) if u < v else (v, u)
        c = float(cap[e]) if e < len(cap) else 0.0
        branch[k] = max(branch.get(k, 0.0), c)
    print(f"  {len(branch)} undirected branches", flush=True)

    # ---- 4. projection (equirectangular, aspect-corrected) ----
    cminlon, cminlat, cmaxlon, cmaxlat = gdf.total_bounds
    nlat, nlon = coords[:, 0], coords[:, 1]
    minlon = min(cminlon, float(nlon.min()))
    maxlon = max(cmaxlon, float(nlon.max()))
    minlat = min(cminlat, float(nlat.min()))
    maxlat = max(cmaxlat, float(nlat.max()))
    pad = 0.03 * max(maxlon - minlon, maxlat - minlat)
    minlon -= pad
    maxlon += pad
    minlat -= pad
    maxlat += pad
    meanlat = math.radians((minlat + maxlat) / 2)
    W = 1000.0
    H = W * (maxlat - minlat) / ((maxlon - minlon) * math.cos(meanlat))

    def px(lat, lon):
        x = (lon - minlon) / (maxlon - minlon) * W
        y = (maxlat - lat) / (maxlat - minlat) * H
        return round(x, 1), round(y, 1)

    # ---- 5. county paths (simplified) + per-layer rating index ----
    gsimp = gdf.copy()
    gsimp["geometry"] = gsimp.geometry.simplify(0.02, preserve_topology=True)
    counties = []
    for i, row in gsimp.iterrows():
        geom = row.geometry
        if geom is None or geom.is_empty:
            continue
        polys = geom.geoms if geom.geom_type == "MultiPolygon" else [geom]
        d = []
        for poly in polys:
            pts = list(poly.exterior.coords)
            d.append("M" + " ".join(f"{px(lat, lon)[0]},{px(lat, lon)[1]}" for lon, lat in pts) + "Z")
        rr = [ridx(gdf.loc[i, f]) if f in gdf else ridx(gdf.loc[i, "RISK_RATNG"])
              for _, f, _ in LAYERS]
        counties.append({"d": " ".join(d), "n": str(row["COUNTY"]), "r": rr})

    # ---- 6. edges (deduped) as projected geometry; width by capacity ----
    capvals = [c for c in branch.values() if c > 0]
    cmax = max(capvals) if capvals else 1.0

    def width(c):
        return round(0.35 + 2.4 * math.sqrt(max(c, 0.0) / cmax), 2)

    edges = []
    for (u, v), c in branch.items():
        x1, y1 = px(coords[u, 0], coords[u, 1])
        x2, y2 = px(coords[v, 0], coords[v, 1])
        rr = [int(max(node_idx[key][u], node_idx[key][v])) for key, _, _ in LAYERS]
        edges.append({"a": [x1, y1], "b": [x2, y2], "w": width(c),
                      "u": u, "v": v, "c": round(c), "r": rr})

    comp = node_idx["composite"]
    stats = {
        "buses": int(len(coords)), "branches": len(branch), "states": len(loaded),
        "cap_gw": round(sum(branch.values()) / 1000.0, 1),
        "buses_high": int((comp >= 4).sum()), "counties": len(counties),
    }
    print("  stats:", stats, flush=True)

    data = {
        "layers": [{"key": k, "label": lab} for k, _, lab in LAYERS],
        "W": round(W, 1), "H": round(H, 1),
        "counties": counties, "edges": edges,
        "prob": PROB, "stats": stats,
        "title": f"{region_label} Transmission Grid — Multi-Hazard Failure Exposure",
        "prov": {
            "grid": f"GridSFM · {len(loaded)} states",
            "nri": f"FEMA NRI counties · {nprov.resolved_version}",
        },
        "attribution": nri.ATTRIBUTION,
    }

    html = TEMPLATE.replace("__DATA__", json.dumps(data, separators=(",", ":")))
    with open(out_path, "w") as fh:
        fh.write(html)
    print(f"wrote {out_path} ({os.path.getsize(out_path) / 1024:.0f} KB)", flush=True)


TEMPLATE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Transmission Grid — Multi-Hazard Exposure</title>
<style>
  :root{
    --bg:#0f1216; --panel:#171b21; --ink:#e8ecf1; --muted:#8b96a5;
    --line:#252b34; --accent:#6ea8fe;
    --r0:#9aa5b1; --r1:#1a9850; --r2:#91cf60; --r3:#fee08b; --r4:#fc8d59; --r5:#d73027;
  }
  *{box-sizing:border-box}
  html,body{margin:0;height:100%}
  body{background:var(--bg);color:var(--ink);
    font:14px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
    display:flex;flex-direction:column;overflow:hidden}
  header{padding:14px 20px 10px;border-bottom:1px solid var(--line)}
  h1{font-size:18px;font-weight:650;margin:0;letter-spacing:-.01em}
  .sub{color:var(--muted);font-size:12.5px;margin-top:3px}
  .sub b{color:var(--ink);font-weight:600}
  .bar{display:flex;flex-wrap:wrap;gap:6px;padding:10px 20px;border-bottom:1px solid var(--line);align-items:center}
  .bar .lbl{color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:.08em;margin-right:4px}
  button.layer{background:var(--panel);color:var(--muted);border:1px solid var(--line);
    padding:6px 12px;border-radius:999px;cursor:pointer;font-size:13px;transition:.12s}
  button.layer:hover{color:var(--ink);border-color:#39424e}
  button.layer.on{background:var(--accent);color:#0b1622;border-color:var(--accent);font-weight:600}
  main{flex:1;position:relative;overflow:hidden;min-height:340px}
  svg{width:100%;height:100%;display:block;cursor:grab;background:
    radial-gradient(1200px 700px at 60% -10%, #182029 0%, var(--bg) 70%)}
  svg.grab{cursor:grabbing}
  .county{stroke:#2b323c;stroke-width:.4;vector-effect:non-scaling-stroke;transition:fill .25s}
  .edge{stroke-linecap:round;vector-effect:non-scaling-stroke;opacity:.92;transition:stroke .25s}
  .legend{position:absolute;right:16px;bottom:16px;background:rgba(23,27,33,.94);
    border:1px solid var(--line);border-radius:10px;padding:12px 14px;min-width:190px;backdrop-filter:blur(6px)}
  .legend h4{margin:0 0 8px;font-size:11px;text-transform:uppercase;letter-spacing:.08em;color:var(--muted)}
  .legrow{display:flex;align-items:center;gap:8px;margin:3px 0;font-size:12.5px}
  .sw{width:16px;height:11px;border-radius:2px;flex:none}
  .legend .fine{color:var(--muted);font-size:11px;margin-top:8px;line-height:1.4}
  .tip{position:absolute;pointer-events:none;background:rgba(12,15,19,.97);border:1px solid #333c48;
    border-radius:8px;padding:8px 10px;font-size:12.5px;color:var(--ink);opacity:0;transition:opacity .1s;
    max-width:240px;box-shadow:0 6px 24px rgba(0,0,0,.45);z-index:5}
  .tip b{color:#fff}.tip .k{color:var(--muted)}
  footer{padding:8px 20px;border-top:1px solid var(--line);color:var(--muted);font-size:11px;
    display:flex;justify-content:space-between;gap:16px;flex-wrap:wrap}
  footer .attr{max-width:70%;line-height:1.35}
  .hint{position:absolute;left:16px;top:12px;color:var(--muted);font-size:11.5px;
    background:rgba(23,27,33,.8);padding:4px 8px;border-radius:6px;border:1px solid var(--line)}
</style></head>
<body>
<header>
  <h1 id="title"></h1>
  <div class="sub" id="sub"></div>
</header>
<div class="bar"><span class="lbl">Hazard</span><span id="btns"></span></div>
<main>
  <svg id="map" preserveAspectRatio="xMidYMin meet"></svg>
  <div class="hint">drag to pan · scroll to zoom</div>
  <div class="legend">
    <h4 id="legtitle">Risk rating</h4>
    <div id="legrows"></div>
    <div class="fine">County fill = NRI rating.<br>Line color = higher-exposed endpoint.<br>Line width = branch capacity (MVA).</div>
  </div>
  <div class="tip" id="tip"></div>
</main>
<footer>
  <span class="attr" id="attr"></span>
  <span id="prov"></span>
</footer>
<script>
const DATA = __DATA__;
const RC = ["--r0","--r1","--r2","--r3","--r4","--r5"].map(v=>
  getComputedStyle(document.documentElement).getPropertyValue(v).trim());
const RLAB = ["No rating","Very Low","Relatively Low","Relatively Moderate","Relatively High","Very High"];
const svg = document.getElementById("map");
svg.setAttribute("viewBox", `0 0 ${DATA.W} ${DATA.H}`);
const NS = "http://www.w3.org/2000/svg";
let layer = 0;

const gC = document.createElementNS(NS,"g");
const gE = document.createElementNS(NS,"g");
DATA.counties.forEach(c=>{
  const p = document.createElementNS(NS,"path");
  p.setAttribute("d", c.d); p.setAttribute("class","county");
  p.__c = c; p.addEventListener("mousemove",e=>tipCounty(e,c));
  p.addEventListener("mouseleave",hideTip);
  gC.appendChild(p);
});
DATA.edges.forEach(ed=>{
  const l = document.createElementNS(NS,"line");
  l.setAttribute("x1",ed.a[0]);l.setAttribute("y1",ed.a[1]);
  l.setAttribute("x2",ed.b[0]);l.setAttribute("y2",ed.b[1]);
  l.setAttribute("stroke-width",ed.w); l.setAttribute("class","edge");
  l.__e = ed; l.addEventListener("mousemove",e=>tipEdge(e,ed));
  l.addEventListener("mouseleave",hideTip);
  gE.appendChild(l);
});
svg.appendChild(gC); svg.appendChild(gE);

function recolor(){
  for(const p of gC.children){
    const idx = p.__c.r[layer];
    p.setAttribute("fill", hexA(RC[idx], idx===0?0.18:0.5));
  }
  for(const l of gE.children){
    const idx = l.__e.r[layer];
    l.setAttribute("stroke", RC[idx]);
    l.setAttribute("opacity", idx>=3 ? 0.98 : (idx===0?0.5:0.82));
  }
  document.getElementById("legtitle").textContent = DATA.layers[layer].label + " rating";
}
function hexA(hex,a){
  const h=hex.replace("#","");
  const r=parseInt(h.slice(0,2),16),g=parseInt(h.slice(2,4),16),b=parseInt(h.slice(4,6),16);
  return `rgba(${r},${g},${b},${a})`;
}

const btns = document.getElementById("btns");
DATA.layers.forEach((L,i)=>{
  const b=document.createElement("button"); b.className="layer"+(i===0?" on":"");
  b.textContent=L.label; b.onclick=()=>{layer=i;
    [...btns.children].forEach((x,j)=>x.classList.toggle("on",j===i)); recolor();};
  btns.appendChild(b);
});

const lr=document.getElementById("legrows");
for(let i=5;i>=1;i--){
  const row=document.createElement("div"); row.className="legrow";
  row.innerHTML=`<span class="sw" style="background:${RC[i]}"></span>`+
    `<span>${RLAB[i]} · <span style="color:var(--muted)">P≈${DATA.prob[i]}</span></span>`;
  lr.appendChild(row);
}

const tip=document.getElementById("tip");
function place(e){const r=svg.getBoundingClientRect();
  tip.style.left=(e.clientX-r.left+14)+"px"; tip.style.top=(e.clientY-r.top+14)+"px"; tip.style.opacity=1;}
function tipEdge(e,ed){const idx=ed.r[layer];
  tip.innerHTML=`<b>Branch ${ed.u} ↔ ${ed.v}</b><br>`+
    `<span class="k">capacity</span> ${ed.c} MVA<br>`+
    `<span class="k">${DATA.layers[layer].label}</span> ${RLAB[idx]} · P≈${DATA.prob[idx]}`;
  place(e);}
function tipCounty(e,c){const idx=c.r[layer];
  tip.innerHTML=`<b>${c.n} County</b><br><span class="k">${DATA.layers[layer].label}</span> ${RLAB[idx]}`;
  place(e);}
function hideTip(){tip.style.opacity=0;}

let vb={x:0,y:0,w:DATA.W,h:DATA.H}, drag=null;
function applyVB(){svg.setAttribute("viewBox",`${vb.x} ${vb.y} ${vb.w} ${vb.h}`);}
svg.addEventListener("wheel",e=>{e.preventDefault();
  const r=svg.getBoundingClientRect();
  const mx=vb.x+(e.clientX-r.left)/r.width*vb.w, my=vb.y+(e.clientY-r.top)/r.height*vb.h;
  const f=e.deltaY<0?0.85:1.176; const nw=Math.min(DATA.W, Math.max(DATA.W*0.05, vb.w*f));
  const nh=nw*DATA.H/DATA.W;
  vb.x=mx-(mx-vb.x)*(nw/vb.w); vb.y=my-(my-vb.y)*(nh/vb.h); vb.w=nw; vb.h=nh; applyVB();
},{passive:false});
svg.addEventListener("mousedown",e=>{drag={x:e.clientX,y:e.clientY,vx:vb.x,vy:vb.y};svg.classList.add("grab");});
window.addEventListener("mousemove",e=>{if(!drag)return;
  const r=svg.getBoundingClientRect();
  vb.x=drag.vx-(e.clientX-drag.x)/r.width*vb.w; vb.y=drag.vy-(e.clientY-drag.y)/r.height*vb.h; applyVB();});
window.addEventListener("mouseup",()=>{drag=null;svg.classList.remove("grab");});

document.getElementById("title").textContent = DATA.title;
const s=DATA.stats;
document.getElementById("sub").innerHTML =
  `<b>${s.states}</b> states · <b>${s.buses}</b> buses · <b>${s.branches}</b> branches · `+
  `<b>${s.cap_gw}</b> GW · <b>${s.buses_high}</b> buses in relatively-high / very-high composite-risk counties`;
document.getElementById("attr").textContent = DATA.attribution;
document.getElementById("prov").innerHTML =
  `${DATA.prov.grid} &nbsp;·&nbsp; ${DATA.prov.nri} &nbsp;·&nbsp; built with Gravel 2.7`;
recolor();
</script>
</body></html>"""


if __name__ == "__main__":
    args = sys.argv[1:]
    if args:
        states = [a.strip().lower().replace(" ", "_").replace("-", "_") for a in args]
        label = ", ".join(s.replace("_", " ").title() for s in states)
        if len(states) > 3:
            label = f"{len(states)}-State"
    else:
        states, label = EAST_COAST, "US East Coast"
    out = f"{label.lower().replace(' ', '_').replace(',', '')}_power_grid_multihazard.html"
    main(states, out, label)
