#pragma once
#include "gravel/fragility/fragility_result.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/ch/contraction.h"
#include "gravel/core/array_graph.h"
#include <vector>

namespace gravel {

struct FragilityValidationConfig {
    uint32_t sample_count = 100;
    uint64_t seed = 42;
    double tolerance = 1e-6;
};

struct FragilityValidationReport {
    bool passed = true;
    uint32_t pairs_tested = 0;
    uint32_t edges_tested = 0;
    uint32_t mismatches = 0;
    double max_absolute_error = 0.0;
};

// Validate route_fragility against naive leave-one-out (Dijkstra on modified graph).
FragilityValidationReport validate_fragility(
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const ArrayGraph& graph,
    FragilityValidationConfig config = {});

// Validate that blocking original edges within shortcut expansions works correctly
// by comparing CH blocked query against reference Dijkstra on uncontracted graph.
FragilityValidationReport validate_shortcut_interaction(
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const ArrayGraph& graph,
    FragilityValidationConfig config = {});

}  // namespace gravel
