"""Microsoft Research US Transmission Grid — GridSFM (``gravel.datasets.gridsfm``).

Load a GridSFM power-grid model JSON (buses with coordinates + branches with
thermal ratings) into a graph plus a per-edge capacity array (branch thermal
limit, MVA). Fetch a single ``*_model.json`` from the Hugging Face dataset
(https://huggingface.co/datasets/microsoft/GridSFM_US_power_grid) with
:func:`fetch`, or bring your own file and pass its path to :func:`load`.
MIT-licensed (attribution, no share-alike).
"""

from __future__ import annotations

import numpy as np

from .. import _gravel

# Hugging Face dataset holding the PowerModels/MATPOWER ``*_model.json`` cases
# (buses with lat/lon, branches with per-unit thermal ratings). Public, no auth.
_HF_REPO_ID = "microsoft/GridSFM_US_power_grid"
_HF_RESOLVE = (
    "https://huggingface.co/datasets/microsoft/GridSFM_US_power_grid/resolve"
)
# Two per-case snapshots ship in the repo: 04h = off-peak, 16h = peak (4 PM local).
_HOURS = ("04h", "16h")


def load(model_json_path):
    """Load a GridSFM ``*_model.json``.

    Returns ``(Graph, capacity)`` where ``capacity`` is a float64 array of per-edge
    thermal limits in MVA (CSR-aligned), and the graph carries per-bus coordinates.
    """
    graph, capacity = _gravel.load_gridsfm_network(model_json_path)
    return graph, np.asarray(capacity, dtype=np.float64)


def _case_filename(name, hour):
    """Repo-relative path ``<hour>/<name>_model.json`` for a case, with light
    normalization (lowercase, spaces/hyphens → underscores) of the region name."""
    hour = str(hour).lower()
    if hour not in _HOURS:
        raise ValueError(f"hour must be one of {_HOURS}, got {hour!r}")
    name = str(name).strip().lower().replace(" ", "_").replace("-", "_")
    if not name:
        raise ValueError("name must be a state or region (e.g. 'delaware', 'pjm')")
    return f"{hour}/{name}_model.json"


def fetch(dest_dir, name, *, hour="16h", revision="main", timeout=120.0):
    """Download one GridSFM ``<hour>/<name>_model.json`` into ``dest_dir``.

    Fetches a single grid case from the ``microsoft/GridSFM_US_power_grid`` Hugging
    Face dataset (public, no auth). ``name`` is a state (``"delaware"``, ``"texas"``,
    …) or one of the six multi-state regions (``"new_england"``, ``"pacific_nw"``,
    ``"desert_sw"``, ``"western"``, ``"eastern"``, ``"pjm"``); ``hour`` selects the
    off-peak (``"04h"``) or peak (``"16h"``, default) snapshot.

    Returns ``((model_json_path,), Provenance)`` — feed ``model_json_path`` to
    :func:`load`. Prefers ``huggingface_hub`` if installed (shared cache, resumable);
    otherwise falls back to a stdlib HTTPS pull of the resolve URL. ``revision`` is
    the git ref/commit to pin (default the ``main`` branch); the returned provenance
    records the concrete commit SHA when the server reports it.
    """
    import os

    from ._provenance import Provenance

    rel = _case_filename(name, hour)
    endpoint = f"{_HF_RESOLVE}/{revision}/{rel}"
    os.makedirs(dest_dir, exist_ok=True)

    # Prefer huggingface_hub when present (shared HF cache, resume, xet). Fall back
    # to a plain stdlib HTTPS pull so the dependency stays optional.
    try:
        from huggingface_hub import hf_hub_download  # noqa: PLC0415 - lazy optional
    except ImportError:
        model_path, resolved_version = _fetch_https(endpoint, dest_dir, rel, timeout)
    else:
        try:
            cached = hf_hub_download(
                repo_id=_HF_REPO_ID,
                filename=rel,
                repo_type="dataset",
                revision=revision,
                local_dir=dest_dir,
            )
        except Exception as exc:  # noqa: BLE001 - re-raise with actionable context
            raise RuntimeError(
                f"huggingface_hub could not fetch {rel!r} from {_HF_REPO_ID!r} "
                f"(revision {revision!r}): {exc}. The dataset is public (no token "
                f"needed) — check the name/hour spelling and network access, or "
                f"uninstall huggingface_hub to use the stdlib HTTPS fallback."
            ) from exc
        model_path = os.path.abspath(cached)
        resolved_version = _resolved_revision(model_path, revision)

    prov = Provenance.stamp("gridsfm", endpoint, resolved_version)
    return (model_path,), prov


def _fetch_https(url, dest_dir, rel, timeout):
    """Stdlib HTTPS fallback: download ``url`` to ``dest_dir`` mirroring the repo's
    ``<hour>/<name>_model.json`` layout. Returns ``(local_path, resolved_version)``,
    where the version is the ``X-Repo-Commit`` SHA if the server reports it."""
    import os
    from urllib.error import HTTPError
    from urllib.request import Request, urlopen

    req = Request(url, headers={"User-Agent": "gravel-fragility"})
    try:
        with urlopen(req, timeout=timeout) as resp:  # noqa: S310 - fixed https source
            body = resp.read()
            # HF exposes the concrete commit that served the file for citable pins.
            commit = resp.headers.get("X-Repo-Commit")
    except HTTPError as exc:
        if exc.code == 404:
            raise FileNotFoundError(
                f"GridSFM case not found at {url!r} (HTTP 404) — check the state/"
                f"region name and hour (04h/16h)."
            ) from exc
        raise

    out_path = os.path.join(dest_dir, rel)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as fh:
        fh.write(body)
    return os.path.abspath(out_path), commit or "main"


def _resolved_revision(cached_path, revision):
    """Best-effort concrete commit SHA behind a ``hf_hub_download`` result, so the
    provenance can be re-requested exactly. Reads it from the snapshot cache path
    when present, else resolves ``revision`` via the Hub API, else returns
    ``revision`` unchanged."""
    import os
    import re

    # A hub cache path is ``.../snapshots/<commit-sha>/<file>``; local_dir results
    # aren't, so this only upgrades the version when the snapshot layout is present.
    for candidate in (cached_path, os.path.realpath(cached_path)):
        match = re.search(r"[/\\]snapshots[/\\]([0-9a-f]{40})[/\\]", candidate)
        if match:
            return match.group(1)
    # local_dir download: ask the Hub which commit ``revision`` currently points to.
    try:
        from huggingface_hub import HfApi  # noqa: PLC0415 - lazy optional

        sha = HfApi().dataset_info(_HF_REPO_ID, revision=revision).sha
        if sha:
            return sha
    except Exception:  # noqa: BLE001 - provenance nicety, never fatal
        pass
    return revision
