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
        # Keyless: fetch the ZIP straight from the given URL.
        url, headers, source = feed_url, {}, feed_url
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
        url, headers = source, {"apikey": key}

    headers = {"User-Agent": "gravel-fragility", **headers}
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
