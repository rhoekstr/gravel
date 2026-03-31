"""Gravel: Graph Routing and Vulnerability Analysis Library.

A high-performance C++ library for road network routing and edge importance
analysis, with first-class Python bindings.

Quick start::

    import gravel

    # Load or create a graph
    g = gravel.make_grid_graph(100, 100)

    # Build contraction hierarchy
    ch = gravel.build_ch(g)
    idx = gravel.ShortcutIndex(ch)
    q = gravel.CHQuery(ch)

    # Route
    result = q.route(0, 9999)
    print(f"Distance: {result.distance}, Path length: {len(result.path)}")

    # Route fragility
    frag = gravel.route_fragility(ch, idx, g, 0, 9999)
    print(f"Bottleneck ratio: {frag.bottleneck().fragility_ratio}")

    # Location fragility
    cfg = gravel.LocationFragilityConfig()
    cfg.center = gravel.Coord(35.398, -83.218)
    cfg.radius_meters = 80467  # 50 miles
    loc = gravel.location_fragility(g, ch, idx, cfg)
    print(f"Isolation risk: {loc.isolation_risk}")

Modules:
    Core: Graph, CH, CHQuery, RouteResult
    Fragility: route_fragility, batch_fragility, filtered_route_fragility
    Analysis: algebraic_connectivity, edge_betweenness, kirchhoff_index
    County: county_fragility_index
    Location: location_fragility
    Elevation: load_srtm_elevation, classify_closure_risk
    Snap: SnapQualityReport, snap_quality
"""

from _gravel import (
    # Core types
    Graph,
    CH,
    CHQuery,
    RouteResult,
    Coord,
    Polygon,

    # Graph construction
    make_grid_graph,
    make_random_graph,
    make_tree_with_bridges,

    # CH construction
    build_ch,
    build_ch_with_config,
    CHBuildConfig,

    # Routing
    dijkstra_pair,

    # Validation
    ValidationReport,
    validate,

    # Fragility
    EdgeFragility,
    FragilityResult,
    AlternateRouteResult,
    ShortcutIndex,
    route_fragility,
    batch_fragility,
    find_alternative_routes,
    hershberger_suri,
    bernstein_approx,
    ViaPathConfig,
    BernsteinConfig,
    FilterConfig,
    FilteredFragilityResult,
    filtered_route_fragility,

    # Fragility validation
    FragilityValidationReport,
    validate_fragility,
    validate_shortcut_interaction,

    # Network analysis
    SubgraphResult,
    extract_subgraph,
    algebraic_connectivity,
    BetweennessConfig,
    BetweennessResult,
    edge_betweenness,
    KirchhoffConfig,
    kirchhoff_index,
    natural_connectivity,
    BridgeResult,

    # County fragility
    CountyFragilityConfig,
    CountyFragilityResult,
    county_fragility_index,

    # Location fragility
    LocationFragilityConfig,
    LocationFragilityResult,
    location_fragility,

    # Landmarks
    LandmarkData,
    precompute_landmarks,

    # O-D sampling
    SamplingConfig,
    stratified_sample,

    # Snapping
    SnapQualityReport,
    snap_quality,

    # Elevation
    ElevationData,
    elevation_from_array,
    load_srtm_elevation,
    save_elevation,
    load_elevation,

    # Closure risk
    ClosureRiskTier,
    ClosureRiskData,
    classify_closure_risk,
    seasonal_weight_multipliers,
)

__version__ = "1.0.0"

__all__ = [
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
    # Network analysis
    "SubgraphResult", "extract_subgraph",
    "algebraic_connectivity", "BetweennessConfig", "BetweennessResult", "edge_betweenness",
    "KirchhoffConfig", "kirchhoff_index", "natural_connectivity", "BridgeResult",
    # County
    "CountyFragilityConfig", "CountyFragilityResult", "county_fragility_index",
    # Location
    "LocationFragilityConfig", "LocationFragilityResult", "location_fragility",
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
]
