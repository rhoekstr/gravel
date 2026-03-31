#include "gravel/core/four_heap.h"
#include <algorithm>

namespace gravel {

void FourHeap::push(NodeID node, Weight dist) {
    if (size_ >= heap_.size()) {
        heap_.push_back({node, dist});
    } else {
        heap_[size_] = {node, dist};
    }
    sift_up(size_);
    ++size_;
}

FourHeap::Entry FourHeap::pop() {
    Entry result = heap_[0];
    --size_;
    if (size_ > 0) {
        heap_[0] = heap_[size_];
        sift_down(0);
    }
    return result;
}

void FourHeap::clear() {
    size_ = 0;
}

void FourHeap::reserve(size_t n) {
    heap_.reserve(n);
}

void FourHeap::sift_up(size_t idx) {
    Entry e = heap_[idx];
    while (idx > 0) {
        size_t p = parent(idx);
        if (heap_[p].dist <= e.dist) break;
        heap_[idx] = heap_[p];
        idx = p;
    }
    heap_[idx] = e;
}

void FourHeap::sift_down(size_t idx) {
    Entry e = heap_[idx];
    while (true) {
        size_t fc = first_child(idx);
        if (fc >= size_) break;

        // Find minimum child among up to 4 children
        size_t min_child = fc;
        size_t end = std::min(fc + 4, size_);
        for (size_t c = fc + 1; c < end; ++c) {
            if (heap_[c].dist < heap_[min_child].dist) {
                min_child = c;
            }
        }

        if (e.dist <= heap_[min_child].dist) break;
        heap_[idx] = heap_[min_child];
        idx = min_child;
    }
    heap_[idx] = e;
}

}  // namespace gravel
