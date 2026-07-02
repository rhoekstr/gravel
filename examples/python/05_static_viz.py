"""Static fragility maps (gravel.viz Tier 1).

Builds a small coordinate-bearing grid, runs stochastic fragility, and renders a
colorblind-safe static map with `plot_fragility`. Runs without OSM data.

    pip install gravel-fragility[viz]
    python examples/python/05_static_viz.py        # writes fragility_map.png
"""

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "python"))

import gravel  # noqa: E402
from gravel import viz  # noqa: E402


def coord_grid(n):
    """n x n 4-connected bidirectional grid with lat/lon coordinates."""
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
    g = coord_grid(12)
    ch = gravel.build_ch(g)
    idx = gravel.ShortcutIndex(ch)

    # Higher failure probability on a diagonal band — stands in for a hazard footprint.
    s, t, _ = g.to_coo()
    xy = g.node_coordinates()
    probs = [
        0.4 if abs(xy[u, 0] - xy[u, 1]) < 1.5 else 0.03
        for u, v in zip(s, t, strict=True)
    ]

    cfg = gravel.StochasticFragilityConfig()
    cfg.monte_carlo_runs = 200
    cfg.seed = 7
    cfg.od_sample_count = 40
    res = gravel.stochastic_fragility(g, ch, idx, probs, cfg)
    print(f"mean inflation {res.mean:.3f}   disconnected {res.mean_disconnected_fraction:.3f}")

    ax = viz.plot_fragility(
        g, res, cmap="viridis", title="Stochastic fragility — P(edge fails)"
    )
    ax.figure.savefig("fragility_map.png", dpi=200, bbox_inches="tight")
    print("wrote fragility_map.png")


if __name__ == "__main__":
    main()
