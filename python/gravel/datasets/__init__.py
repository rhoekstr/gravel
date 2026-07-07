"""Dataset catalog and per-dataset access — the info-pull front door (2.6).

``gravel.datasets`` is the single entry point for every natively-supported
dataset:

    gravel.datasets.list()            # all catalog entries (list[Dataset])
    gravel.datasets.info("shakemap")  # one entry (KeyError if unknown)
    gravel.datasets.summary()         # prints + returns the feature matrix

    gravel.datasets.osm.load(pbf)               # road network from OSM
    gravel.datasets.tiger.counties(geojson)     # US Census boundaries

Each supported source also has a submodule (``osm``, ``tiger``, and the hazard
fetchers ``nfhl`` / ``shakemap`` / ``usdm`` / ``nri``) with a consistent
interface. The catalog metadata is authoritative in the C++ core; this layer
annotates runtime availability and renders it.
"""

from __future__ import annotations

import importlib.util
import json as _json

from .._gravel import (
    Access,
    Coverage,
    DatasetInfo,
    DatasetKind,
    Domain,
    Feature,
    Geometry,
    Temporal,
    dataset_catalog,
)

from . import (
    caida,
    gridsfm,
    gtfs,
    nfhl,
    nri,
    opfdata,
    openflights,
    osm,
    shakemap,
    tiger,
    usdm,
)

__all__ = [
    "Dataset",
    "list",
    "info",
    "summary",
    "caida",
    "gridsfm",
    "gtfs",
    "nfhl",
    "nri",
    "opfdata",
    "openflights",
    "osm",
    "shakemap",
    "tiger",
    "usdm",
    "Access",
    "Coverage",
    "DatasetInfo",
    "DatasetKind",
    "Domain",
    "Feature",
    "Geometry",
    "Temporal",
    "dataset_catalog",
]


def _is_available(entry: DatasetInfo) -> bool:
    """Whether the dependencies needed to actually use this dataset are present."""
    if entry.id == "osm":
        import gravel

        return bool(gravel.HAS_OSM)
    if entry.kind == DatasetKind.HAZARD_OVERLAY:
        # Hazard fetchers build GeoDataFrames; network loaders need only numpy.
        return importlib.util.find_spec("geopandas") is not None
    return True


class Dataset:
    """A catalog entry: what a dataset is, what it provides, and how to cite it.

    A thin wrapper over the C++ ``DatasetInfo`` that adds runtime availability
    and rendering (``feature_names``, ``to_dict``, ``to_json``). Descriptive
    facets (``kind``, ``domain``, ``geometry``, ``temporal``, ``coverage``,
    ``access``) are the bound enums; ``features`` and ``temporal`` are bitmasks.
    """

    __slots__ = ("_info",)

    def __init__(self, info: DatasetInfo):
        self._info = info

    # --- passthrough metadata ---
    @property
    def id(self) -> str:
        return self._info.id

    @property
    def name(self) -> str:
        return self._info.name

    @property
    def kind(self):
        return self._info.kind

    @property
    def domain(self):
        return self._info.domain

    @property
    def geometry(self):
        return self._info.geometry

    @property
    def temporal(self):
        return self._info.temporal

    @property
    def coverage(self):
        return self._info.coverage

    @property
    def features(self):
        return self._info.features

    @property
    def versioning(self) -> str:
        return self._info.versioning

    @property
    def source_url(self) -> str:
        return self._info.source_url

    @property
    def field_docs_url(self) -> str:
        return self._info.field_docs_url

    @property
    def license(self) -> str:
        return self._info.license

    @property
    def access(self):
        return self._info.access

    # --- derived ---
    @property
    def available(self) -> bool:
        """True when the dependencies to use this dataset are installed."""
        return _is_available(self._info)

    def has_feature(self, feature: Feature) -> bool:
        """Whether this dataset provides ``feature`` (a Feature flag)."""
        return bool(int(self._info.features) & int(feature))

    def feature_names(self) -> list[str]:
        """The names of the features this dataset provides."""
        bits = int(self._info.features)
        return [
            name
            for name, member in Feature.__members__.items()
            if member != Feature.NONE and bits & int(member)
        ]

    def temporal_names(self) -> list[str]:
        """The temporal classifications that apply (e.g. ['SNAPSHOT', 'HISTORICAL'])."""
        bits = int(self._info.temporal)
        return [
            name
            for name, member in Temporal.__members__.items()
            if member != Temporal.NONE and bits & int(member)
        ]

    def to_dict(self) -> dict:
        """A plain-dict record suitable for JSON or a methods section."""
        return {
            "id": self._info.id,
            "name": self._info.name,
            "kind": self._info.kind.name,
            "domain": self._info.domain.name,
            "geometry": self._info.geometry.name,
            "temporal": self.temporal_names(),
            "coverage": self._info.coverage.name,
            "features": self.feature_names(),
            "versioning": self._info.versioning,
            "source_url": self._info.source_url,
            "field_docs_url": self._info.field_docs_url,
            "license": self._info.license,
            "access": self._info.access.name,
            "available": self.available,
        }

    def to_json(self, **kwargs) -> str:
        """The record as a JSON string (``**kwargs`` forwarded to ``json.dumps``)."""
        return _json.dumps(self.to_dict(), **kwargs)

    def __repr__(self) -> str:
        return (
            f"Dataset(id={self._info.id!r}, kind={self._info.kind.name}, "
            f"available={self.available})"
        )


def list() -> list[Dataset]:  # noqa: A001 - public API is gravel.datasets.list()
    """Every dataset in the catalog, as ``Dataset`` objects."""
    return [Dataset(entry) for entry in dataset_catalog()]


def info(dataset_id: str) -> Dataset:
    """The catalog entry for ``dataset_id`` (raises ``KeyError`` if unknown)."""
    for entry in dataset_catalog():
        if entry.id == dataset_id:
            return Dataset(entry)
    known = [entry.id for entry in dataset_catalog()]
    raise KeyError(f"unknown dataset id {dataset_id!r}; known ids: {known}")


def summary(file=None) -> str:
    """Print (and return) the catalog as a compact feature matrix."""
    entries = [Dataset(entry) for entry in dataset_catalog()]
    header = (
        f"{'id':<10} {'kind':<17} {'domain':<15} {'geometry':<8} "
        f"{'temporal':<21} {'access':<8} {'avail':<5}  features"
    )
    lines = [header, "-" * len(header)]
    for d in entries:
        lines.append(
            f"{d.id:<10} {d.kind.name:<17} {d.domain.name:<15} {d.geometry.name:<8} "
            f"{'|'.join(d.temporal_names()):<21} {d.access.name:<8} "
            f"{('yes' if d.available else 'no'):<5}  {', '.join(d.feature_names()) or '-'}"
        )
    text = "\n".join(lines)
    print(text, file=file)
    return text
