"""Animated failure playback (gravel.viz Tier 2).

Builds a progressive (greedy) fragility result and constructs an animated widget that
scrubs the removal order — failed edges recede to grey as the round advances. The widget
is notebook-interactive: in Jupyter, display the returned object and press play.

    pip install gravel-fragility[viz]
    # then, in a notebook:
    #   from examples.python... import build; w = build(); w
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


def build():
    """Return the animated widget (display it in a notebook)."""
    n = 16
    g = coord_grid(n)
    ch = gravel.build_ch(g)
    idx = gravel.ShortcutIndex(ch)

    cfg = gravel.ProgressiveFragilityConfig()
    box = gravel.Polygon()
    box.vertices = [
        gravel.Coord(-1, -1), gravel.Coord(-1, n), gravel.Coord(n, n), gravel.Coord(n, -1)
    ]
    bc = cfg.base_config
    bc.boundary = box
    bc.od_sample_count = 30
    cfg.base_config = bc
    cfg.selection_strategy = gravel.SelectionStrategy.GREEDY_BETWEENNESS
    cfg.k_max = 30
    prog = gravel.progressive_fragility(g, ch, idx, cfg)

    return viz.animate_failure(g, prog)


if __name__ == "__main__":
    w = build()
    print(f"built animated widget: {type(w).__name__} — display it in a Jupyter notebook to play")
