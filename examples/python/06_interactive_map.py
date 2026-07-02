"""Interactive fragility maps (gravel.viz Tier 2, lonboard/WebGL).

Builds a coordinate-bearing grid, runs stochastic fragility, and exports an interactive
standalone HTML map (pan/zoom, colored by per-edge P(fail)). In a notebook, just display
the returned Map. Runs without OSM data.

    pip install gravel-fragility[viz]
    python examples/python/06_interactive_map.py     # writes fragility_interactive.html
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))

import gravel  # noqa: E402
from gravel import viz  # noqa: E402


def coord_grid(n):
    src, tgt = [], []
    for r in range(n):
        for c in range(n):
            for dr, dc in ((0, 1), (1, 0)):
                rr, cc = r + dr, c + dc
                if rr < n and cc < n:
                    a, b = r * n + c, rr * n + cc
                    src += [a, b]
                    tgt += [b, a]
    coords = np.array([[r, c] for r in range(n) for c in range(n)], dtype=np.float64)
    return gravel.Graph.from_coo(
        n * n, np.array(src, np.uint32), np.array(tgt, np.uint32), np.ones(len(src)), coords
    )


def main():
    g = coord_grid(30)
    ch = gravel.build_ch(g)
    idx = gravel.ShortcutIndex(ch)

    cfg = gravel.StochasticFragilityConfig()
    cfg.monte_carlo_runs = 100
    cfg.seed = 3
    cfg.od_sample_count = 40
    res = gravel.stochastic_fragility(g, ch, idx, [0.06] * g.edge_count, cfg)

    m = viz.interactive_map(g, res)  # in a notebook: just `m` to display it
    out = "fragility_interactive.html"
    m.to_html(out)
    print(f"wrote {out}  ({g.edge_count} edges)")


if __name__ == "__main__":
    main()
