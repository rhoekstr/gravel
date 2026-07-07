#include "gravel/datasets/catalog.h"

namespace gravel {

// The 2.6 catalog. Entries are ordered by kind (substrate → boundary → hazard
// overlay). `field_docs_url` points at each source's authoritative data
// dictionary; the URLs are canonical landing/spec pages and should be
// re-verified as part of the release checklist. Hazard overlays reuse the
// shipped multi-zone `edges_in_polygon` kernel; their Python fetchers land
// alongside (NFHL shipped; ShakeMap / USDM / NRI in this cycle).
std::vector<DatasetInfo> dataset_catalog() {
    return {
        // --- Graph substrates ---
        DatasetInfo{
            .id = "osm",
            .name = "OpenStreetMap (road network)",
            .kind = DatasetKind::NETWORK,
            .domain = Domain::ROAD,
            .features = Feature::NODE_COORDS | Feature::EDGE_GEOMETRY |
                        Feature::CAPACITY | Feature::LANES | Feature::SPEED |
                        Feature::ROAD_CLASS | Feature::ONEWAY,
            .geometry = Geometry::LINE,
            .temporal = Temporal::SNAPSHOT,
            .coverage = Coverage::GLOBAL,
            .versioning = "extract_date",
            .source_url = "https://www.openstreetmap.org/",
            .field_docs_url = "https://wiki.openstreetmap.org/wiki/Map_features",
            .license = "ODbL 1.0",
            .access = Access::BYO,
        },
        // --- Boundaries ---
        DatasetInfo{
            .id = "tiger",
            .name = "US Census TIGER/Line boundaries",
            .kind = DatasetKind::BOUNDARY,
            .domain = Domain::ADMINISTRATIVE,
            .features = Feature::NONE,
            .geometry = Geometry::POLYGON,
            .temporal = Temporal::SNAPSHOT,
            .coverage = Coverage::US,
            .versioning = "vintage_year",
            .source_url = "https://www.census.gov/geographies/mapping-files/time-series/geo/tiger-line-file.html",
            .field_docs_url = "https://www.census.gov/programs-surveys/geography/technical-documentation/complete-technical-documentation/tiger-geo-line.html",
            .license = "public domain (US federal)",
            .access = Access::BYO,
        },
        // --- Hazard overlays ---
        DatasetInfo{
            .id = "nfhl",
            .name = "FEMA National Flood Hazard Layer",
            .kind = DatasetKind::HAZARD_OVERLAY,
            .domain = Domain::FLOOD,
            .features = Feature::SEVERITY | Feature::HAZARD_PROB,
            .geometry = Geometry::POLYGON,
            .temporal = Temporal::SNAPSHOT,
            .coverage = Coverage::US,
            .versioning = "effective_date",
            .source_url = "https://www.fema.gov/flood-maps/national-flood-hazard-layer",
            .field_docs_url = "https://www.fema.gov/about/glossary/flood-zones",
            .license = "public domain (US federal)",
            .access = Access::FETCHER,
        },
        DatasetInfo{
            .id = "shakemap",
            .name = "USGS ShakeMap / ComCat",
            .kind = DatasetKind::HAZARD_OVERLAY,
            .domain = Domain::EARTHQUAKE,
            .features = Feature::SEVERITY | Feature::HAZARD_PROB,
            .geometry = Geometry::POLYGON,
            .temporal = Temporal::SNAPSHOT | Temporal::HISTORICAL,
            .coverage = Coverage::GLOBAL,
            .versioning = "event_id+version",
            .source_url = "https://earthquake.usgs.gov/data/shakemap/",
            .field_docs_url = "https://usgs.github.io/shakemap/",
            .license = "public domain (US federal)",
            .access = Access::FETCHER,
        },
        DatasetInfo{
            .id = "usdm",
            .name = "US Drought Monitor",
            .kind = DatasetKind::HAZARD_OVERLAY,
            .domain = Domain::DROUGHT,
            .features = Feature::SEVERITY | Feature::HAZARD_PROB,
            .geometry = Geometry::POLYGON,
            .temporal = Temporal::SNAPSHOT | Temporal::HISTORICAL,
            .coverage = Coverage::US,
            .versioning = "weekly_date",
            .source_url = "https://droughtmonitor.unl.edu/",
            .field_docs_url = "https://droughtmonitor.unl.edu/About/AbouttheData/DroughtClassification.aspx",
            .license = "public domain (US gov / NDMC)",
            .access = Access::FETCHER,
        },
        DatasetInfo{
            .id = "nri",
            .name = "FEMA National Risk Index",
            .kind = DatasetKind::HAZARD_OVERLAY,
            .domain = Domain::MULTI_HAZARD,
            .features = Feature::SEVERITY | Feature::HAZARD_PROB,
            .geometry = Geometry::POLYGON,
            .temporal = Temporal::ANNUALIZED,
            .coverage = Coverage::US,
            .versioning = "release_version",
            .source_url = "https://hazards.fema.gov/nri/",
            .field_docs_url = "https://hazards.fema.gov/nri/data-resources",
            .license = "public domain (US federal)",
            .access = Access::FETCHER,
        },
    };
}

}  // namespace gravel
