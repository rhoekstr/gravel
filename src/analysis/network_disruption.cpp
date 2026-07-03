#include "gravel/analysis/network_disruption.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace gravel {
namespace {

// Union-find with per-component size, an "anchor" flag (component contains the hub), and a running
// Σ(size²) so the connectivity metric updates in O(1) per union.
struct UnionFind {
    std::vector<uint32_t> parent;
    std::vector<uint32_t> size;
    std::vector<char> anchor;
    double sumsq;

    explicit UnionFind(uint32_t n)
        : parent(n), size(n, 1), anchor(n, 0), sumsq(static_cast<double>(n)) {
        std::iota(parent.begin(), parent.end(), 0u);
    }

    uint32_t find(uint32_t a) {
        uint32_t r = a;
        while (parent[r] != r) r = parent[r];
        while (parent[a] != r) {
            uint32_t next = parent[a];
            parent[a] = r;
            a = next;
        }
        return r;
    }

    void unite(uint32_t a, uint32_t b) {
        uint32_t ra = find(a), rb = find(b);
        if (ra == rb) return;
        if (size[ra] < size[rb]) std::swap(ra, rb);
        sumsq -= static_cast<double>(size[ra]) * size[ra];
        sumsq -= static_cast<double>(size[rb]) * size[rb];
        parent[rb] = ra;
        size[ra] += size[rb];
        anchor[ra] = static_cast<char>(anchor[ra] || anchor[rb]);
        sumsq += static_cast<double>(size[ra]) * size[ra];
    }
};

}  // namespace

NetworkDisruption network_disruption(const ArrayGraph& graph,
                                     const std::vector<double>& failure_round) {
    const uint32_t n = graph.node_count();
    const auto& offsets = graph.raw_offsets();
    const auto& targets = graph.raw_targets();
    const uint32_t m = static_cast<uint32_t>(targets.size());
    const double kNaN = std::numeric_limits<double>::quiet_NaN();

    if (failure_round.size() != m) {
        throw std::invalid_argument(
            "network_disruption: failure_round length must equal the edge count");
    }

    NetworkDisruption out;
    out.stranded_round.assign(m, kNaN);

    int max_round = 0;
    for (double r : failure_round) {
        if (!std::isnan(r) && r > max_round) max_round = static_cast<int>(r);
    }
    out.severed_fraction.assign(static_cast<size_t>(max_round) + 1, 0.0);
    if (n == 0 || m == 0) return out;

    // Per-edge source (CSR: implicit from the offset array).
    std::vector<uint32_t> src(m);
    for (uint32_t u = 0; u < n; ++u) {
        for (uint32_t e = offsets[u]; e < offsets[u + 1]; ++e) src[e] = u;
    }

    // Hub = most-connected node; "stranded" = cut off from it.
    std::vector<uint32_t> degree(n, 0);
    for (uint32_t e = 0; e < m; ++e) {
        degree[src[e]]++;
        degree[targets[e]]++;
    }
    uint32_t hub = 0;
    for (uint32_t v = 1; v < n; ++v) {
        if (degree[v] > degree[hub]) hub = v;
    }

    // Nodes originally reachable from the hub (in the full graph).
    UnionFind full(n);
    for (uint32_t e = 0; e < m; ++e) full.unite(src[e], targets[e]);
    const uint32_t hub_root = full.find(hub);
    std::vector<char> in_main(n, 0);
    for (uint32_t v = 0; v < n; ++v) {
        in_main[v] = static_cast<char>(full.find(v) == hub_root);
    }

    // Reverse-incremental pass: start with only the never-removed edges present (the most-removed
    // stage) and add edges back in decreasing round.
    std::vector<std::vector<uint32_t>> by_round(static_cast<size_t>(max_round) + 1);
    UnionFind uf(n);
    uf.anchor[hub] = 1;
    for (uint32_t e = 0; e < m; ++e) {
        double r = failure_round[e];
        if (std::isnan(r)) {
            uf.unite(src[e], targets[e]);
        } else {
            by_round[static_cast<size_t>(r)].push_back(e);
        }
    }

    std::vector<double> node_strand(n, kNaN);
    const double n2 = static_cast<double>(n) * static_cast<double>(n);
    for (int k = max_round; k >= 0; --k) {
        out.severed_fraction[static_cast<size_t>(k)] = 1.0 - uf.sumsq / n2;
        for (uint32_t v = 0; v < n; ++v) {
            if (in_main[v] && !uf.anchor[uf.find(v)]) node_strand[v] = static_cast<double>(k);
        }
        if (k > 0) {
            for (uint32_t e : by_round[static_cast<size_t>(k)]) uf.unite(src[e], targets[e]);
        }
    }

    // An intact edge strands when its (shared) component detaches from the hub — the round its
    // endpoints strand, provided the edge is still present then.
    for (uint32_t e = 0; e < m; ++e) {
        double su = node_strand[src[e]], sv = node_strand[targets[e]];
        double cand = std::isnan(sv) ? su : (std::isnan(su) ? sv : std::min(su, sv));
        double r = failure_round[e];
        if (!std::isnan(cand) && (std::isnan(r) || r > cand)) out.stranded_round[e] = cand;
    }
    return out;
}

}  // namespace gravel
