#pragma once
#include "gravel/core/types.h"
#include <vector>

namespace gravel {

// 4-ary min-heap with lazy deletion (no decrease-key).
// Better cache performance than binary heap: 4 children fit in ~one cache line.
// Callers must check staleness after pop() by comparing dist to current best.
class FourHeap {
public:
    struct Entry { NodeID node; Weight dist; };

    void push(NodeID node, Weight dist);
    Entry pop();
    bool empty() const { return size_ == 0; }
    void clear();
    void reserve(size_t n);

private:
    std::vector<Entry> heap_;
    size_t size_ = 0;

    void sift_up(size_t idx);
    void sift_down(size_t idx);

    static size_t parent(size_t i) { return (i - 1) / 4; }
    static size_t first_child(size_t i) { return 4 * i + 1; }
};

}  // namespace gravel
