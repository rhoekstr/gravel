/// @file gravel.h
/// @brief Gravel: Graph Routing and Vulnerability Analysis Library
///
/// Gravel is a high-performance C++ library for road network routing and
/// edge importance analysis. Its core function is answering: what happens
/// to network connectivity and travel time when specific edges are removed?
///
/// @section usage Quick Start
///
/// @code
/// #include <gravel/gravel.h>
///
/// // Load graph and build CH
/// auto graph = gravel::load_graph("network.gravel.meta");
/// auto ch = gravel::build_ch(*graph);
/// gravel::ShortcutIndex idx(ch);
///
/// // Route fragility: which edges are critical?
/// auto frag = gravel::route_fragility(ch, idx, *graph, source, target);
///
/// // Location fragility: is this location isolated?
/// gravel::LocationFragilityConfig loc_cfg;
/// loc_cfg.center = {35.398, -83.218};
/// loc_cfg.radius_meters = 80467;  // 50 miles
/// auto loc = gravel::location_fragility(*graph, ch, loc_cfg);
///
/// // County fragility index
/// gravel::CountyFragilityConfig county_cfg;
/// county_cfg.boundary = my_polygon;
/// auto county = gravel::county_fragility_index(*graph, ch, idx, county_cfg);
/// @endcode
///
/// @section modules Module Overview
///
/// - **Core** (`core/`): ArrayGraph, CSVGraph, OSMGraph, serialization
/// - **CH** (`ch/`): Contraction hierarchy build (sequential + parallel), query, serialization
/// - **Fragility** (`fragility/`): Route fragility, bridges, blocked CH query, via-path alternatives
/// - **Analysis** (`analysis/`): Spectral metrics, betweenness, county/location fragility
/// - **Geo** (`geo/`): Haversine distance, coordinate snapping, elevation, closure risk
/// - **Algo** (`algo/`): Dijkstra, priority queues, landmarks
/// - **IO** (`io/`): Binary serialization, Arrow/Parquet output, GeoJSON
/// - **Snap** (`snap/`): R-tree spatial index, nearest-edge snapping
/// - **Validation** (`validation/`): CH correctness, fragility validation, synthetic graphs

#pragma once

// Core
#include "gravel/core/types.h"
#include "gravel/core/array_graph.h"
#include "gravel/core/graph_serialization.h"

// CH
#include "gravel/ch/contraction.h"
#include "gravel/ch/ch_query.h"
#include "gravel/ch/ch_serialization.h"

// Fragility
#include "gravel/fragility/fragility_result.h"
#include "gravel/fragility/route_fragility.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/simplify/bridges.h"
#include "gravel/fragility/fragility_filter.h"

// Analysis
#include "gravel/analysis/algebraic_connectivity.h"
#include "gravel/analysis/betweenness.h"
#include "gravel/analysis/kirchhoff.h"
#include "gravel/analysis/natural_connectivity.h"
#include "gravel/analysis/county_fragility.h"
#include "gravel/analysis/location_fragility.h"
#include "gravel/analysis/closure_risk.h"

// Geo
#include "gravel/core/geo_math.h"
#include "gravel/geo/elevation.h"

// Snap
#include "gravel/snap/snapper.h"

// IO
#include "gravel/io/arrow_output.h"
#include "gravel/io/geojson_output.h"
