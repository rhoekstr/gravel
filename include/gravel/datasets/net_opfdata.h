#pragma once
/// @file net_opfdata.h
/// @brief Loader for an OPFData (DeepMind PGLib-OPF-derived) solved AC-OPF example
///        into a capacitated bus-level graph.

#include "gravel/datasets/network_graph.h"

#include <string>

namespace gravel {

/// Load a single OPFData example JSON into a bus-level capacitated graph.
///
/// OPFData (`gs://gridopt-dataset`, DeepMind arXiv:2406.07234) is a *synthetic*
/// power-grid dataset: each `example_*.json` is one solved AC-OPF instance derived
/// from a PGLib-OPF base case. This loader parses the **base-case topology** of one
/// such example — it does not solve, and it ignores the OPF `solution` block.
///
/// Mapping (see the OPFData spec):
/// - **Nodes** = rows of `grid.nodes.bus`; the vertex id is the bus row index. The
///   grid carries no geography, so the returned graph has **no node coordinates**.
/// - **Edges** = `grid.edges.ac_line` followed by `grid.edges.transformer`, each a
///   bus→bus link (`senders[k]`→`receivers[k]`). Parallel edges between the same
///   bus pair are kept (the grid is a genuine multigraph). Edges are treated as
///   **undirected**: each parsed line/transformer yields a forward and a reverse
///   CSR entry so both endpoints see it.
/// - **Capacity** = the long-term thermal rating `rate_a`, converted from per-unit
///   to physical MVA with `capacity = rate_a * baseMVA` (`baseMVA = grid.context[0]`).
///   The rate_a feature column differs by edge type (ac_line index 6, transformer
///   index 4). A `rate_a == 0` encodes "no thermal limit" in PGLib and is mapped to
///   `+infinity` capacity, **not** a severed edge. Capacity is returned CSR-aligned
///   (both directions of an undirected edge carry the same value).
///
/// The **N-1 contingency** variant (`dataset_release_1_nminusone`) needs no special
/// handling: the dropped component is simply absent from the JSON, so parsing an
/// example as-is already yields its post-contingency topology. Pass any example
/// path — base-case or N-1 — and you get the grid that example describes.
///
/// Edge weight is set to the branch impedance magnitude `hypot(br_r, br_x)` (a
/// nonnegative electrical "length"), so shortest-path analyses have a meaningful
/// metric; capacity travels separately in `NetworkGraph::capacity`.
///
/// @param json_path Path to one OPFData `example_*.json` file (extracted from a
///        `{case_name}_{i}.tar.gz` group). Not a directory, not a tarball.
/// @return A `NetworkGraph` whose `graph` has one node per bus and one CSR edge per
///         direction of each line/transformer, and whose `capacity` is populated
///         (CSR-aligned) with per-edge MVA thermal ratings. `NetworkGraph::graph`
///         has no node coordinates (OPFData carries no geography).
/// @throws std::runtime_error if the file cannot be opened, is not valid JSON, or
///         is missing the `grid.nodes.bus` / `grid.edges` structure.
NetworkGraph load_opfdata_graph(const std::string& json_path);

}  // namespace gravel
