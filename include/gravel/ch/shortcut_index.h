#pragma once
#include "gravel/ch/contraction.h"
#include <span>
#include <unordered_map>
#include <vector>

namespace gravel {

// Maps each original edge → the set of CH overlay edges (shortcuts) that contain it.
// Built once from ContractionResult, used by BlockedCHQuery for edge blocking.
class ShortcutIndex {
public:
    explicit ShortcutIndex(const ContractionResult& ch);

    // Returns all overlay edges (packed as uint64_t via pack_edge) that
    // recursively expand through the original edge (u, v).
    std::span<const uint64_t> shortcuts_using(NodeID u, NodeID v) const;

    // Number of original edges that have at least one shortcut dependency.
    size_t size() const { return reverse_map_.size(); }

private:
    const ContractionResult& ch_;
    std::unordered_map<uint64_t, std::vector<uint64_t>> reverse_map_;

    void expand_shortcut(uint64_t root_key, NodeID from, NodeID to);
};

}  // namespace gravel
