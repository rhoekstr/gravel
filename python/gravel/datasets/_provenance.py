"""Fetch provenance — a lean, citable record of what was pulled, from where, when.

Returned as the second element of every ``fetch()`` (``(data, provenance)``).
Deliberately not full lineage: just enough to cite the pull in a methods section
and re-request it — the dataset id, the exact endpoint, the resolved version
(event id, week, vintage, ...), and the UTC pull time.
"""

from __future__ import annotations

import dataclasses
import datetime
import json as _json


@dataclasses.dataclass(frozen=True)
class Provenance:
    """What a dataset fetch retrieved, as a citable stamp."""

    dataset_id: str
    endpoint: str
    resolved_version: str
    pulled_at: str  # ISO-8601 UTC, second precision

    @staticmethod
    def stamp(dataset_id: str, endpoint: str, resolved_version) -> Provenance:
        """Create a record stamped at the current UTC time."""
        now = datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)
        return Provenance(
            dataset_id=dataset_id,
            endpoint=endpoint,
            resolved_version=str(resolved_version),
            pulled_at=now.isoformat(),
        )

    def to_dict(self) -> dict:
        return dataclasses.asdict(self)

    def to_json(self, **kwargs) -> str:
        """The stamp as a JSON string (``**kwargs`` forwarded to ``json.dumps``)."""
        return _json.dumps(self.to_dict(), **kwargs)

    def summary(self) -> str:
        """A one-line human-readable citation."""
        return (
            f"{self.dataset_id}: {self.resolved_version} "
            f"from {self.endpoint} @ {self.pulled_at}"
        )

    def __str__(self) -> str:  # pragma: no cover - trivial
        return self.summary()
