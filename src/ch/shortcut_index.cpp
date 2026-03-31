#include "gravel/ch/shortcut_index.h"
#include <stack>

namespace gravel {

ShortcutIndex::ShortcutIndex(const ContractionResult& ch) : ch_(ch) {
    // For every shortcut in the unpack_map, recursively expand it to find
    // all original (leaf) edges. Record that the root shortcut depends on each leaf.

    // We only care about entries where mid != INVALID_NODE (actual shortcuts).
    // Original edges in the overlay (mid == INVALID_NODE) don't need reverse mapping
    // because blocking them is handled directly by their pack_edge key.

    for (const auto& [key, mid] : ch_.unpack_map) {
        if (mid == INVALID_NODE) continue;
        // key is a shortcut — expand it to find all original edges it contains
        NodeID from = static_cast<NodeID>(key >> 32);
        NodeID to = static_cast<NodeID>(key & 0xFFFFFFFF);
        expand_shortcut(key, from, to);
    }
}

void ShortcutIndex::expand_shortcut(uint64_t root_key, NodeID from, NodeID to) {
    // Iterative expansion to avoid stack overflow on deep shortcuts
    struct Frame {
        NodeID from, to;
    };

    std::stack<Frame> stk;
    stk.push({from, to});

    while (!stk.empty()) {
        auto [u, v] = stk.top();
        stk.pop();

        uint64_t edge_key = ContractionResult::pack_edge(u, v);
        auto it = ch_.unpack_map.find(edge_key);

        if (it == ch_.unpack_map.end() || it->second == INVALID_NODE) {
            // Original edge — this leaf is a dependency of root_key
            reverse_map_[edge_key].push_back(root_key);
        } else {
            // Further shortcut — expand both halves
            NodeID mid = it->second;
            stk.push({u, mid});
            stk.push({mid, v});
        }
    }
}

std::span<const uint64_t> ShortcutIndex::shortcuts_using(NodeID u, NodeID v) const {
    uint64_t key = ContractionResult::pack_edge(u, v);
    auto it = reverse_map_.find(key);
    if (it == reverse_map_.end()) return {};
    return {it->second.data(), it->second.size()};
}

}  // namespace gravel
