"""Tests for gravel.datasets.chicago_traffic.

The live-endpoint tests are gated behind GRAVEL_LIVE_TESTS=1 so CI stays deterministic (the Chicago
Data Portal's historical dataset is large and its aggregations can be slow). The structural test runs
everywhere.
"""
import os

import pytest
from gravel.datasets import chicago_traffic as ct

LIVE = os.environ.get("GRAVEL_LIVE_TESTS") == "1"
CORRIDOR = (41.885, -87.655, 41.905, -87.640)  # a Halsted/near-north box


def test_module_shape():
    # No network: dataclasses, sentinel, and attribution are well-formed.
    s = ct.Segment("1", "Halsted", "SB", "Grand", "Chicago", 0.5, (41.90, -87.64), (41.91, -87.64))
    assert s.segment_id == "1" and s.direction == "SB"
    ev = ct.ClosureEvent("1", "Halsted", "SB", "2025-08-05", "2025-08-15", 11, 80.0)
    assert ev.days == 11 and ev.neighbors == []
    assert ct.NO_DATA == -1.0
    assert "Chicago" in ct.ATTRIBUTION


@pytest.mark.skipif(not LIVE, reason="set GRAVEL_LIVE_TESTS=1 to hit the live Chicago Data Portal")
def test_load_segments_corridor():
    g, segs, prov = ct.load_segments(bbox=CORRIDOR)
    assert g.node_count > 0 and g.edge_count > 0
    assert len(segs) == g.edge_count  # segments re-aligned to CSR edge order
    assert prov.dataset_id == "chicago_traffic_tracker"
    # every present segment carries a street name and endpoints
    present = [s for s in segs if s]
    assert present and all(s.length_mi > 0 for s in present)


@pytest.mark.skipif(not LIVE, reason="set GRAVEL_LIVE_TESTS=1 to hit the live Chicago Data Portal")
def test_free_flow_speeds_positive():
    _, segs, _ = ct.load_segments(bbox=CORRIDOR)
    ids = [s.segment_id for s in segs if s][:8]
    ff = ct.free_flow_speeds(ids)
    assert ff and all(v > 0 for v in ff.values())
