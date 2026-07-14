"""Real-data flow-layer calibration on a Chicago road closure — and its honest limit.

This example walks the full 3.0 flow-layer pipeline end to end on **real open data**, and then reports,
plainly, what it *cannot* do. It is a study, not a success demo.

The pipeline (all from ``gravel``):

  1. ``gravel.datasets.chicago_traffic`` — load a corridor as a routable graph from the City of
     Chicago Traffic Tracker (bus-probe arterial speeds), derive free-flow, and find a real road
     **closure** as a natural experiment (a segment goes dark while a neighbor slows).
  2. ``gravel.flow`` — the stochastic User-Equilibrium layer whose ``calibrate_theta`` harness this
     observed diversion would feed. This script measures the diversion signal and reports the verdict;
     the full corridor calibration (routed O-D seed + capacities) is in ``docs/FLOW_LAYER.md``.

**The finding (honest verdict, v3.0).** On a corridor-scale network, ``theta`` does **not** identify
from open speed data: the model reproduces the *average* congestion but not *which* alternates absorbed
the diverted traffic, at any ``theta`` (pattern correlation ~0, flat error curve). The full study
(``docs/FLOW_LAYER.md``) traces this to **boundary effects** — real traffic reroutes *around* a 1-2 km
corridor along paths a corridor-scale model cannot see. So the flow layer ships **experimental**: the
solver is exact (validated on Sioux Falls) and recovers a known ``theta`` on synthetic data, but real
``theta`` identification needs regional-scale assignment (city network + CMAP TAZ demand + Spiess ODME
against all camera counts). This script demonstrates the machinery and the gap, not a graduation.

Requires network access (Chicago Data Portal). Runtime is dominated by the historical-speed pulls.
"""
from __future__ import annotations

import numpy as np
from gravel.datasets import chicago_traffic as ct

# The Halsted SB closure of Aug 5-15 2025 (found by detect_closures in the 23-month sweep); its
# near-north corridor. Using a known event keeps the example bounded; pass --detect to hunt live.
CORRIDOR = (41.884, -87.656, 41.905, -87.638)
CLOSURE = ("2025-08-05", "2025-08-15")
BASELINE = ("2025-08-19", "2025-08-27")  # matched post-closure weeks (same weekdays, closure absent)


def main() -> None:
    print("1. Loading the Halsted corridor from Chicago Traffic Tracker ...", flush=True)
    g, segs, prov = ct.load_segments(bbox=CORRIDOR)
    present = [s for s in segs if s]
    print(f"   {g.node_count} nodes, {g.edge_count} edges, {len(present)} segments  [{prov.resolved_version}]")

    ids = [s.segment_id for s in present]
    ff = ct.free_flow_speeds(ids)
    dur = ct.congestion_profile(ids, *CLOSURE)        # speeds during the closure
    base = ct.congestion_profile(ids, *BASELINE)      # matched baseline speeds
    print(f"   free-flow derived for {len(ff)} segments; congestion profiles pulled", flush=True)

    # 2. Observed diversion: segments that slowed during the closure vs their matched baseline.
    #    (t/t0 = free_flow / observed; a rise = congestion.) These are the monitored links.
    def peak_ratio(prof: dict) -> float | None:
        if not prof:
            return None
        worst = min(prof.values())
        return None if worst <= 0 else worst

    seg_by_id = {s.segment_id: s for s in present}
    monitored, observed = [], []
    for sid in ids:
        if sid not in ff or sid not in dur or sid not in base:
            continue
        d, b = peak_ratio(dur.get(sid, {})), peak_ratio(base.get(sid, {}))
        if d is None or b is None:
            continue
        extra = b / d  # >1 means slower during the closure than baseline (absorbed diversion)
        if extra > 1.02:
            s = seg_by_id[sid]
            monitored.append(s)
            observed.append(extra)
    print(f"\n2. Observed diversion: {len(monitored)} segments slowed during the closure")
    for s, e in sorted(zip(monitored, observed, strict=True), key=lambda x: -x[1])[:6]:
        print(f"   {s.street:16s} {s.direction:3s}  x{e:.2f} slower during closure")

    if len(monitored) < 4:
        print("\n   Too few corroborating segments this run (live data varies); see docs/FLOW_LAYER.md.")
        return

    # 3. The calibration verdict. This observed slowdown pattern is what gravel.flow.calibrate_theta
    #    fits theta against. The full corridor run (docs/FLOW_LAYER.md) found the error curve is FLAT —
    #    theta is under-identified — because the diversion the model predicts on these alternates does
    #    not depend on theta once around-the-corridor rerouting (invisible here) is missing.
    obs_arr = np.array(observed)
    print(f"\n3. Observed extra-slowdown: mean {obs_arr.mean():.3f}, spread {obs_arr.std():.3f} "
          f"over {len(obs_arr)} segments — the signal calibrate_theta fits against.")
    print("   VERDICT (v3.0): flow layer ships EXPERIMENTAL. Solver is exact (Sioux Falls) and recovers")
    print("   a known theta on synthetic data, but real theta does NOT identify at corridor scale —")
    print("   it needs regional-scale ODME (docs/FLOW_LAYER.md, Phase F3). This script shows that gap.")


if __name__ == "__main__":
    main()
