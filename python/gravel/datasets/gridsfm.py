"""Microsoft Research US Transmission Grid — GridSFM (``gravel.datasets.gridsfm``).

Load a GridSFM power-grid model JSON (buses with coordinates + branches with
thermal ratings) into a graph plus a per-edge capacity array (branch thermal
limit, MVA). Bring-your-own file: download a ``*_model.json`` from Hugging Face
(https://huggingface.co/datasets/microsoft/GridSFM_US_power_grid) and pass its
path to :func:`load`. MIT-licensed.
"""

from __future__ import annotations

import numpy as np

from .. import _gravel


def load(model_json_path):
    """Load a GridSFM ``*_model.json``.

    Returns ``(Graph, capacity)`` where ``capacity`` is a float64 array of per-edge
    thermal limits in MVA (CSR-aligned), and the graph carries per-bus coordinates.
    """
    graph, capacity = _gravel.load_gridsfm_network(model_json_path)
    return graph, np.asarray(capacity, dtype=np.float64)
