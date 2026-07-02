"""Interoperability adapters between Gravel graphs and the geo-Python stack.

Convert Gravel's C++ ``Graph`` to and from :mod:`networkx` graphs and
:mod:`geopandas` ``GeoDataFrame`` objects. This lets you build or clean a
network with NetworkX's utility library, hand the heavy simulation loops to
Gravel's C++ core, and visualize results with the mature geo-Python tooling
(Folium, kepler.gl, matplotlib) by way of GeoPandas.

These adapters are optional and require extra dependencies::

    pip install gravel-fragility[interop]

Everything is lazy-imported, so importing :mod:`gravel.interop` itself is cheap
and never fails for a missing optional dependency — the error is raised only
when you call an adapter that needs the package.

Edge ordering
-------------
``Graph.to_coo()`` and :class:`gravel.EdgeMetadata` share one CSR edge order, so
the i-th metadata value lines up with the i-th edge returned by ``to_coo()``.
The adapters here rely on that alignment.

Geometry
--------
By default :func:`to_geodataframe` draws each edge as a straight segment between its
two nodes. Pass ``edge_geometry`` — a :class:`gravel.EdgeGeometry` produced by
``simplify_graph`` with ``emit_geometry`` set on its :class:`gravel.SimplificationConfig` —
to draw each edge along its real OSM way polyline instead.
"""

from __future__ import annotations

from typing import TYPE_CHECKING, Any

from ._gravel import Graph

if TYPE_CHECKING:  # pragma: no cover - typing only
    import geopandas as gpd
    import networkx as nx

__all__ = [
    "to_networkx",
    "from_networkx",
    "to_geodataframe",
    "from_geodataframe",
]


def _require(module: str, *, extra: str = "interop"):
    """Import an optional dependency, with an actionable error if it's missing."""
    try:
        return __import__(module)
    except ImportError as exc:  # pragma: no cover - exercised only without the dep
        raise ImportError(
            f"gravel.interop needs the optional dependency '{module}'. "
            f"Install the interop extras with:  pip install gravel-fragility[{extra}]"
        ) from exc


def _metadata_columns(metadata, edge_count: int) -> dict[str, list[str]]:
    """Pull non-empty tag arrays out of an EdgeMetadata, validating alignment."""
    if metadata is None:
        return {}
    columns: dict[str, list[str]] = {}
    for key in metadata.keys:
        values = metadata.get(key)
        if len(values) != edge_count:
            raise ValueError(
                f"metadata['{key}'] has {len(values)} entries but the graph has "
                f"{edge_count} edges — the metadata does not belong to this graph."
            )
        columns[key] = values
    return columns


def to_networkx(
    graph: Graph,
    *,
    metadata: Any | None = None,
    directed: bool = True,
    include_coordinates: bool = True,
) -> nx.Graph:
    """Convert a Gravel ``Graph`` to a NetworkX graph.

    Parameters
    ----------
    graph:
        The Gravel graph to convert.
    metadata:
        Optional :class:`gravel.EdgeMetadata` (e.g. from
        ``load_osm_graph_with_metadata``). Non-empty tag values are attached as
        edge attributes (``highway``, ``lanes``, ``maxspeed``, ...).
    directed:
        If ``True`` (default) return a :class:`networkx.DiGraph` preserving edge
        direction; if ``False`` return a :class:`networkx.Graph` (reciprocal
        edges collapse).
    include_coordinates:
        If ``True`` and the graph has coordinates, set ``lat``/``lon`` (and
        ``x``/``y`` = lon/lat for plotting) node attributes.

    Returns
    -------
    networkx.DiGraph or networkx.Graph
        Nodes are integers ``0 .. node_count-1``; edges carry a ``weight``
        attribute.
    """
    nx = _require("networkx")

    sources, targets, weights = graph.to_coo()
    edge_count = graph.edge_count
    columns = _metadata_columns(metadata, edge_count)

    g = nx.DiGraph() if directed else nx.Graph()
    g.add_nodes_from(range(graph.node_count))

    src = sources.tolist()
    tgt = targets.tolist()
    wts = weights.tolist()

    if columns:
        edges = []
        for i in range(edge_count):
            attrs = {"weight": wts[i]}
            for key, values in columns.items():
                val = values[i]
                if val:  # skip empty tag values to keep the graph light
                    attrs[key] = val
            edges.append((src[i], tgt[i], attrs))
        g.add_edges_from(edges)
    else:
        g.add_weighted_edges_from(zip(src, tgt, wts, strict=True))

    if include_coordinates and graph.has_coordinates:
        coords = graph.node_coordinates()
        attrs = {}
        for n in range(graph.node_count):
            lat = float(coords[n, 0])
            lon = float(coords[n, 1])
            attrs[n] = {"lat": lat, "lon": lon, "x": lon, "y": lat}
        nx.set_node_attributes(g, attrs)

    return g


def from_networkx(graph: nx.Graph, *, weight: str = "weight") -> Graph:
    """Convert a NetworkX graph to a Gravel ``Graph``.

    Nodes are relabeled to a dense ``0 .. n-1`` range following the graph's node
    iteration order. If every node carries ``lat``/``lon`` attributes, those are
    preserved as Gravel node coordinates.

    Parameters
    ----------
    graph:
        Any NetworkX graph. Undirected graphs become bidirectional Gravel edges.
    weight:
        Edge-attribute name to use as the Gravel edge weight. Edges missing it
        default to ``1.0``.

    Returns
    -------
    gravel.Graph
    """
    _require("networkx")
    np = _require("numpy")

    nodes = list(graph.nodes())
    index = {node: i for i, node in enumerate(nodes)}
    num_nodes = len(nodes)

    sources: list[int] = []
    targets: list[int] = []
    weights: list[float] = []
    directed = graph.is_directed()
    for u, v, data in graph.edges(data=True):
        w = float(data.get(weight, 1.0))
        iu, iv = index[u], index[v]
        sources.append(iu)
        targets.append(iv)
        weights.append(w)
        if not directed:
            sources.append(iv)
            targets.append(iu)
            weights.append(w)

    coords = None
    node_data = graph.nodes(data=True)
    if num_nodes and all("lat" in d and "lon" in d for _, d in node_data):
        coords = np.empty((num_nodes, 2), dtype="float64")
        for node, d in graph.nodes(data=True):
            coords[index[node], 0] = float(d["lat"])
            coords[index[node], 1] = float(d["lon"])

    return Graph.from_coo(
        num_nodes,
        np.asarray(sources, dtype="uint32"),
        np.asarray(targets, dtype="uint32"),
        np.asarray(weights, dtype="float64"),
        coords,
    )


def to_geodataframe(
    graph: Graph,
    *,
    metadata: Any | None = None,
    edge_values: dict[str, Any] | None = None,
    edge_geometry: Any | None = None,
    crs: str = "EPSG:4326",
) -> gpd.GeoDataFrame:
    """Convert a Gravel ``Graph`` to an edge ``GeoDataFrame`` of LineStrings.

    Each row is a directed edge with ``source``, ``target``, ``weight`` columns,
    any non-empty :class:`gravel.EdgeMetadata` tag columns, and any arrays passed
    via ``edge_values``. By default the geometry is a straight segment between the
    two nodes' coordinates; pass ``edge_geometry`` to draw the real road shape.

    Parameters
    ----------
    graph:
        A Gravel graph that has coordinates (e.g. loaded from OSM).
    metadata:
        Optional :class:`gravel.EdgeMetadata`; its tags become columns.
    edge_values:
        Optional mapping of ``column_name -> array-like`` (length ``edge_count``,
        in CSR order) to attach as columns — e.g. betweenness or fragility
        scores.
    edge_geometry:
        Optional :class:`gravel.EdgeGeometry` (from ``simplify_graph`` with ``emit_geometry``
        set on its ``SimplificationConfig``), CSR-aligned to ``graph``'s edges. When given,
        each edge is drawn along its true polyline instead of a straight chord.
    crs:
        Coordinate reference system for the result (default WGS84).

    Returns
    -------
    geopandas.GeoDataFrame
    """
    gpd = _require("geopandas")
    shapely = _require("shapely")
    np = _require("numpy")

    if not graph.has_coordinates:
        raise ValueError(
            "to_geodataframe requires node coordinates; this graph has none "
            "(build it from OSM or supply coords via Graph.from_coo)."
        )

    sources, targets, weights = graph.to_coo()
    edge_count = graph.edge_count
    coords = graph.node_coordinates()  # (N, 2) [lat, lon]
    lat = coords[:, 0]
    lon = coords[:, 1]

    # shapely/GeoJSON use (x=lon, y=lat).
    if edge_geometry is not None and not edge_geometry.empty:
        if edge_geometry.edge_count != edge_count:
            raise ValueError(
                f"edge_geometry describes {edge_geometry.edge_count} edges but the "
                f"graph has {edge_count} edges."
            )
        pts = edge_geometry.points            # (M, 2) [lat, lon]
        offs = edge_geometry.offsets          # (edge_count + 1,)
        xy = np.column_stack([pts[:, 1], pts[:, 0]])            # -> (x=lon, y=lat)
        indices = np.repeat(np.arange(edge_count), np.diff(offs))
        geometry = shapely.linestrings(xy, indices=indices)
    else:
        # Straight segment between endpoints (E, 2, 2).
        src_xy = np.stack([lon[sources], lat[sources]], axis=1)
        tgt_xy = np.stack([lon[targets], lat[targets]], axis=1)
        line_coords = np.stack([src_xy, tgt_xy], axis=1)
        geometry = shapely.linestrings(line_coords)

    data: dict[str, Any] = {
        "source": sources,
        "target": targets,
        "weight": weights,
    }
    for key, values in _metadata_columns(metadata, edge_count).items():
        data[key] = values
    if edge_values:
        for name, values in edge_values.items():
            arr = np.asarray(values)
            if arr.shape[0] != edge_count:
                raise ValueError(
                    f"edge_values['{name}'] has {arr.shape[0]} entries but the "
                    f"graph has {edge_count} edges."
                )
            data[name] = arr

    return gpd.GeoDataFrame(data, geometry=geometry, crs=crs)


def _haversine_meters(lat1, lon1, lat2, lon2):
    """Vectorized great-circle distance in meters."""
    np = _require("numpy")
    r = 6_371_000.0
    p1 = np.radians(lat1)
    p2 = np.radians(lat2)
    dphi = np.radians(lat2 - lat1)
    dlmb = np.radians(lon2 - lon1)
    a = np.sin(dphi / 2.0) ** 2 + np.cos(p1) * np.cos(p2) * np.sin(dlmb / 2.0) ** 2
    return 2.0 * r * np.arcsin(np.sqrt(a))


def from_geodataframe(
    gdf: gpd.GeoDataFrame,
    *,
    weight: str | None = None,
    directed: bool = False,
    precision: int = 7,
) -> Graph:
    """Build a Gravel ``Graph`` from a ``GeoDataFrame`` of LineStrings.

    Endpoints are snapped to shared nodes by rounding coordinates to ``precision``
    decimal places. Node ids are assigned in first-seen order, and node
    coordinates are preserved.

    Parameters
    ----------
    gdf:
        A GeoDataFrame whose geometry column holds ``LineString`` (or
        ``MultiLineString``) features. Non-line geometries are skipped.
    weight:
        Column to use as edge weight. If ``None``, the great-circle distance
        between endpoints (meters) is used.
    directed:
        If ``False`` (default) each line becomes a forward and reverse edge; if
        ``True`` only the drawn direction is kept.
    precision:
        Decimal places for coordinate snapping (~1 cm at 7 places).

    Returns
    -------
    gravel.Graph
    """
    _require("geopandas")
    np = _require("numpy")

    node_ids: dict[tuple[float, float], int] = {}
    node_coords: list[tuple[float, float]] = []  # (lat, lon) in node-id order

    def node_for(lon: float, lat: float) -> int:
        key = (round(lat, precision), round(lon, precision))
        nid = node_ids.get(key)
        if nid is None:
            nid = len(node_coords)
            node_ids[key] = nid
            node_coords.append((lat, lon))
        return nid

    sources: list[int] = []
    targets: list[int] = []
    weights: list[float] = []
    skipped = 0

    weight_values = gdf[weight].tolist() if weight is not None else None

    for row_i, geom in enumerate(gdf.geometry):
        if geom is None or geom.is_empty:
            skipped += 1
            continue
        gtype = geom.geom_type
        if gtype == "LineString":
            coords = list(geom.coords)
        elif gtype == "MultiLineString":
            coords = list(geom.geoms[0].coords) + list(geom.geoms[-1].coords)
        else:
            skipped += 1
            continue
        (lon_a, lat_a) = coords[0][0], coords[0][1]
        (lon_b, lat_b) = coords[-1][0], coords[-1][1]
        ia = node_for(lon_a, lat_a)
        ib = node_for(lon_b, lat_b)

        if weight_values is not None:
            w = float(weight_values[row_i])
        else:
            w = float(_haversine_meters(lat_a, lon_a, lat_b, lon_b))

        sources.append(ia)
        targets.append(ib)
        weights.append(w)
        if not directed:
            sources.append(ib)
            targets.append(ia)
            weights.append(w)

    if skipped:
        import warnings

        warnings.warn(
            f"from_geodataframe skipped {skipped} non-LineString/empty geometries.",
            stacklevel=2,
        )

    coords_arr = np.asarray(node_coords, dtype="float64").reshape(-1, 2)
    return Graph.from_coo(
        len(node_coords),
        np.asarray(sources, dtype="uint32"),
        np.asarray(targets, dtype="uint32"),
        np.asarray(weights, dtype="float64"),
        coords_arr,
    )
