"""CAIDA Internet Topology Data Kit — ITDK (``gravel.datasets.caida``).

Load a CAIDA ITDK release (router-level internet topology) into a graph.
**Bring-your-own files**: the ITDK is governed by the CAIDA Acceptable Use
Agreement (restricted, non-transferable), so Gravel ships no fetcher — download
and ``bunzip2`` the ``*.nodes`` / ``*.links`` (and optional ``*.nodes.geo``)
yourself, then pass their paths to :func:`load`.
"""

from __future__ import annotations

import numpy as np

from .. import _gravel


def load(
    nodes_path,
    links_path,
    *,
    nodes_geo_path="",
    expansion="CLIQUE",
    drop_placeholder_nodes=False,
):
    """Load a CAIDA ITDK release into a router graph.

    Returns ``(Graph, capacity)`` — ``capacity`` is empty (the ITDK carries
    connectivity only). ``expansion`` is ``"CLIQUE"`` (pairwise, default) or
    ``"STAR"`` for how a multi-node link becomes edges. ``nodes_geo_path`` (an
    optional decompressed ``*.nodes.geo``) populates node coordinates.
    """
    cfg = _gravel.ItdkConfig()
    cfg.nodes_path = nodes_path
    cfg.links_path = links_path
    if nodes_geo_path:
        cfg.nodes_geo_path = nodes_geo_path
    cfg.expansion = getattr(_gravel.ItdkLinkExpansion, str(expansion).upper())
    cfg.drop_placeholder_nodes = drop_placeholder_nodes
    graph, capacity = _gravel.load_caida_itdk(cfg)
    return graph, np.asarray(capacity, dtype=np.float64)
