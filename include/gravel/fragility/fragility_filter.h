#pragma once
#include "gravel/fragility/fragility_result.h"
#include "gravel/ch/shortcut_index.h"
#include "gravel/ch/contraction.h"
#include "gravel/core/array_graph.h"
#include "gravel/ch/landmarks.h"
#include <vector>

namespace gravel {

struct FilterConfig {
    double ch_level_percentile = 0.80;    // top 20% of CH levels pass screening
    double skip_ratio_threshold = 1.05;   // skip if ALT proves ratio < this
    bool use_ch_level_filter = true;
    bool use_alt_filter = true;
};

struct FilteredFragilityResult : FragilityResult {
    uint32_t edges_screened = 0;   // edges skipped by filters
    uint32_t edges_computed = 0;   // edges requiring exact computation
};

// Route fragility with filter-then-verify pipeline.
// Stages: bridge detection → CH level screening → ALT lower bound → exact blocked query.
FilteredFragilityResult filtered_route_fragility(
    const ContractionResult& ch,
    const ShortcutIndex& idx,
    const ArrayGraph& graph,
    const LandmarkData& landmarks,
    NodeID source, NodeID target,
    FilterConfig config = {});

}  // namespace gravel
