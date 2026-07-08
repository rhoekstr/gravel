"""OPFData — DeepMind synthetic AC-OPF power grids (``gravel.datasets.opfdata``).

Load one OPFData example JSON (a solved AC-OPF instance: buses + branches with
thermal limits; no geography) into a graph plus a per-edge capacity array
(thermal rating, MVA; ``+inf`` where a branch declares no limit). Use
:func:`fetch` to pull a group of examples from the public ``gridopt-dataset`` GCS
bucket, then hand one ``example_*.json`` to :func:`load`. CC BY 4.0; synthetic —
not for real-world use.
"""

from __future__ import annotations

import numpy as np

from .. import _gravel

# Public GCS bucket, served over plain HTTPS (no auth). Override with base_url=.
BASE_URL = "https://storage.googleapis.com/gridopt-dataset"

# The two published variants: FullTop (load perturbation, fixed topology) and N-1
# (load perturbation plus one random line/transformer/generator removed).
RELEASE_FULLTOP = "dataset_release_1"
RELEASE_NMINUSONE = "dataset_release_1_nminusone"

# The 10 base PGLib-OPF grids, smallest first. Each ships 300k examples per variant
# as 20 gzipped tars (``{case_name}_{i}.tar.gz``, i in 0..19), 15,000 examples each.
CASES = (
    "pglib_opf_case14_ieee",
    "pglib_opf_case30_ieee",
    "pglib_opf_case57_ieee",
    "pglib_opf_case118_ieee",
    "pglib_opf_case500_goc",
    "pglib_opf_case2000_goc",
    "pglib_opf_case4661_sdet",
    "pglib_opf_case6470_rte",
    "pglib_opf_case10000_goc",
    "pglib_opf_case13659_pegase",
)

_NUM_GROUPS = 20  # groups 0..19 per case per variant
_UA = {"User-Agent": "gravel-fragility"}


def load(json_path):
    """Load one OPFData ``example_*.json``.

    Returns ``(Graph, capacity)`` with ``capacity`` a float64 array of per-edge
    thermal ratings in MVA. The graph has no node coordinates (synthetic cases).
    """
    graph, capacity = _gravel.load_opfdata_graph(json_path)
    return graph, np.asarray(capacity, dtype=np.float64)


def fetch(
    dest_dir,
    *,
    case_name=CASES[0],
    group=0,
    n_minus_one=False,
    base_url=BASE_URL,
    timeout=300.0,
):
    """Download and extract one OPFData tar group into ``dest_dir``.

    Pulls ``{base_url}/{release}/{case_name}_{group}.tar.gz`` — one of the 20
    groups (15,000 solved examples) published per grid per variant — over public
    HTTPS, extracts it, and returns ``((example_path, group_dir), Provenance)``.
    ``example_path`` is one ``example_*.json`` ready to hand straight to
    :func:`load`; ``group_dir`` holds all 15,000 for iterating. Stdlib HTTP only —
    the ``gridopt-dataset`` bucket is public, no auth.

    ``case_name`` is one of :data:`CASES` (default the smallest, ``case14_ieee`` —
    a ~28 MB tar good for prototyping; big grids like ``case13659_pegase`` are
    heavy). ``group`` is 0..19. ``n_minus_one=True`` selects the N-1 variant
    (:data:`RELEASE_NMINUSONE`), where each example has one line/transformer/
    generator removed; ``False`` (default) is FullTop (:data:`RELEASE_FULLTOP`).
    """
    import os
    import tarfile
    from urllib.request import Request, urlopen

    from ._provenance import Provenance

    if case_name not in CASES:
        raise ValueError(f"unknown case_name {case_name!r}; expected one of {CASES}")
    if not 0 <= group < _NUM_GROUPS:
        raise ValueError(f"group must be in 0..{_NUM_GROUPS - 1}, got {group}")

    release = RELEASE_NMINUSONE if n_minus_one else RELEASE_FULLTOP
    name = f"{case_name}_{group}.tar.gz"
    url = f"{base_url.rstrip('/')}/{release}/{name}"

    os.makedirs(dest_dir, exist_ok=True)
    archive_path = os.path.join(dest_dir, name)
    req = Request(url, headers=_UA)
    with urlopen(req, timeout=timeout) as resp:  # noqa: S310 - fixed https bucket
        with open(archive_path, "wb") as fh:
            while True:
                chunk = resp.read(1 << 20)
                if not chunk:
                    break
                fh.write(chunk)

    # Extract, then locate the group directory of example_*.json. The tar nests the
    # examples under gridopt-dataset-tmp/{release}/{case_name}/group_{group}/, but
    # don't hardcode that — walk the tree for the first example_*.json.
    with tarfile.open(archive_path) as tar:
        _safe_extractall(tar, dest_dir)

    example_path = None
    for root, _dirs, files in os.walk(dest_dir):
        for fname in files:
            if fname.startswith("example_") and fname.endswith(".json"):
                cand = os.path.join(root, fname)
                if example_path is None or cand < example_path:
                    example_path = cand
    if example_path is None:
        raise RuntimeError(
            f"no example_*.json found after extracting {name} into {dest_dir}"
        )
    group_dir = os.path.dirname(example_path)

    prov = Provenance.stamp("opfdata", url, f"{release}/{case_name}/group_{group}")
    return (example_path, group_dir), prov


def _safe_extractall(tar, dest_dir):
    """Extract ``tar`` into ``dest_dir``, rejecting members that escape the tree.

    Guards against path-traversal (``..``) and absolute-path members in the archive
    (the OPFData tars are trusted, but tarfile extraction is unsafe by default).
    """
    import os

    dest = os.path.realpath(dest_dir)
    for member in tar.getmembers():
        target = os.path.realpath(os.path.join(dest_dir, member.name))
        if target != dest and not target.startswith(dest + os.sep):
            raise RuntimeError(f"refusing unsafe tar member path: {member.name!r}")
    tar.extractall(dest_dir)  # noqa: S202 - members validated above
