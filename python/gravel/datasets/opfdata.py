"""OPFData — DeepMind synthetic AC-OPF power grids (``gravel.datasets.opfdata``).

Load one OPFData example JSON (a solved AC-OPF instance: buses + branches with
thermal limits; no geography) into a graph plus a per-edge capacity array
(thermal rating, MVA; ``+inf`` where a branch declares no limit). Bring-your-own
file: pull an ``example_*.json`` from the ``gridopt-dataset`` GCS bucket and pass
its path to :func:`load`. CC BY 4.0; synthetic — not for real-world use.
"""

from __future__ import annotations

import numpy as np

from .. import _gravel


def load(json_path):
    """Load one OPFData ``example_*.json``.

    Returns ``(Graph, capacity)`` with ``capacity`` a float64 array of per-edge
    thermal ratings in MVA. The graph has no node coordinates (synthetic cases).
    """
    graph, capacity = _gravel.load_opfdata_graph(json_path)
    return graph, np.asarray(capacity, dtype=np.float64)
