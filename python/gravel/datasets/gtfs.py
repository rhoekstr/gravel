"""GTFS Schedule static transit feed (``gravel.datasets.gtfs``).

Load a GTFS static feed (a directory of extracted ``.txt`` files) into a transit
network: stops become nodes (lat/lon), consecutive stop-time hops become edges,
and each edge carries a schedule-derived **capacity** = a persons/hour throughput
proxy (per-hop vehicle frequency × a per-mode vehicle-capacity assumption). The
capacity model is disclosed and adjustable; GTFS has no native capacity field.
Bring-your-own feed: download + unzip it (e.g. from Transitland or the agency),
then pass the directory path to :func:`load` — or let :func:`fetch` pull and
extract a Transitland feed for you.
"""

from __future__ import annotations

import numpy as np

from .. import _gravel

# Transitland v2 REST API. Feeds are keyed by Onestop ID (e.g. ``f-9q9-caltrain``)
# or integer feed ID. Override the base via GRAVEL_TRANSITLAND_ENDPOINT, or pass
# endpoint=. A free API key is REQUIRED for Transitland (get one at
# https://www.transit.land/documentation — sign-up flow); pass apikey= or set
# GRAVEL_TRANSITLAND_APIKEY. Keyless use is possible only via feed_url= (a direct
# agency/portal ZIP link).
_TRANSITLAND_ENDPOINT = "https://transit.land/api/v2/rest"


def load(directory, *, window_hours=18.0, capacity_model=None):
    """Load a GTFS feed directory into a transit graph.

    Returns ``(Graph, capacity)`` — the graph carries stop coordinates and
    ``capacity`` is the persons/hour throughput proxy (CSR-aligned float64).
    ``window_hours`` is the frequency denominator (default 18h service span).
    ``capacity_model`` is an optional :class:`gravel.GtfsCapacityModel` overriding
    the per-mode vehicle-capacity assumptions.
    """
    cfg = _gravel.GtfsConfig()
    cfg.dir = directory
    cfg.window_hours = float(window_hours)
    if capacity_model is not None:
        cfg.capacity_model = capacity_model
    graph, capacity = _gravel.load_gtfs_network(cfg)
    return graph, np.asarray(capacity, dtype=np.float64)


def fetch(
    dest_dir,
    onestop_id=None,
    *,
    feed_url=None,
    apikey=None,
    endpoint=None,
    extra_headers=None,
    timeout=120.0,
):
    """Download a GTFS static feed ZIP and extract it into ``dest_dir``.

    Returns ``(feed_dir, Provenance)`` — ``feed_dir`` is the directory of extracted
    ``.txt`` files, ready to hand straight to :func:`load`.

    Two ways to name the feed:

    * ``onestop_id`` — a Transitland feed key (Onestop ID like ``"f-9q9-caltrain"``
      or an integer feed ID). Downloads the feed's latest version from Transitland's
      ``GET /feeds/{feed_key}/download_latest_feed_version`` endpoint. **Transitland
      requires a free API key** — pass ``apikey=`` or set the
      ``GRAVEL_TRANSITLAND_APIKEY`` environment variable (sign up at
      https://www.transit.land/documentation). The key is sent as the ``apikey``
      header. Only the *latest* version is generally available, and the download is
      refused (HTTP 401) when the source feed's license forbids redistribution — in
      that case fetch the agency's own ZIP directly with ``feed_url=``.
    * ``feed_url`` — a direct HTTPS link to a GTFS ``.zip`` (an agency or portal
      URL). No API key needed. Takes precedence over ``onestop_id`` if both are given.

    ``endpoint`` overrides the Transitland base URL (default
    ``https://transit.land/api/v2/rest``, or ``GRAVEL_TRANSITLAND_ENDPOINT``). The
    ``resolved_version`` on the returned :class:`Provenance` is the feed's version
    identifier (Transitland ``sha1``, else ``feed_info.txt``'s ``feed_version``, else
    ``"latest"``). Stdlib HTTP only; record the per-feed license from its source
    portal (it is out-of-band, not in the ZIP).
    """
    import os
    import zipfile
    from urllib.error import HTTPError
    from urllib.request import Request, urlopen

    from ._provenance import Provenance

    if feed_url is None and onestop_id is None:
        raise ValueError("pass either onestop_id= (Transitland) or feed_url= (direct ZIP)")

    ep = (endpoint or os.environ.get("GRAVEL_TRANSITLAND_ENDPOINT")
          or _TRANSITLAND_ENDPOINT).rstrip("/")

    os.makedirs(dest_dir, exist_ok=True)
    zip_path = os.path.join(dest_dir, "feed.zip")

    if feed_url is not None:
        # Keyless (or caller-supplied auth via extra_headers): fetch the ZIP straight
        # from the given URL — e.g. an agency portal link, or a WMATA endpoint with
        # an ``api_key`` header (see :func:`fetch_city`).
        url, auth, source = feed_url, {}, feed_url
    else:
        key = apikey or os.environ.get("GRAVEL_TRANSITLAND_APIKEY")
        if not key:
            raise ValueError(
                "Transitland requires a free API key. Pass apikey= or set "
                "GRAVEL_TRANSITLAND_APIKEY (sign up at "
                "https://www.transit.land/documentation), or use feed_url= for a "
                "direct agency ZIP link that needs no key."
            )
        source = f"{ep}/feeds/{onestop_id}/download_latest_feed_version"
        url, auth = source, {"apikey": key}

    headers = {"User-Agent": "gravel-fragility", **auth, **(extra_headers or {})}
    req = Request(url, headers=headers)
    try:
        with urlopen(req, timeout=timeout) as resp:  # noqa: S310 - https feed source
            body = resp.read()
    except HTTPError as exc:  # pragma: no cover - network path
        if feed_url is None and exc.code in (401, 403):
            raise RuntimeError(
                f"Transitland refused the download for {onestop_id!r} (HTTP "
                f"{exc.code}). The API key may be missing/invalid, or the feed's "
                "license forbids redistribution — fetch the agency's own ZIP with "
                "feed_url= instead."
            ) from exc
        raise
    with open(zip_path, "wb") as fh:
        fh.write(body)

    feed_dir = os.path.join(dest_dir, "feed")
    os.makedirs(feed_dir, exist_ok=True)
    with zipfile.ZipFile(zip_path) as zf:
        zf.extractall(feed_dir)
    # Some feeds nest the .txt files in a subdirectory; if agency.txt lives one
    # level down, use that directory as the feed root instead.
    feed_dir = _gtfs_root(feed_dir)

    version = _resolve_gtfs_version(feed_dir)
    if version is None and feed_url is None:
        version = _transitland_latest_sha1(ep, onestop_id, apikey, timeout)
    prov = Provenance.stamp("gtfs", source, version or "latest")
    return feed_dir, prov


# Major-city GTFS presets — direct agency feed URLs so a caller can pull a whole
# city's transit network by name without hunting for the ZIP. NYC and Chicago are
# keyless; WMATA (DC) needs a free ``api_key`` (developer.wmata.com), sent as a
# header. Aliases route common names to the canonical key. Feeds move occasionally
# — override with ``feed_url=`` on :func:`fetch` if an agency relocates its ZIP.
CITY_FEEDS: dict[str, dict] = {
    "nyc": {
        "label": "New York City Subway (MTA)",
        "feed_url": "https://rrgtfsfeeds.s3.amazonaws.com/gtfs_subway.zip",
        "modes": "subway",
        "license": "MTA open data — see https://www.mta.info/developers",
        "aliases": ("new_york", "newyork", "new_york_city", "mta"),
    },
    "chicago": {
        "label": "Chicago (CTA)",
        "feed_url": "https://www.transitchicago.com/downloads/sch_data/google_transit.zip",
        "modes": "bus+rail",
        "license": "CTA open data — see https://www.transitchicago.com/developers/",
        "aliases": ("cta", "chi"),
    },
    "dc": {
        "label": "Washington Metrorail (WMATA)",
        "feed_url": "https://api.wmata.com/gtfs/rail-gtfs-static.zip",
        "modes": "rail",
        "needs_key": True,
        "key_header": "api_key",
        "key_env": "GRAVEL_WMATA_APIKEY",
        "license": "WMATA open data — free key at https://developer.wmata.com",
        "aliases": ("washington", "wmata", "washington_dc"),
    },
    "bart": {
        "label": "San Francisco Bay Area Rapid Transit (BART)",
        "feed_url": "https://www.bart.gov/dev/schedules/google_transit.zip",
        "modes": "regional rail",
        "license": "BART open data (Developer License Agreement; no key) — "
        "https://www.bart.gov/schedules/developers/gtfs",
        "aliases": ("sf", "san_francisco", "bay_area", "sf_bart"),
    },
    "boston": {
        "label": "Boston (MBTA — subway + commuter rail + bus)",
        "feed_url": "https://cdn.mbta.com/MBTA_GTFS.zip",
        "modes": "subway+commuter rail+bus",
        "license": "MBTA open data (keyless) — https://www.mbta.com/developers/gtfs",
        "aliases": ("mbta", "the_t"),
    },
}


def _resolve_city(city):
    """Map a city name/alias (case-insensitive) to a :data:`CITY_FEEDS` key."""
    key = str(city).strip().lower().replace(" ", "_").replace("-", "_")
    if key in CITY_FEEDS:
        return key
    for canonical, spec in CITY_FEEDS.items():
        if key in spec.get("aliases", ()):
            return canonical
    raise KeyError(
        f"unknown city {city!r}; known cities: {sorted(CITY_FEEDS)} "
        f"(or pass feed_url= to gtfs.fetch for any agency)"
    )


def cities():
    """Return the supported major-city GTFS presets (see :data:`CITY_FEEDS`).

    A dict keyed by canonical city name (``"nyc"``, ``"dc"``, ``"chicago"``) with
    the human label, modes, license note, and whether an API key is required.
    """
    return {
        k: {
            "label": v["label"],
            "modes": v["modes"],
            "needs_key": bool(v.get("needs_key")),
            "license": v["license"],
        }
        for k, v in CITY_FEEDS.items()
    }


def fetch_city(city, dest_dir, *, apikey=None, timeout=120.0):
    """Fetch + extract a major-city GTFS feed by name. Returns ``(feed_dir, Provenance)``.

    ``city`` is a name or alias from :func:`cities` — ``"nyc"`` (MTA subway),
    ``"chicago"`` (CTA bus + rail), or ``"dc"`` (WMATA Metrorail). NYC and Chicago
    are keyless direct pulls; **DC (WMATA) requires a free API key** — pass
    ``apikey=`` or set ``GRAVEL_WMATA_APIKEY`` (register at
    https://developer.wmata.com), which is sent as WMATA's ``api_key`` header. The
    returned directory is ready to hand to :func:`load`; record the per-feed license
    (see :func:`cities`). Thin convenience over :func:`fetch` with a preset
    ``feed_url``; for any other agency, call :func:`fetch` with your own ``feed_url``.
    """
    import os

    spec = CITY_FEEDS[_resolve_city(city)]
    extra_headers = None
    if spec.get("needs_key"):
        key = apikey or os.environ.get(spec["key_env"])
        if not key:
            raise ValueError(
                f"{spec['label']} requires a free API key. Pass apikey= or set "
                f"{spec['key_env']} (register at https://developer.wmata.com)."
            )
        extra_headers = {spec["key_header"]: key}
    return fetch(
        dest_dir, feed_url=spec["feed_url"], extra_headers=extra_headers, timeout=timeout
    )


def _gtfs_root(feed_dir):
    """Return the directory that actually holds the GTFS ``.txt`` files.

    Handles feeds whose ZIP nests everything one level down inside a folder.
    """
    import os

    if os.path.exists(os.path.join(feed_dir, "stops.txt")):
        return feed_dir
    for name in sorted(os.listdir(feed_dir)):
        sub = os.path.join(feed_dir, name)
        if os.path.isdir(sub) and os.path.exists(os.path.join(sub, "stops.txt")):
            return sub
    return feed_dir


def _resolve_gtfs_version(feed_dir):
    """Read ``feed_version`` from ``feed_info.txt`` if present, else ``None``.

    A quote-naive, header-mapped read of the single ``feed_info`` row — enough for
    a citable version string, not a full CSV parse.
    """
    import csv
    import os

    path = os.path.join(feed_dir, "feed_info.txt")
    if not os.path.exists(path):
        return None
    try:
        with open(path, encoding="utf-8-sig", newline="") as fh:
            for row in csv.DictReader(fh):
                v = (row.get("feed_version") or "").strip()
                return v or None
    except (OSError, csv.Error):  # pragma: no cover - malformed feed_info
        return None
    return None


def _transitland_latest_sha1(endpoint, onestop_id, apikey, timeout):
    """Best-effort: the latest feed version's ``sha1`` from Transitland metadata.

    Returns ``None`` on any failure — provenance falls back to ``"latest"``.
    """
    import json
    import os
    from urllib.request import Request, urlopen

    key = apikey or os.environ.get("GRAVEL_TRANSITLAND_APIKEY")
    if not key:
        return None
    url = f"{endpoint}/feeds/{onestop_id}/feed_versions.json?limit=1"
    req = Request(url, headers={"User-Agent": "gravel-fragility", "apikey": key})
    try:  # pragma: no cover - network path
        with urlopen(req, timeout=timeout) as resp:  # noqa: S310 - https metadata
            payload = json.loads(resp.read())
        versions = payload.get("feed_versions") or []
        return (versions[0].get("sha1") or None) if versions else None
    except Exception:  # pragma: no cover - metadata is optional
        return None
