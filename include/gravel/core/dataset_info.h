#pragma once
/// @file dataset_info.h
/// @brief Catalog metadata describing a natively-supported dataset (2.6 info-pull).
///
/// `DatasetInfo` is the pure-data record behind the dataset info-pull
/// (`gravel.datasets.list()` / `.info(id)` in Python). It says what a supported
/// dataset *is* — its role in the pipeline, the per-edge/per-node features it
/// contributes, where it comes from, and how its versions are addressed — so a
/// researcher can tell, without reading source, whether a source yields a graph,
/// a set of boundaries, or a hazard footprint, and what fields come with it.
///
/// Pure data, with no dependency on the graph or on any serialization library:
/// JSON and human-readable rendering live in the Python layer, keeping
/// `gravel-core` free of nlohmann/json (which is confined to `gravel-geo` /
/// `gravel-fragility`). It lives in `gravel-core` so every higher sub-library —
/// `gravel-geo` (OSM, hazard overlays), `gravel-us` (TIGER) — can describe its
/// own datasets through the same record without crossing the sub-library DAG.
/// Each module exposes an `available_datasets()` returning
/// `std::vector<DatasetInfo>`; the Python surface aggregates them and annotates
/// runtime availability (build flags plus optional-dependency checks).
///
/// The descriptive facets are enums, not free strings — a closed, compile-checked
/// vocabulary that a typo cannot slip past and that binds directly to Python.

#include <cstdint>
#include <string>

namespace gravel {

/// The role a dataset plays in the fragility pipeline.
enum class DatasetKind {
    NETWORK,           ///< A graph substrate to analyze (OSM, GridSFM, GTFS, CSV, synthetic).
    BOUNDARY,          ///< Region polygons for partitioning / node assignment (TIGER).
    HAZARD_OVERLAY,    ///< A geographic footprint joined by point-in-polygon to a per-edge failure probability (NFHL, ShakeMap).
    ATTRIBUTE_OVERLAY, ///< A tabular per-edge attribute join, e.g. capacity (BTS T-100, GTFS schedule).
};

/// The domain a dataset describes. Spans network/infrastructure domains and
/// hazard domains; grows deliberately as sources are added.
enum class Domain {
    GENERIC,        ///< Unclassified graph source (CSV, synthetic).
    ADMINISTRATIVE, ///< Administrative boundaries (TIGER).
    ROAD,           ///< Road network.
    POWER,          ///< Electric transmission grid.
    INTERNET,       ///< Router-level internet topology.
    AIR,            ///< Air-transport network.
    TRANSIT,        ///< Public-transit network.
    FLOOD,          ///< Flood hazard.
    WILDFIRE,       ///< Wildfire hazard.
    EARTHQUAKE,     ///< Seismic hazard.
    HURRICANE,      ///< Tropical-cyclone hazard.
    TORNADO,        ///< Tornado / severe-wind hazard.
    DROUGHT,        ///< Drought hazard.
    MULTI_HAZARD,   ///< Composite across multiple hazards (FEMA NRI).
};

/// Footprint geometry of a hazard overlay, or the coordinate content a source
/// carries. `NONE` = no geometry at all (a plain edge list, an attribute table).
enum class Geometry {
    NONE,
    POINT,
    LINE,
    POLYGON,
    RASTER,
};

/// How a dataset relates to time, as a combinable bitmask — a source can be more
/// than one (ShakeMap and GTFS are both `SNAPSHOT` and `HISTORICAL`). There is
/// deliberately no "realtime": Gravel pulls a fixed capture, it does not stream
/// or interpret live-updating feeds, so what you retrieve is always a snapshot as
/// of the pull time (recorded in the fetch provenance's `pulled_at`).
enum class Temporal : uint32_t {
    NONE       = 0,
    SNAPSHOT   = 1u << 0,  ///< Retrievable as its current state, captured as of the pull time.
    HISTORICAL = 1u << 1,  ///< A dated archive of past events/states, addressable by date or id.
    ANNUALIZED = 1u << 2,  ///< Annualized composite values, not tied to an instant (FEMA NRI).
};

/// Bitwise union of two temporal classifications.
constexpr Temporal operator|(Temporal a, Temporal b) {
    return static_cast<Temporal>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/// Bitwise intersection of two temporal classifications.
constexpr Temporal operator&(Temporal a, Temporal b) {
    return static_cast<Temporal>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/// True when `set` contains every bit of `query`.
constexpr bool has_temporal(Temporal set, Temporal query) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(query)) ==
           static_cast<uint32_t>(query);
}

/// Spatial extent of a dataset.
enum class Coverage {
    NONE,   ///< Not geographically situated (synthetic test graphs).
    US,     ///< United States (and territories where applicable).
    GLOBAL, ///< Worldwide.
};

/// How a dataset is obtained. Derived from the integration-tier rule: a source
/// earns a `FETCHER` only with a stable version axis *and* a programmatic
/// endpoint; otherwise it is `BYO`; tiny reference graphs ship `BUNDLED`.
enum class Access {
    FETCHER, ///< An auto-fetcher exists (resolve + fetch).
    BYO,     ///< Bring-your-own: catalog + field-docs pointer, user supplies the file (e.g. AUP-gated CAIDA).
    BUNDLED, ///< Ships with Gravel as a fixture (e.g. a tiny example graph, inf-power).
};

/// Per-edge / per-node features a dataset contributes, as a combinable bitmask.
/// Answers "what will I actually get?" programmatically and lets an analysis
/// precondition on its inputs (e.g. a capacity-aware run requires `CAPACITY`).
enum class Feature : uint32_t {
    NONE          = 0,
    NODE_COORDS   = 1u << 0,  ///< Node latitude / longitude.
    EDGE_GEOMETRY = 1u << 1,  ///< True per-edge polyline (not just endpoints).
    CAPACITY      = 1u << 2,  ///< Per-edge throughput / capacity.
    LANES         = 1u << 3,  ///< Lane count.
    SPEED         = 1u << 4,  ///< Speed / travel-time attribute.
    ROAD_CLASS    = 1u << 5,  ///< Functional classification (highway class, line type).
    ONEWAY        = 1u << 6,  ///< Directionality.
    SEVERITY      = 1u << 7,  ///< Hazard intensity gradient (vs. boundary-only extent).
    HAZARD_PROB   = 1u << 8,  ///< Yields a per-edge failure probability.
};

/// Bitwise union of two feature sets.
constexpr Feature operator|(Feature a, Feature b) {
    return static_cast<Feature>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

/// Bitwise intersection of two feature sets.
constexpr Feature operator&(Feature a, Feature b) {
    return static_cast<Feature>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/// True when `set` contains every bit of `query`.
constexpr bool has_feature(Feature set, Feature query) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(query)) ==
           static_cast<uint32_t>(query);
}

/// A catalog entry describing one natively-supported dataset. Facets are enums;
/// only genuinely free-form fields (id, name, URLs, license, version axis) are
/// strings. The authoritative field dictionary is *pointed to* by
/// `field_docs_url`, never reproduced here — the source's dictionary stays
/// canonical and cannot go stale in a Gravel copy.
struct DatasetInfo {
    std::string id;             ///< Stable slug used to fetch / resolve (e.g. "nfhl", "shakemap", "osm").
    std::string name;           ///< Human-readable name (e.g. "USGS ShakeMap / ComCat").
    DatasetKind kind;           ///< Role in the pipeline.
    Domain domain;              ///< Domain described.
    Feature features;           ///< Features contributed (bitmask; `Feature::NONE` if none).
    Geometry geometry;          ///< Footprint / coordinate geometry.
    Temporal temporal;          ///< Relationship to time (bitmask).
    Coverage coverage;          ///< Spatial extent.
    std::string versioning;     ///< Free-form label for the resolver's version axis (e.g. "event_id+version", "weekly_date", "vintage").
    std::string source_url;     ///< Landing page for the source.
    std::string field_docs_url; ///< Authoritative data dictionary (a pointer, not ingested content).
    std::string license;        ///< e.g. "public domain (US federal)".
    Access access;              ///< How the dataset is obtained.
};

}  // namespace gravel
