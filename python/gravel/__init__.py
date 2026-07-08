"""Gravel: Graph Routing and Vulnerability Analysis Library.

A high-performance C++ library for road network routing and edge importance
analysis, with first-class Python bindings.

Quick start::

    import gravel

    # Load or create a graph
    g = gravel.make_grid_graph(100, 100)

    # Build contraction hierarchy
    ch = gravel.build_ch(g)

    # Route
    q = gravel.CHQuery(ch)
    result = q.route(0, 9999)
    print(f"Distance: {result.distance}, Path length: {len(result.path)}")

    # Location fragility (Dijkstra + IncrementalSSSP, ~2s on real data)
    cfg = gravel.LocationFragilityConfig()
    cfg.center = gravel.Coord(35.398, -83.218)
    cfg.radius_meters = 80467  # 50 miles
    loc = gravel.location_fragility(g, ch, cfg)
    print(f"Isolation risk: {loc.isolation_risk}")

    # Geographic analysis
    regions = gravel.load_regions_geojson("counties.geojson")
    assignment = gravel.assign_nodes_to_regions(g, regions)
    border = gravel.summarize_border_edges(g, assignment)
    coarsened = gravel.coarsen_graph(g, assignment, border)
"""

from ._gravel import (
    CH,
    # --- Export / interop + parallelism flags ---
    HAS_ARROW,
    HAS_OPENMP,
    AlternateRouteResult,
    AssignmentConfig,
    BernsteinConfig,
    BetweennessConfig,
    BetweennessResult,
    # --- Blocked CH query ---
    BlockedCHQuery,
    BorderEdgeResult,
    BorderEdgeSummary,
    BridgeResult,
    # --- Cascading failure (Motter-Lai) ---
    CascadeAlphaPoint,
    CascadeCapacity,
    CascadeFragilityConfig,
    CascadeFragilityResult,
    CHBuildConfig,
    CHQuery,
    ClosureRiskData,
    # --- Closure risk ---
    ClosureRiskTier,
    # --- Graph coarsening ---
    CoarseningConfig,
    CoarseningResult,
    Coord,
    # --- County fragility ---
    CountyFragilityConfig,
    CountyFragilityResult,
    # --- Fragility ---
    EdgeFragility,
    # --- Edge geometry (2B) ---
    EdgeGeometry,
    EdgeSampler,
    # --- Elevation ---
    ElevationData,
    FilterConfig,
    FilteredFragilityResult,
    FragilityResult,
    # --- Fragility validation ---
    FragilityValidationReport,
    GeoJSONLoadConfig,
    # --- Core types ---
    Graph,
    # --- Inter-region progressive fragility ---
    InterRegionFragilityConfig,
    InterRegionFragilityResult,
    InterRegionLevel,
    InterRegionPairResult,
    KirchhoffConfig,
    # --- Landmarks ---
    LandmarkData,
    LocationFragilityConfig,
    LocationFragilityResult,
    LocationKLevel,
    Polygon,
    # --- Progressive elimination fragility ---
    ProgressiveFragilityConfig,
    ProgressiveFragilityResult,
    ReducedGraph,
    # --- Reduced graph (region-aware graph reduction) ---
    ReducedGraphConfig,
    RegionAssignment,
    # --- Border edges ---
    RegionPair,
    # --- Region assignment & GeoJSON ---
    RegionSpec,
    RouteResult,
    SamplerConfig,
    # --- O-D sampling ---
    SamplingConfig,
    # --- Edge sampling ---
    SamplingStrategy,
    # --- Scenario fragility ---
    ScenarioConfig,
    ScenarioResult,
    # --- Location fragility ---
    SelectionStrategy,
    ShortcutIndex,
    # --- Graph simplification ---
    SimplificationConfig,
    SimplificationResult,
    # --- Snapping ---
    SnapQualityReport,
    # --- Stochastic fragility ---
    StochasticFragilityConfig,
    StochasticFragilityResult,
    StochasticTarget,
    # --- Network analysis ---
    SubgraphResult,
    # --- Validation ---
    ValidationReport,
    ViaPathConfig,
    algebraic_connectivity,
    assign_nodes_to_regions,
    batch_fragility,
    bernstein_approx,
    boundary_nodes,
    # --- CH construction ---
    build_ch,
    build_ch_with_config,
    build_reduced_geography_graph,  # geo adapter for RegionAssignment
    capacity_weighted_importance,
    cascade_fragility,
    cascade_vs_alpha,
    classify_closure_risk,
    coarsen_graph,
    county_fragility_index,
    # --- Routing ---
    dijkstra_pair,
    edge_betweenness,
    edges_in_polygon,
    elevation_from_array,
    extract_subgraph,
    filtered_route_fragility,
    find_alternative_routes,
    hershberger_suri,
    inter_region_fragility,
    kirchhoff_index,
    load_elevation,
    load_region_assignment,
    load_regions_geojson,
    load_srtm_elevation,
    location_fragility,
    location_fragility_to_geojson,
    # --- Graph construction ---
    make_grid_graph,
    make_random_graph,
    make_tree_with_bridges,
    max_threads,
    natural_connectivity,
    outgoing_edges,
    precompute_landmarks,
    progressive_fragility,
    route_fragility,
    route_to_geojson,
    save_elevation,
    # --- Region serialization ---
    save_region_assignment,
    scenario_fragility,
    seasonal_weight_multipliers,
    set_max_threads,
    simplify_graph,
    snap_quality,
    stochastic_fragility,
    stratified_sample,
    summarize_border_edges,
    validate,
    validate_fragility,
    validate_shortcut_interaction,
    write_fragility_jsonl,
)

# --- Dataset catalog / info-pull (2.6) ---
from ._gravel import (  # noqa: E402
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

# OSM loader availability depends on how the extension was built.
# PyPI wheels from v2.2.2+ ship with OSM enabled on every platform; older
# wheels and source builds without libosmium will have HAS_OSM = False.
# The supported runtime check is `gravel.HAS_OSM`:
#
#     import gravel
#     if gravel.HAS_OSM:
#         g = gravel.load_osm_graph(cfg)
try:
    from ._gravel import (
        CapacityConfig,
        EdgeMetadata,
        OSMConfig,
        SpeedProfile,
        estimate_capacity,
    )
    HAS_OSM = True
except ImportError:
    HAS_OSM = False

# Parquet writers exist only when the extension was built with Arrow
# (GRAVEL_USE_ARROW=ON). `gravel.HAS_ARROW` is the supported runtime check;
# the JSONL writer (`write_fragility_jsonl`) is always available.
if HAS_ARROW:
    from ._gravel import (
        write_betweenness_parquet,
        write_county_fragility_parquet,
        write_fragility_parquet,
    )

# Pure-Python submodules. All import cleanly without the optional geo deps, which
# are lazy-imported inside the functions that need them (`pip install
# gravel-fragility[interop]`):
#   interop  — NetworkX / GeoPandas adapters
#   hazards  — hazard footprints -> per-edge failure probabilities for stochastic_fragility
#   viz      — fragility results -> plot-ready GeoDataFrames (visualization data bridge)
from . import (  # noqa: E402
    datasets,
    hazards,
    interop,
    viz,
)

__version__ = "2.7.0"

__all__ = [
    # Feature flags
    "HAS_OSM",
    # Core
    "Graph", "CH", "CHQuery", "RouteResult", "Coord", "Polygon",
    "build_ch", "build_ch_with_config", "CHBuildConfig",
    "make_grid_graph", "make_random_graph", "make_tree_with_bridges",
    "dijkstra_pair",
    # Validation
    "ValidationReport", "validate",
    # Fragility
    "EdgeFragility", "FragilityResult", "AlternateRouteResult",
    "ShortcutIndex", "route_fragility", "batch_fragility",
    "find_alternative_routes", "hershberger_suri", "bernstein_approx",
    "ViaPathConfig", "BernsteinConfig",
    "FilterConfig", "FilteredFragilityResult", "filtered_route_fragility",
    "FragilityValidationReport", "validate_fragility", "validate_shortcut_interaction",
    # Graph simplification + edge geometry
    "SimplificationConfig", "SimplificationResult", "simplify_graph", "EdgeGeometry",
    # Network analysis
    "SubgraphResult", "extract_subgraph",
    "algebraic_connectivity", "BetweennessConfig", "BetweennessResult", "edge_betweenness",
    "capacity_weighted_importance",
    "KirchhoffConfig", "kirchhoff_index", "natural_connectivity", "BridgeResult",
    # County fragility
    "CountyFragilityConfig", "CountyFragilityResult", "county_fragility_index",
    # Location fragility
    "SelectionStrategy", "LocationFragilityConfig", "LocationKLevel",
    "LocationFragilityResult", "location_fragility",
    # Progressive fragility
    "ProgressiveFragilityConfig", "ProgressiveFragilityResult", "progressive_fragility",
    # Scenario fragility
    "ScenarioConfig", "ScenarioResult", "scenario_fragility", "edges_in_polygon",
    # Stochastic fragility
    "StochasticFragilityConfig", "StochasticFragilityResult", "StochasticTarget",
    "stochastic_fragility",
    # Cascading failure (Motter-Lai, experimental)
    "CascadeCapacity", "CascadeFragilityConfig", "CascadeFragilityResult",
    "CascadeAlphaPoint", "cascade_fragility", "cascade_vs_alpha",
    # Edge sampling
    "SamplingStrategy", "SamplerConfig", "EdgeSampler",
    # Region assignment
    "RegionSpec", "RegionAssignment", "AssignmentConfig", "GeoJSONLoadConfig",
    "assign_nodes_to_regions", "load_regions_geojson", "boundary_nodes",
    # Border edges
    "RegionPair", "BorderEdgeSummary", "BorderEdgeResult", "summarize_border_edges",
    # Graph coarsening
    "CoarseningConfig", "CoarseningResult", "coarsen_graph",
    # Region serialization
    "save_region_assignment", "load_region_assignment",
    # Geography skeleton + inter-geography fragility
    "ReducedGraphConfig", "ReducedGraph", "build_reduced_geography_graph",
    "InterRegionFragilityConfig", "InterRegionLevel", "InterRegionPairResult",
    "InterRegionFragilityResult", "inter_region_fragility",
    # (TIGER loaders + load_osm_graph* moved to gravel.datasets in 2.6; the deprecated
    #  top-level aliases still work via __getattr__ below, and are removed in 3.0.)
    # Landmarks + sampling
    "LandmarkData", "precompute_landmarks", "SamplingConfig", "stratified_sample",
    # Snap
    "SnapQualityReport", "snap_quality",
    # Elevation
    "ElevationData", "elevation_from_array", "load_srtm_elevation",
    "save_elevation", "load_elevation",
    # Closure risk
    "ClosureRiskTier", "ClosureRiskData", "classify_closure_risk",
    "seasonal_weight_multipliers",
    # Export / interop / hazards / viz
    "HAS_ARROW", "route_to_geojson", "location_fragility_to_geojson",
    "write_fragility_jsonl", "interop", "hazards", "viz", "datasets",
    # Dataset catalog / info-pull (2.6)
    "DatasetKind", "Domain", "Geometry", "Temporal", "Coverage", "Access", "Feature",
    "DatasetInfo", "dataset_catalog",
    # Parallelism
    "HAS_OPENMP", "max_threads", "set_max_threads",
]


# --- Deprecated top-level loaders (2.6) ------------------------------------------
# Relocated under gravel.datasets; kept working as warning-emitting aliases and
# removed in 3.0. Resolved lazily (PEP 562) so a bare `import gravel` never warns —
# only actual access to a deprecated name does.
_DEPRECATED_LOADERS = {
    "load_osm_graph": ("osm", "load"),
    "load_osm_graph_with_metadata": ("osm", "load_with_metadata"),
    "load_tiger_counties": ("tiger", "counties"),
    "load_tiger_states": ("tiger", "states"),
    "load_tiger_cbsas": ("tiger", "cbsas"),
    "load_tiger_places": ("tiger", "places"),
    "load_tiger_urban_areas": ("tiger", "urban_areas"),
}


def __getattr__(name):  # PEP 562 module-level attribute hook
    target = _DEPRECATED_LOADERS.get(name)
    if target is not None:
        import warnings

        submodule, func = target
        warnings.warn(
            f"gravel.{name} is deprecated; use gravel.datasets.{submodule}.{func} "
            "(removed in Gravel 3.0).",
            DeprecationWarning,
            stacklevel=2,
        )
        return getattr(getattr(datasets, submodule), func)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
