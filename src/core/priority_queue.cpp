#include "gravel/core/priority_queue.h"
#include <cassert>
#include <algorithm>
#include <utility>

namespace gravel {

void MinHeap::clear() {
    heap_.clear();
    // Don't shrink pos_ — reuse across queries
    std::fill(pos_.begin(), pos_.end(), SIZE_MAX);
}

void MinHeap::reserve(size_t n) {
    heap_.reserve(n);
    pos_.resize(n, SIZE_MAX);
}

void MinHeap::ensure_pos_size(NodeID node) {
    if (node >= pos_.size()) {
        pos_.resize(static_cast<size_t>(node) + 1, SIZE_MAX);
    }
}

bool MinHeap::contains(NodeID node) const {
    return node < pos_.size() && pos_[node] != SIZE_MAX;
}

void MinHeap::push_or_decrease(NodeID node, Weight dist) {
    ensure_pos_size(node);
    if (pos_[node] != SIZE_MAX) {
        // Decrease key
        size_t idx = pos_[node];
        assert(idx < heap_.size());
        if (dist < heap_[idx].dist) {
            heap_[idx].dist = dist;
            sift_up(idx);
        }
    } else {
        // Insert
        pos_[node] = heap_.size();
        heap_.push_back({node, dist});
        sift_up(heap_.size() - 1);
    }
}

MinHeap::Entry MinHeap::pop() {
    assert(!heap_.empty());
    Entry result{heap_[0].node, heap_[0].dist};
    pos_[result.node] = SIZE_MAX;

    if (heap_.size() > 1) {
        heap_[0] = heap_.back();
        pos_[heap_[0].node] = 0;
        heap_.pop_back();
        sift_down(0);
    } else {
        heap_.pop_back();
    }
    return result;
}

void MinHeap::sift_up(size_t idx) {
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (heap_[idx].dist < heap_[parent].dist) {
            std::swap(heap_[idx], heap_[parent]);
            pos_[heap_[idx].node] = idx;
            pos_[heap_[parent].node] = parent;
            idx = parent;
        } else {
            break;
        }
    }
}

void MinHeap::sift_down(size_t idx) {
    size_t n = heap_.size();
    while (true) {
        size_t smallest = idx;
        size_t left = 2 * idx + 1;
        size_t right = 2 * idx + 2;

        if (left < n && heap_[left].dist < heap_[smallest].dist)
            smallest = left;
        if (right < n && heap_[right].dist < heap_[smallest].dist)
            smallest = right;

        if (smallest != idx) {
            std::swap(heap_[idx], heap_[smallest]);
            pos_[heap_[idx].node] = idx;
            pos_[heap_[smallest].node] = smallest;
            idx = smallest;
        } else {
            break;
        }
    }
}

}  // namespace gravel
