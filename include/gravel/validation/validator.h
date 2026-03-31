#pragma once
#include "gravel/core/graph_interface.h"
#include "gravel/ch/ch_query.h"
#include <vector>
#include <tuple>

namespace gravel {

struct ValidationConfig {
    enum Mode { EXHAUSTIVE, SAMPLED };
    Mode mode = EXHAUSTIVE;
    uint32_t sample_count = 10000;
    uint64_t seed = 42;
    double tolerance = 1e-6;  // absolute tolerance for weight comparison
    bool abort_on_first = false;
};

struct ValidationReport {
    bool passed = true;
    uint32_t pairs_tested = 0;
    uint32_t mismatches = 0;
    double max_absolute_error = 0.0;
    // (source, target, expected_dist, ch_dist)
    std::vector<std::tuple<NodeID, NodeID, Weight, Weight>> failures;
};

// Compare CH query results against reference Dijkstra on the original graph.
// This is the primary correctness oracle for the CH implementation.
ValidationReport validate_ch(const GravelGraph& original,
                             const CHQuery& ch_query,
                             ValidationConfig config = {});

}  // namespace gravel
