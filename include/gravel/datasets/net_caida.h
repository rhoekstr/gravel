#pragma once
/// @file net_caida.h
/// @brief Loader for the CAIDA Macroscopic Internet Topology Data Kit (ITDK).

#include "gravel/datasets/network_graph.h"

#include <string>

namespace gravel {

/// Router-graph expansion policy for a `.links` line that lists more than two
/// member nodes (a shared L2 segment — POS/ATM/Ethernet). CAIDA links are IP-layer
/// links, not physical cables, so a multi-node link has no single canonical
/// graph representation and the caller must choose.
enum class ItdkLinkExpansion {
    CLIQUE,  ///< Every member node is joined pairwise (O(k^2) edges per link). The usual choice for a connectivity/fragility graph.
    STAR,    ///< The first member node is the hub; every other member joins it (k-1 edges per link).
};

/// Inputs for loading a CAIDA ITDK release into a router graph.
///
/// An ITDK release is a *directory of files*, not an API — you download the two
/// topology files you need over HTTPS and point this loader at them. Because the
/// ITDK is governed by the CAIDA Acceptable Use Agreement (a restricted-use,
/// non-transferable license), Gravel ships **no fetcher**: the user must bring
/// their own already-downloaded, already-decompressed files (`bunzip2` the
/// `*.nodes.bz2` / `*.links.bz2` / `*.nodes.geo.bz2` first).
struct ItdkConfig {
    std::string nodes_path;  ///< Path to the decompressed `*.nodes` file (e.g. `midar-iff.nodes`). REQUIRED.
    std::string links_path;  ///< Path to the decompressed `*.links` file (e.g. `midar-iff.links`). REQUIRED.

    /// Optional path to the decompressed `*.nodes.geo` file. When set and present,
    /// each node with a geo row gets its (lat, lon); nodes without one keep the
    /// default (0, 0). Leave empty to load a topology-only graph with no coords.
    std::string nodes_geo_path;

    /// How to turn a link that touches more than two nodes into graph edges.
    ItdkLinkExpansion expansion = ItdkLinkExpansion::CLIQUE;

    /// Drop nodes whose only interface addresses are synthetic placeholders
    /// (IANA `224.0.0.0/3`, or `0.0.0.0/8` in releases <= 2013-04) — non-responding
    /// hops CAIDA emits to preserve path structure. When false (default), they are
    /// kept as ordinary vertices.
    bool drop_placeholder_nodes = false;
};

/// Load a CAIDA ITDK release (router topology) into a `NetworkGraph`.
///
/// Nodes are routers (`node N<id>: ...` lines in `.nodes`); edges come from links
/// (`link L<id>: <N1>:ip <N2>:ip ...` lines in `.links`), with multi-node links
/// expanded per `config.expansion`. The graph is built **undirected** (each
/// derived pair contributes both directions). Every edge is **unit weight** —
/// the ITDK carries connectivity only, with no bandwidth, capacity, length, or
/// latency of any kind, so the returned `NetworkGraph::capacity` is **empty**.
///
/// Node coordinates are populated only when `config.nodes_geo_path` is set and the
/// file is present; otherwise the graph has no coordinates. `city`/region text in
/// that file is ISO-8859-1 but only its numeric lat/lon are read here.
///
/// A link member that references a node id never seen in `.nodes` still registers
/// that node (some releases reference nodes only from links); such nodes get no
/// coordinate. Lines beginning with `#` (the large kapar-command header block) are
/// skipped in every file.
///
/// @param config  Paths and expansion/placeholder options (see `ItdkConfig`).
/// @return A `NetworkGraph` whose `graph` is the router topology and whose
///         `capacity` is empty (the ITDK defines none).
/// @throws std::runtime_error if `nodes_path` or `links_path` cannot be opened.
NetworkGraph load_caida_itdk(const ItdkConfig& config);

}  // namespace gravel
