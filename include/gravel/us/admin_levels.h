/// @file admin_levels.h
/// @brief US administrative geography constants and enums.
///
/// TIGER property name constants and administrative level definitions
/// for use with the TIGER loader and US-specific analysis.

#pragma once

#include <string>

namespace gravel {

/// US administrative hierarchy levels.
enum class USAdminLevel {
    STATE,          ///< State/territory (STATEFP)
    COUNTY,         ///< County/equivalent (GEOID = STATEFP + COUNTYFP)
    PLACE,          ///< Incorporated place / CDP
    CBSA,           ///< Core Based Statistical Area (MSA/uSA)
    URBAN_AREA,     ///< Census Urban Area
};

/// TIGER/Line 2023 property name constants.
namespace tiger {
    // Counties
    inline constexpr const char* COUNTY_ID = "GEOID";       // 5-digit FIPS
    inline constexpr const char* COUNTY_LABEL = "NAMELSAD";
    inline constexpr const char* STATE_FP = "STATEFP";       // 2-digit state FIPS

    // States
    inline constexpr const char* STATE_ID = "STATEFP";
    inline constexpr const char* STATE_LABEL = "NAME";

    // CBSAs
    inline constexpr const char* CBSA_ID = "CBSAFP";
    inline constexpr const char* CBSA_LABEL = "NAME";

    // Places
    inline constexpr const char* PLACE_ID = "GEOID";        // 7-digit
    inline constexpr const char* PLACE_LABEL = "NAMELSAD";

    // Urban Areas
    inline constexpr const char* UA_ID = "UACE10";
    inline constexpr const char* UA_LABEL = "NAME10";
}  // namespace tiger

}  // namespace gravel
