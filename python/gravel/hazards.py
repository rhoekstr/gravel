"""DEPRECATED — hazard fetching moved to ``gravel.datasets`` in 2.6.

This module is a thin back-compat shim kept for existing code; every name here
emits a :class:`DeprecationWarning` and forwards to its new home. It is scheduled
for **removal in Gravel 3.0**. Migrate:

===============================================  ============================================
old (``gravel.hazards``)                          new (``gravel.datasets``)
===============================================  ============================================
``fetch_nfhl_flood_zones(bbox)``                  ``datasets.nfhl.fetch(bbox)[0]``
``flood_edge_probabilities(g, zones)``            ``datasets.nfhl.edge_probabilities(g, zones)``
``hazard_edge_probabilities(g, zones)``           ``datasets._hazard.hazard_edge_probabilities(g, zones)``
``nfhl_zone_color(z)``                            ``datasets.nfhl.zone_color(z)``
``NFHL_EVENT_CLOSURE`` / ``NFHL_ANNUAL_PROBABILITY``  ``datasets.nfhl.EVENT_CLOSURE`` / ``.ANNUAL_PROBABILITY``
``NFHL_ENDPOINT`` / ``NFHL_FLOOD_ZONE_LAYER``     ``datasets.nfhl.ENDPOINT`` / ``.FLOOD_ZONE_LAYER``
``NFHL_ZONE_COLORS`` / ``NFHL_DEFAULT_ZONE_COLOR``   ``datasets.nfhl.ZONE_COLORS`` / ``._DEFAULT_ZONE_COLOR``
===============================================  ============================================

The new ``fetch`` returns ``(GeoDataFrame, Provenance)``; this shim's
``fetch_nfhl_flood_zones`` returns just the frame, preserving the old contract.
"""

from __future__ import annotations

import warnings

from .datasets import nfhl as _nfhl
from .datasets._hazard import hazard_edge_probabilities as _hazard_edge_probabilities

__all__ = [
    "fetch_nfhl_flood_zones",
    "flood_edge_probabilities",
    "hazard_edge_probabilities",
    "nfhl_zone_color",
    "NFHL_EVENT_CLOSURE",
    "NFHL_ANNUAL_PROBABILITY",
    "NFHL_ENDPOINT",
    "NFHL_FLOOD_ZONE_LAYER",
    "NFHL_ZONE_COLORS",
    "NFHL_DEFAULT_ZONE_COLOR",
]

_REMOVED_IN = "removed in Gravel 3.0"


def _warn(old: str, new: str) -> None:
    warnings.warn(
        f"gravel.hazards.{old} is deprecated; use {new} ({_REMOVED_IN}).",
        DeprecationWarning,
        stacklevel=3,
    )


def fetch_nfhl_flood_zones(bbox, **kwargs):
    """Deprecated. Use ``gravel.datasets.nfhl.fetch`` (returns ``(gdf, provenance)``)."""
    _warn("fetch_nfhl_flood_zones", "gravel.datasets.nfhl.fetch")
    gdf, _provenance = _nfhl.fetch(bbox, **kwargs)
    return gdf


def flood_edge_probabilities(graph, flood_zones, **kwargs):
    """Deprecated. Use ``gravel.datasets.nfhl.edge_probabilities``."""
    _warn("flood_edge_probabilities", "gravel.datasets.nfhl.edge_probabilities")
    return _nfhl.edge_probabilities(graph, flood_zones, **kwargs)


def hazard_edge_probabilities(graph, zones, *, baseline: float = 0.0):
    """Deprecated. Use ``gravel.datasets._hazard.hazard_edge_probabilities``."""
    _warn("hazard_edge_probabilities", "gravel.datasets._hazard.hazard_edge_probabilities")
    return _hazard_edge_probabilities(graph, zones, baseline=baseline)


def nfhl_zone_color(zone: str):
    """Deprecated. Use ``gravel.datasets.nfhl.zone_color``."""
    _warn("nfhl_zone_color", "gravel.datasets.nfhl.zone_color")
    return _nfhl.zone_color(zone)


# Deprecated constants, resolved lazily (PEP 562) so a bare `import gravel` — which
# imports this module — does not warn; only actual access to a deprecated name does.
_CONSTANT_ALIASES = {
    "NFHL_EVENT_CLOSURE": ("EVENT_CLOSURE", "gravel.datasets.nfhl.EVENT_CLOSURE"),
    "NFHL_ANNUAL_PROBABILITY": ("ANNUAL_PROBABILITY", "gravel.datasets.nfhl.ANNUAL_PROBABILITY"),
    "NFHL_ENDPOINT": ("ENDPOINT", "gravel.datasets.nfhl.ENDPOINT"),
    "NFHL_FLOOD_ZONE_LAYER": ("FLOOD_ZONE_LAYER", "gravel.datasets.nfhl.FLOOD_ZONE_LAYER"),
    "NFHL_ZONE_COLORS": ("ZONE_COLORS", "gravel.datasets.nfhl.ZONE_COLORS"),
    "NFHL_DEFAULT_ZONE_COLOR": ("_DEFAULT_ZONE_COLOR", "gravel.datasets.nfhl._DEFAULT_ZONE_COLOR"),
}


def __getattr__(name: str):  # PEP 562 module-level attribute hook
    alias = _CONSTANT_ALIASES.get(name)
    if alias is not None:
        new_attr, new_path = alias
        _warn(name, new_path)
        return getattr(_nfhl, new_attr)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
