#include <catch2/catch_test_macros.hpp>
#include "gravel/core/four_heap.h"
#include <vector>
#include <algorithm>

using namespace gravel;

TEST_CASE("FourHeap basic operations", "[four_heap]") {
    FourHeap heap;
    REQUIRE(heap.empty());

    heap.push(5, 3.0);
    heap.push(2, 1.0);
    heap.push(8, 2.0);

    REQUIRE(!heap.empty());

    auto e1 = heap.pop();
    REQUIRE(e1.node == 2);
    REQUIRE(e1.dist == 1.0);

    auto e2 = heap.pop();
    REQUIRE(e2.node == 8);
    REQUIRE(e2.dist == 2.0);

    auto e3 = heap.pop();
    REQUIRE(e3.node == 5);
    REQUIRE(e3.dist == 3.0);

    REQUIRE(heap.empty());
}

TEST_CASE("FourHeap handles duplicates (lazy deletion pattern)", "[four_heap]") {
    FourHeap heap;

    // Push same node twice with different distances
    heap.push(10, 5.0);
    heap.push(10, 2.0);  // improved distance

    auto e1 = heap.pop();
    REQUIRE(e1.node == 10);
    REQUIRE(e1.dist == 2.0);

    // Second pop gives stale entry
    auto e2 = heap.pop();
    REQUIRE(e2.node == 10);
    REQUIRE(e2.dist == 5.0);

    REQUIRE(heap.empty());
}

TEST_CASE("FourHeap clear and reuse", "[four_heap]") {
    FourHeap heap;

    heap.push(1, 1.0);
    heap.push(2, 2.0);
    heap.clear();
    REQUIRE(heap.empty());

    heap.push(3, 0.5);
    auto e = heap.pop();
    REQUIRE(e.node == 3);
    REQUIRE(e.dist == 0.5);
}

TEST_CASE("FourHeap handles many elements in sorted order", "[four_heap]") {
    FourHeap heap;
    const int N = 1000;

    // Insert in reverse order
    for (int i = N - 1; i >= 0; --i) {
        heap.push(static_cast<NodeID>(i), static_cast<Weight>(i));
    }

    // Should come out in ascending order
    for (int i = 0; i < N; ++i) {
        REQUIRE(!heap.empty());
        auto e = heap.pop();
        REQUIRE(e.node == static_cast<NodeID>(i));
        REQUIRE(e.dist == static_cast<Weight>(i));
    }
    REQUIRE(heap.empty());
}

TEST_CASE("FourHeap handles equal priorities", "[four_heap]") {
    FourHeap heap;

    heap.push(1, 5.0);
    heap.push(2, 5.0);
    heap.push(3, 5.0);

    std::vector<NodeID> nodes;
    while (!heap.empty()) {
        nodes.push_back(heap.pop().node);
    }
    REQUIRE(nodes.size() == 3);

    // All three nodes should be present (order among equal priorities is unspecified)
    std::sort(nodes.begin(), nodes.end());
    REQUIRE(nodes == std::vector<NodeID>{1, 2, 3});
}
