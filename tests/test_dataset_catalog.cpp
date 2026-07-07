#include <catch2/catch_test_macros.hpp>

#include "gravel/datasets/catalog.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace gravel;

namespace {
const DatasetInfo* find(const std::vector<DatasetInfo>& cat, const std::string& id) {
    auto it = std::find_if(cat.begin(), cat.end(),
                           [&](const DatasetInfo& d) { return d.id == id; });
    return it == cat.end() ? nullptr : &*it;
}
}  // namespace

TEST_CASE("dataset_catalog lists the 2.6 datasets", "[datasets]") {
    const auto cat = dataset_catalog();
    REQUIRE(cat.size() == 11);
    for (const char* id : {"osm", "tiger", "nfhl", "shakemap", "usdm", "nri",
                           "gridsfm", "opfdata", "caida", "openflights", "gtfs"}) {
        REQUIRE(find(cat, id) != nullptr);
    }
    // every entry has an id, name, source, and license (metadata is complete).
    for (const auto& d : cat) {
        REQUIRE_FALSE(d.id.empty());
        REQUIRE_FALSE(d.name.empty());
        REQUIRE_FALSE(d.source_url.empty());
        REQUIRE_FALSE(d.license.empty());
    }
}

TEST_CASE("dataset kinds, feature and temporal bitmasks", "[datasets]") {
    const auto cat = dataset_catalog();

    const DatasetInfo* osm = find(cat, "osm");
    REQUIRE(osm != nullptr);
    REQUIRE(osm->kind == DatasetKind::NETWORK);
    REQUIRE(osm->domain == Domain::ROAD);
    REQUIRE(has_feature(osm->features, Feature::CAPACITY));
    REQUIRE(has_feature(osm->features, Feature::NODE_COORDS | Feature::LANES));
    REQUIRE_FALSE(has_feature(osm->features, Feature::SEVERITY));

    const DatasetInfo* shakemap = find(cat, "shakemap");
    REQUIRE(shakemap != nullptr);
    REQUIRE(shakemap->kind == DatasetKind::HAZARD_OVERLAY);
    REQUIRE(shakemap->access == Access::FETCHER);
    // ShakeMap is both a current-state snapshot and a historical archive.
    REQUIRE(has_temporal(shakemap->temporal, Temporal::SNAPSHOT | Temporal::HISTORICAL));

    const DatasetInfo* nri = find(cat, "nri");
    REQUIRE(nri != nullptr);
    REQUIRE(has_temporal(nri->temporal, Temporal::ANNUALIZED));
    REQUIRE_FALSE(has_temporal(nri->temporal, Temporal::HISTORICAL));

    const DatasetInfo* tiger = find(cat, "tiger");
    REQUIRE(tiger != nullptr);
    REQUIRE(tiger->kind == DatasetKind::BOUNDARY);
    REQUIRE(tiger->access == Access::BYO);
    REQUIRE(tiger->features == Feature::NONE);

    // A 2.7 network substrate.
    const DatasetInfo* gridsfm = find(cat, "gridsfm");
    REQUIRE(gridsfm != nullptr);
    REQUIRE(gridsfm->kind == DatasetKind::NETWORK);
    REQUIRE(gridsfm->domain == Domain::POWER);
    REQUIRE(has_feature(gridsfm->features, Feature::CAPACITY));
}
