#include "gravel/validation/validator.h"
#include "gravel/core/dijkstra.h"
#include <cmath>
#include <random>
#include <algorithm>
#include <utility>

namespace gravel {

ValidationReport validate_ch(const GravelGraph& original,
                             const CHQuery& ch_query,
                             ValidationConfig config) {
    ValidationReport report;
    NodeID n = original.node_count();

    std::vector<std::pair<NodeID, NodeID>> pairs;

    if (config.mode == ValidationConfig::EXHAUSTIVE) {
        for (NodeID s = 0; s < n; ++s) {
            for (NodeID t = 0; t < n; ++t) {
                if (s != t) pairs.emplace_back(s, t);
            }
        }
    } else {
        std::mt19937_64 rng(config.seed);
        std::uniform_int_distribution<NodeID> dist(0, n - 1);
        for (uint32_t i = 0; i < config.sample_count; ++i) {
            NodeID s = dist(rng);
            NodeID t = dist(rng);
            while (t == s) t = dist(rng);
            pairs.emplace_back(s, t);
        }
    }

    for (const auto& [s, t] : pairs) {
        Weight expected = dijkstra_pair(original, s, t);
        Weight got = ch_query.distance(s, t);

        report.pairs_tested++;

        // Compare: both should be INF or within tolerance
        bool match;
        if (expected == INF_WEIGHT && got == INF_WEIGHT) {
            match = true;
        } else if (expected == INF_WEIGHT || got == INF_WEIGHT) {
            match = false;
        } else {
            double err = std::abs(expected - got);
            report.max_absolute_error = std::max(report.max_absolute_error, err);
            match = err <= config.tolerance;
        }

        if (!match) {
            report.passed = false;
            report.mismatches++;
            report.failures.emplace_back(s, t, expected, got);

            if (config.abort_on_first) break;
        }
    }

    return report;
}

}  // namespace gravel
