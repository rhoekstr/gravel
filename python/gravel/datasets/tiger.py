"""US Census TIGER/Line boundary datasets (``gravel.datasets.tiger``).

Each function loads a TIGER/Line GeoJSON layer into a list of ``RegionSpec``
for node assignment. Bring-your-own file (download from the Census TIGER/Line
site; see ``gravel.datasets.info('tiger').source_url``).
"""

from __future__ import annotations

from .. import _gravel


def counties(geojson_path):
    """TIGER county boundaries (GEOID -> region_id, NAMELSAD -> label)."""
    return _gravel.load_tiger_counties(geojson_path)


def states(geojson_path):
    """TIGER state boundaries (STATEFP -> region_id, NAME -> label)."""
    return _gravel.load_tiger_states(geojson_path)


def cbsas(geojson_path):
    """TIGER CBSA (metro/micro) boundaries (CBSAFP -> region_id, NAME -> label)."""
    return _gravel.load_tiger_cbsas(geojson_path)


def places(geojson_path):
    """TIGER place (city/CDP) boundaries (GEOID -> region_id, NAMELSAD -> label)."""
    return _gravel.load_tiger_places(geojson_path)


def urban_areas(geojson_path):
    """TIGER urban-area boundaries (UACE10 -> region_id, NAME10 -> label)."""
    return _gravel.load_tiger_urban_areas(geojson_path)
