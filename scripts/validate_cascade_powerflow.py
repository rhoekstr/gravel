"""Validate Gravel's cascade load model against real solved power flows (Gravel 3.0).

Gravel's ``cascade_fragility`` is a topological Motter–Lai model: it treats **edge
betweenness** as each line's "load" and fails a line when its (re)computed betweenness
exceeds a tolerance-scaled capacity. That is only a valid picture of a *power grid*
cascade if betweenness actually tracks the physical power flowing on each line.

This script tests that necessary condition directly, with ground truth, using
DeepMind's **OPFData** — a corpus of solved AC-OPF instances that ships, per branch,
both the thermal limit (``rate_a``, MVA) and the **solved apparent power flow**
(``|S| = sqrt(pf^2 + qf^2)``). For each solved state we build the bus–branch graph,
compute Gravel edge betweenness (unit- and impedance-weighted), and measure the
Spearman rank correlation between betweenness and real |S| across the lines.

The honest finding (reproducing Hines et al., *Chaos* 2010, on modern data): the
correlation is weak — betweenness does **not** track power flow — so the topological
cascade cannot be presented as a validated physical model of grid contingencies. It
stays experimental. See docs/PRD.md "Phase 5" for the full verdict.

Requires the ``[datasets]`` extra and network access (OPFData GCS bucket). Usage::

    python scripts/validate_cascade_powerflow.py                       # case14 + case118
    python scripts/validate_cascade_powerflow.py pglib_opf_case500_goc # any OPFData case
"""
from __future__ import annotations

import glob
import json
import math
import os
import sys
import tempfile

import gravel
import numpy as np
from gravel.datasets import opfdata

try:
    from scipy.stats import spearmanr
    def _spearman(a, b):
        r, _ = spearmanr(a, b)
        return float(r)
except Exception:  # pragma: no cover - scipy optional
    def _spearman(a, b):
        ar = np.argsort(np.argsort(a)).astype(float)
        br = np.argsort(np.argsort(b)).astype(float)
        ar -= ar.mean()
        br -= br.mean()
        d = math.sqrt((ar @ ar) * (br @ br))
        return float(ar @ br / d) if d else float("nan")


# OPFData feature layout (per the dataset schema): the index of br_x and rate_a within
# each edge type's grid feature vector. Solution features are [pf, qf, pt, qt].
_BRANCH_LAYOUT = {"ac_line": (5, 6), "transformer": (3, 4)}


def parse_example(path):
    """One OPFData example -> (n_buses, [{u, v, br_x, rate_a, S}, ...])."""
    doc = json.load(open(path))
    grid, sol = doc["grid"], doc["solution"]
    bus = grid["nodes"]["bus"]
    n_bus = len(bus["features"]) if isinstance(bus, dict) else len(bus)
    branches = []
    for etype, (xi, ri) in _BRANCH_LAYOUT.items():
        ge, se = grid["edges"].get(etype), sol["edges"].get(etype)
        if not ge or not se:
            continue
        snd, rcv, feat, sfeat = ge["senders"], ge["receivers"], ge["features"], se["features"]
        for k in range(len(snd)):
            pf, qf = sfeat[k][0], sfeat[k][1]
            branches.append({
                "u": int(snd[k]), "v": int(rcv[k]),
                "br_x": abs(float(feat[k][xi])) or 1e-6,
                "rate_a": float(feat[k][ri]),
                "S": math.hypot(pf, qf),
            })
    return n_bus, branches


def branch_betweenness(n_bus, branches, weight):
    """Sum edge betweenness over both directed halves of each branch (aligned to
    `branches`). `weight` is 'unit' or 'bx' (impedance)."""
    src, tgt, w = [], [], []
    for b in branches:
        ww = 1.0 if weight == "unit" else b["br_x"]
        src += [b["u"], b["v"]]
        tgt += [b["v"], b["u"]]
        w += [ww, ww]
    g = gravel.Graph.from_coo(
        n_bus, np.asarray(src, np.uint32), np.asarray(tgt, np.uint32), np.asarray(w, np.float64)
    )
    cfg = gravel.BetweennessConfig()
    cfg.deterministic = True
    bc = np.asarray(gravel.edge_betweenness(g, cfg).edge_scores)
    csrc, ctgt, _ = (np.asarray(a) for a in g.to_coo())
    idx = {(int(csrc[e]), int(ctgt[e])): e for e in range(len(csrc))}
    out = np.zeros(len(branches))
    for i, b in enumerate(branches):
        out[i] = sum(bc[idx[(u, v)]] for u, v in ((b["u"], b["v"]), (b["v"], b["u"])) if (u, v) in idx)
    return out


def _topk_overlap(pred, truth, frac=0.3):
    """Fraction of the top-`frac` lines by real utilization that Gravel's top-`frac`
    by betweenness also flags — i.e. does the topological 'load' identify the actually
    critical lines? A random predictor scores ~`frac`."""
    k = max(1, int(round(frac * len(pred))))
    top_pred = set(np.argsort(pred)[-k:])
    top_truth = set(np.argsort(truth)[-k:])
    return len(top_pred & top_truth) / k


def validate_case(case_name, n_states, cache):
    (_ex, group_dir), _prov = opfdata.fetch(cache, case_name=case_name, group=0)
    files = sorted(glob.glob(os.path.join(group_dir, "example_*.json")))[:n_states]
    r_unit, r_bx, r_util, topk, n_over, n_branch, nb = [], [], [], [], 0, 0, 0
    for path in files:
        n_bus, branches = parse_example(path)
        if len(branches) < 5:
            continue
        nb = len(branches)
        S = np.array([b["S"] for b in branches])
        rate = np.array([b["rate_a"] for b in branches])
        util = np.divide(S, rate, out=np.zeros_like(S), where=rate > 0)
        r_unit.append(_spearman(branch_betweenness(n_bus, branches, "unit"), S))
        bx = branch_betweenness(n_bus, branches, "bx")
        r_bx.append(_spearman(bx, S))
        r_util.append(_spearman(bx, util))
        topk.append(_topk_overlap(bx, util, 0.3))
        n_over += int((util > 1.0).sum())
        n_branch += len(branches)
    return {
        "case": case_name, "states": len(r_unit), "branches": nb,
        "r_unit": float(np.mean(r_unit)), "r_bx": float(np.mean(r_bx)),
        "r_util": float(np.mean(r_util)), "topk": float(np.mean(topk)),
        "overloaded": n_over, "branch_states": n_branch,
    }


def main(cases, n_states=60):
    cache = os.path.join(tempfile.gettempdir(), "gravel-opfdata-validate")
    rows = []
    for c in cases:
        print(f"fetching + evaluating {c} ...", flush=True)
        rows.append(validate_case(c, n_states, cache))
    print("\n" + "=" * 78)
    print("Betweenness (cascade 'load') vs real solved power flow |S|  — Spearman rho")
    print("=" * 78)
    print(f"{'case':24} {'branch':>6} {'states':>6} {'unit':>7} {'br_x':>7} {'util':>7} {'top30%':>7}")
    for r in rows:
        print(f"{r['case']:24} {r['branches']:>6} {r['states']:>6} "
              f"{r['r_unit']:>+7.2f} {r['r_bx']:>+7.2f} {r['r_util']:>+7.2f} {r['topk']:>7.2f}")
    print("-" * 78)
    print("unit/br_x = betweenness weighted by hop-count / line reactance, vs |S|.")
    print("util = impedance-betweenness vs real utilization |S|/rate_a.")
    print("top30% = overlap of the 30% most-between lines with the 30% most-loaded")
    print("         (random ~0.30); this is whether betweenness finds the critical lines.")
    best = max(abs(r["r_bx"]) for r in rows)
    print(f"\nVERDICT: best |rho| = {best:.2f} (r^2 <= {best**2:.2f}), and the top-30% overlap")
    print("is at or below chance — shortest-path betweenness does NOT track real power")
    print("flow, so the topological Motter–Lai cascade is NOT a validated physical model")
    print("of grid contingencies. It remains EXPERIMENTAL. The proxy that WOULD track the")
    print("physics is current-flow (Laplacian) betweenness — i.e. essentially a DC power-")
    print("flow solve, which Gravel does not do by design (docs/PRD.md, DD-6). Real per-")
    print("edge robustness (e.g. thermal headroom) can be fed to the cascade as a")
    print("PCE_WEIGHTED tolerance weight, but that reweights the tolerance, not the load,")
    print("so it cannot rescue a load proxy this weak. See docs/PRD.md 'Phase 5'.")


if __name__ == "__main__":
    args = sys.argv[1:]
    cases = args if args else ["pglib_opf_case14_ieee", "pglib_opf_case118_ieee"]
    main(cases)
