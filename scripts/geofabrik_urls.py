"""Geofabrik download URLs for US state/territory OSM PBF extracts."""

BASE = "https://download.geofabrik.de/north-america/us"

# State FIPS -> Geofabrik slug
STATE_FIPS_TO_SLUG = {
    "01": "alabama", "02": "alaska", "04": "arizona", "05": "arkansas",
    "06": "california", "08": "colorado", "09": "connecticut", "10": "delaware",
    "11": "district-of-columbia", "12": "florida", "13": "georgia", "15": "hawaii",
    "16": "idaho", "17": "illinois", "18": "indiana", "19": "iowa",
    "20": "kansas", "21": "kentucky", "22": "louisiana", "23": "maine",
    "24": "maryland", "25": "massachusetts", "26": "michigan", "27": "minnesota",
    "28": "mississippi", "29": "missouri", "30": "montana", "31": "nebraska",
    "32": "nevada", "33": "new-hampshire", "34": "new-jersey", "35": "new-mexico",
    "36": "new-york", "37": "north-carolina", "38": "north-dakota", "39": "ohio",
    "40": "oklahoma", "41": "oregon", "42": "pennsylvania", "44": "rhode-island",
    "45": "south-carolina", "46": "south-dakota", "47": "tennessee", "48": "texas",
    "49": "utah", "50": "vermont", "51": "virginia", "53": "washington",
    "54": "west-virginia", "55": "wisconsin", "56": "wyoming",
}

# Puerto Rico is under a different Geofabrik path
TERRITORY_URLS = {
    "72": "https://download.geofabrik.de/north-america/us/puerto-rico-latest.osm.pbf",
}


def get_pbf_url(state_fips: str) -> str:
    """Get the Geofabrik PBF download URL for a state FIPS code."""
    if state_fips in TERRITORY_URLS:
        return TERRITORY_URLS[state_fips]
    slug = STATE_FIPS_TO_SLUG.get(state_fips)
    if not slug:
        raise ValueError(f"Unknown state FIPS: {state_fips}")
    return f"{BASE}/{slug}-latest.osm.pbf"
