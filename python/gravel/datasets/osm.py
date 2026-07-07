"""OpenStreetMap road-network dataset (``gravel.datasets.osm``).

Bring-your-own ``.pbf`` extract (e.g. from Geofabrik). Requires the extension
to be built with libosmium (``gravel.HAS_OSM``).
"""

from __future__ import annotations

from .. import _gravel


def _require_osm() -> None:
    if not hasattr(_gravel, "load_osm_graph"):
        raise RuntimeError(
            "OSM support is not compiled in (gravel.HAS_OSM is False). Install a "
            "wheel built with libosmium, or build with GRAVEL_USE_OSMIUM=ON."
        )


def load(pbf_path, speed_profile=None):
    """Load a road network from an OSM ``.pbf`` file into a Graph.

    ``speed_profile`` maps OSM highway class -> km/h; defaults to the car profile.
    Edge weight is travel time (seconds); node coordinates are populated.
    """
    _require_osm()
    profile = speed_profile if speed_profile is not None else _gravel.SpeedProfile.car()
    return _gravel.load_osm_graph(pbf_path, profile)


def load_with_metadata(pbf_path, speed_profile=None, bidirectional=True):
    """Load an OSM graph plus per-edge OSM tag metadata.

    Returns ``(Graph, EdgeMetadata)`` — the metadata carries highway/name/
    surface/bridge/tunnel/maxspeed/lanes/ref tags in CSR edge order.
    """
    _require_osm()
    profile = speed_profile if speed_profile is not None else _gravel.SpeedProfile.car()
    return _gravel.load_osm_graph_with_metadata(pbf_path, profile, bidirectional)
