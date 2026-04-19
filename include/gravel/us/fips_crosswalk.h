/// @file fips_crosswalk.h
/// @brief FIPS code crosswalk — county to CBSA and state lookups.
///
/// Provides in-memory lookup tables mapping county FIPS codes to their
/// containing CBSA and state. Built from overlapping RegionAssignments.

#pragma once

#include "gravel/geo/region_assignment.h"
#include "gravel/core/geo_math.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

namespace gravel {

/// Crosswalk entry for one county.
struct FIPSEntry {
    std::string county_fips;    ///< 5-digit FIPS (e.g., "37173")
    std::string county_name;    ///< "Swain County"
    std::string state_fips;     ///< 2-digit (e.g., "37")
    std::string cbsa_code;      ///< CBSA code, or empty if not in a CBSA
    std::string cbsa_name;      ///< CBSA name, or empty
};

/// FIPS crosswalk table.
struct FIPSCrosswalk {
    /// All entries, one per county in the assignment.
    std::vector<FIPSEntry> entries;

    /// Lookup by county FIPS code.
    std::unordered_map<std::string, size_t> by_county_fips;

    /// Lookup CBSA for a county FIPS. Returns empty string if not in a CBSA.
    const std::string& county_to_cbsa(const std::string& county_fips) const {
        static const std::string empty;
        auto it = by_county_fips.find(county_fips);
        return (it != by_county_fips.end()) ? entries[it->second].cbsa_code : empty;
    }

    /// Get state FIPS for a county.
    const std::string& county_to_state(const std::string& county_fips) const {
        static const std::string empty;
        auto it = by_county_fips.find(county_fips);
        return (it != by_county_fips.end()) ? entries[it->second].state_fips : empty;
    }

    /// Get all county FIPS codes in a given CBSA.
    std::vector<std::string> counties_in_cbsa(const std::string& cbsa_code) const {
        std::vector<std::string> result;
        for (const auto& entry : entries) {
            if (entry.cbsa_code == cbsa_code) {
                result.push_back(entry.county_fips);
            }
        }
        return result;
    }

    /// Get all county FIPS codes in a given state.
    std::vector<std::string> counties_in_state(const std::string& state_fips) const {
        std::vector<std::string> result;
        for (const auto& entry : entries) {
            if (entry.state_fips == state_fips) {
                result.push_back(entry.county_fips);
            }
        }
        return result;
    }
};

/// Build a FIPS crosswalk from county and CBSA region assignments.
///
/// The county assignment provides county FIPS → state FIPS mapping (first 2 digits).
/// The CBSA assignment (optional) provides county → CBSA mapping by checking which
/// CBSA polygon contains each county's centroid.
///
/// @param county_assignment  RegionAssignment from TIGER county boundaries
/// @param cbsa_assignment    Optional RegionAssignment from TIGER CBSA boundaries.
///                           If null, CBSA fields will be empty.
inline FIPSCrosswalk build_fips_crosswalk(
    const RegionAssignment& county_assignment,
    const RegionAssignment* cbsa_assignment = nullptr) {

    FIPSCrosswalk xwalk;

    for (size_t i = 0; i < county_assignment.regions.size(); ++i) {
        FIPSEntry entry;
        entry.county_fips = county_assignment.regions[i].region_id;
        entry.county_name = county_assignment.regions[i].label;
        entry.state_fips = (entry.county_fips.size() >= 2) ?
            entry.county_fips.substr(0, 2) : "";

        // Find CBSA by checking if county centroid falls in a CBSA polygon
        if (cbsa_assignment && !county_assignment.regions[i].boundary.vertices.empty()) {
            // Use first vertex as proxy for centroid (good enough for crosswalk)
            Coord centroid = county_assignment.regions[i].boundary.vertices[0];
            for (size_t c = 0; c < cbsa_assignment->regions.size(); ++c) {
                if (point_in_polygon(centroid, cbsa_assignment->regions[c].boundary.vertices)) {
                    entry.cbsa_code = cbsa_assignment->regions[c].region_id;
                    entry.cbsa_name = cbsa_assignment->regions[c].label;
                    break;
                }
            }
        }

        xwalk.by_county_fips[entry.county_fips] = xwalk.entries.size();
        xwalk.entries.push_back(std::move(entry));
    }

    return xwalk;
}

}  // namespace gravel
