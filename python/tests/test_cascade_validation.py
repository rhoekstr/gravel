"""Deterministic (no-network) coverage for scripts/validate_cascade_powerflow.py —
the OPFData schema parser and the betweenness-vs-flow statistics behind the 3.0 cascade
verdict. The live study fetches OPFData; here we hand it a tiny synthetic example."""
import importlib.util
import json
import os

import numpy as np
import pytest

_SCRIPT = os.path.join(
    os.path.dirname(__file__), "..", "..", "scripts", "validate_cascade_powerflow.py"
)


def _load_module():
    spec = importlib.util.spec_from_file_location("val_cascade", _SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


val = pytest.importorskip("gravel") and _load_module()


def _synthetic_example(tmp_path):
    """A 4-bus square with a diagonal: known topology + hand-set solved flows.
    grid.edges.ac_line.features = [angmin, angmax, b_fr, b_to, br_r, br_x, rate_a, ..].
    solution.edges.ac_line.features = [pf, qf, pt, qt]."""
    def line(br_x, rate_a):
        return [-0.5, 0.5, 0.0, 0.0, 0.001, br_x, rate_a, rate_a, rate_a]
    grid = {
        "nodes": {"bus": {"features": [[0]] * 4}},
        "edges": {"ac_line": {
            "senders":   [0, 1, 2, 3, 0],
            "receivers": [1, 2, 3, 0, 2],
            "features":  [line(0.1, 5.0), line(0.1, 5.0), line(0.1, 5.0),
                          line(0.1, 5.0), line(0.05, 8.0)],
        }},
    }
    sol = {"edges": {"ac_line": {
        "senders": [0, 1, 2, 3, 0], "receivers": [1, 2, 3, 0, 2],
        # |S| = hypot(pf, qf): ascending so rank correlation is well-defined
        "features": [[1.0, 0.0, -1.0, 0.0], [2.0, 0.0, -2.0, 0.0],
                     [3.0, 0.0, -3.0, 0.0], [4.0, 0.0, -4.0, 0.0],
                     [5.0, 0.0, -5.0, 0.0]],
    }}}
    p = tmp_path / "example_0.json"
    p.write_text(json.dumps({"grid": grid, "solution": sol, "metadata": {}}))
    return str(p)


def test_parse_example_reads_limits_and_solved_flow(tmp_path):
    n_bus, branches = val.parse_example(_synthetic_example(tmp_path))
    assert n_bus == 4
    assert len(branches) == 5
    S = sorted(b["S"] for b in branches)
    assert S == [1.0, 2.0, 3.0, 4.0, 5.0]
    diag = next(b for b in branches if {b["u"], b["v"]} == {0, 2})
    assert diag["rate_a"] == 8.0 and abs(diag["br_x"] - 0.05) < 1e-9


def test_spearman_and_topk_helpers():
    assert val._spearman([1, 2, 3, 4], [1, 2, 3, 4]) == pytest.approx(1.0)
    assert val._spearman([1, 2, 3, 4], [4, 3, 2, 1]) == pytest.approx(-1.0)
    # perfect agreement on the top-half ranking
    assert val._topk_overlap(np.array([1, 2, 3, 4]), np.array([1, 2, 3, 4]), 0.5) == 1.0
    assert val._topk_overlap(np.array([1, 2, 3, 4]), np.array([4, 3, 2, 1]), 0.5) == 0.0


def test_branch_betweenness_runs_and_aligns(tmp_path):
    n_bus, branches = val.parse_example(_synthetic_example(tmp_path))
    bc = val.branch_betweenness(n_bus, branches, "bx")
    assert bc.shape == (5,)
    assert np.all(bc >= 0)
