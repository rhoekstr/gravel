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
    # --- Snapping ---
    SnapQualityReport,
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
    load_tiger_cbsas,
    # --- TIGER loaders (US Census boundaries) ---
    load_tiger_counties,
    load_tiger_places,
    load_tiger_states,
    load_tiger_urban_areas,
    location_fragility,
    # --- Graph construction ---
    make_grid_graph,
    make_random_graph,
    make_tree_with_bridges,
    natural_connectivity,
    outgoing_edges,
    precompute_landmarks,
    progressive_fragility,
    route_fragility,
    save_elevation,
    # --- Region serialization ---
    save_region_assignment,
    scenario_fragility,
    seasonal_weight_multipliers,
    snap_quality,
    stratified_sample,
    summarize_border_edges,
    validate,
    validate_fragility,
    validate_shortcut_interaction,
)

# Conditional OSM imports (only available when built with GRAVEL_USE_OSMIUM=ON)
try:
    from ._gravel import (
        OSMConfig,
        SpeedProfile,
        load_osm_graph,
        load_osm_graph_with_labels,
    )
except ImportError:
    pass  # OSM support not compiled in

__version__ = "2.2.1"

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
    # County fragility
    "CountyFragilityConfig", "CountyFragilityResult", "county_fragility_index",
    # Location fragility
    "SelectionStrategy", "LocationFragilityConfig", "LocationKLevel",
    "LocationFragilityResult", "location_fragility",
    # Progressive fragility
    "ProgressiveFragilityConfig", "ProgressiveFragilityResult", "progressive_fragility",
    # Scenario fragility
    "ScenarioConfig", "ScenarioResult", "scenario_fragility", "edges_in_polygon",
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
    # TIGER loaders
    "load_tiger_counties", "load_tiger_states", "load_tiger_cbsas",
    "load_tiger_places", "load_tiger_urban_areas",
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
