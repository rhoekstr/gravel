#pragma once
#include "gravel/core/types.h"
#include <vector>

namespace gravel {

// Binary min-heap with decrease-key support.
// Used by Dijkstra and CH witness search.
class MinHeap {
public:
    void clear();
    void reserve(size_t n);
    bool empty() const { return heap_.empty(); }
    size_t size() const { return heap_.size(); }

    // Insert or decrease key for a node
    void push_or_decrease(NodeID node, Weight dist);

    // Extract minimum
    struct Entry { NodeID node; Weight dist; };
    Entry pop();

    // Check if node is currently in the heap
    bool contains(NodeID node) const;

private:
    struct HeapEntry {
        NodeID node;
        Weight dist;
    };

    std::vector<HeapEntry> heap_;
    std::vector<size_t> pos_;  // pos_[node] = index in heap_, or SIZE_MAX if absent

    void sift_up(size_t idx);
    void sift_down(size_t idx);
    void ensure_pos_size(NodeID node);
};

}  // namespace gravel
