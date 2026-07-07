"""Back-compat: ``gravel.hazards`` is a deprecated shim over ``gravel.datasets``.

Verifies the old names still work and emit ``DeprecationWarning``, forwarding to
their new homes. The substantive hazard tests live in ``test_datasets.py``.
Removed in Gravel 3.0.
"""

import gravel
import pytest
from gravel import hazards
from gravel.datasets import nfhl


def test_deprecated_constants_warn_and_forward():
    for old, expected in [
        ("NFHL_EVENT_CLOSURE", nfhl.EVENT_CLOSURE),
        ("NFHL_ANNUAL_PROBABILITY", nfhl.ANNUAL_PROBABILITY),
        ("NFHL_ENDPOINT", nfhl.ENDPOINT),
        ("NFHL_FLOOD_ZONE_LAYER", nfhl.FLOOD_ZONE_LAYER),
        ("NFHL_ZONE_COLORS", nfhl.ZONE_COLORS),
        ("NFHL_DEFAULT_ZONE_COLOR", nfhl._DEFAULT_ZONE_COLOR),
    ]:
        with pytest.warns(DeprecationWarning):
            assert getattr(hazards, old) == expected


def test_deprecated_functions_warn_and_forward():
    with pytest.warns(DeprecationWarning):
        assert hazards.nfhl_zone_color("AE") == nfhl.zone_color("AE")
    g = gravel.make_grid_graph(4, 4)
    with pytest.warns(DeprecationWarning):
        probs = hazards.hazard_edge_probabilities(g, [], baseline=0.02)
    assert probs.shape == (g.edge_count,)


def test_unknown_attribute_raises():
    with pytest.raises(AttributeError):
        _ = hazards.does_not_exist


def test_deprecated_top_level_loaders_warn_and_forward():
    with pytest.warns(DeprecationWarning):
        assert gravel.load_tiger_counties is gravel.datasets.tiger.counties
    with pytest.warns(DeprecationWarning):
        assert gravel.load_osm_graph is gravel.datasets.osm.load
