"""OpenFlights air-transport network (``gravel.datasets.openflights``).

Load the OpenFlights airports + routes ``.dat`` files into a directed air network
(airports as nodes with lat/lon; routes as edges). No native per-edge capacity —
join BTS T-100 seat/passenger data separately for that. ODbL-licensed; attribute
OpenFlights.
"""

from __future__ import annotations

import numpy as np

from .. import _gravel

# The canonical raw data files (the openflights.org data.php links point here).
_AIRPORTS_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/airports.dat"
_ROUTES_URL = "https://raw.githubusercontent.com/jpatokal/openflights/master/data/routes.dat"


def load(airports_path, routes_path, *, collapse_parallel=True, drop_codeshare=True):
    """Load OpenFlights ``airports.dat`` + ``routes.dat``.

    Returns ``(Graph, capacity)`` — the graph carries per-airport coordinates and
    ``capacity`` is empty (OpenFlights has none). ``collapse_parallel`` merges
    duplicate airline/equipment rows over the same airport pair; ``drop_codeshare``
    skips marketing-only codeshare rows.
    """
    graph, capacity = _gravel.load_openflights_network(
        airports_path, routes_path, collapse_parallel, drop_codeshare
    )
    return graph, np.asarray(capacity, dtype=np.float64)


def fetch(dest_dir, *, airports_url=_AIRPORTS_URL, routes_url=_ROUTES_URL, timeout=60.0):
    """Download ``airports.dat`` + ``routes.dat`` into ``dest_dir``.

    Returns ``((airports_path, routes_path), Provenance)`` — feed the two paths to
    :func:`load`. Stdlib HTTP only.
    """
    import os
    from urllib.request import Request, urlopen

    from ._provenance import Provenance

    os.makedirs(dest_dir, exist_ok=True)
    out = {}
    for name, url in (("airports.dat", airports_url), ("routes.dat", routes_url)):
        req = Request(url, headers={"User-Agent": "gravel-fragility"})
        with urlopen(req, timeout=timeout) as resp:  # noqa: S310 - fixed https source
            body = resp.read()
        path = os.path.join(dest_dir, name)
        with open(path, "wb") as fh:
            fh.write(body)
        out[name] = path
    prov = Provenance.stamp("openflights", airports_url, "master")
    return (out["airports.dat"], out["routes.dat"]), prov
