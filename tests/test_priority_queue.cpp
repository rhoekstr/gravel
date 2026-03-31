#include <catch2/catch_test_macros.hpp>
#include "gravel/core/priority_queue.h"

using namespace gravel;

TEST_CASE("MinHeap basic operations", "[priority_queue]") {
    MinHeap pq;

    SECTION("empty heap") {
        REQUIRE(pq.empty());
        REQUIRE(pq.size() == 0);
    }

    SECTION("push and pop single element") {
        pq.push_or_decrease(5, 3.0);
        REQUIRE(!pq.empty());
        REQUIRE(pq.size() == 1);
        REQUIRE(pq.contains(5));

        auto e = pq.pop();
        REQUIRE(e.node == 5);
        REQUIRE(e.dist == 3.0);
        REQUIRE(pq.empty());
    }

    SECTION("pop returns minimum") {
        pq.push_or_decrease(0, 10.0);
        pq.push_or_decrease(1, 3.0);
        pq.push_or_decrease(2, 7.0);

        auto e1 = pq.pop();
        REQUIRE(e1.node == 1);
        REQUIRE(e1.dist == 3.0);

        auto e2 = pq.pop();
        REQUIRE(e2.node == 2);
        REQUIRE(e2.dist == 7.0);

        auto e3 = pq.pop();
        REQUIRE(e3.node == 0);
        REQUIRE(e3.dist == 10.0);
    }
}

TEST_CASE("MinHeap decrease-key", "[priority_queue]") {
    MinHeap pq;
    pq.push_or_decrease(0, 10.0);
    pq.push_or_decrease(1, 5.0);
    pq.push_or_decrease(2, 8.0);

    // Decrease node 0 from 10.0 to 1.0
    pq.push_or_decrease(0, 1.0);

    auto e = pq.pop();
    REQUIRE(e.node == 0);
    REQUIRE(e.dist == 1.0);
}

TEST_CASE("MinHeap decrease-key ignores higher values", "[priority_queue]") {
    MinHeap pq;
    pq.push_or_decrease(0, 5.0);
    pq.push_or_decrease(0, 10.0);  // Should NOT increase

    auto e = pq.pop();
    REQUIRE(e.node == 0);
    REQUIRE(e.dist == 5.0);
}

TEST_CASE("MinHeap clear and reuse", "[priority_queue]") {
    MinHeap pq;
    pq.push_or_decrease(0, 1.0);
    pq.push_or_decrease(1, 2.0);
    pq.clear();

    REQUIRE(pq.empty());
    REQUIRE(!pq.contains(0));

    pq.push_or_decrease(0, 5.0);
    REQUIRE(pq.size() == 1);
    auto e = pq.pop();
    REQUIRE(e.dist == 5.0);
}

TEST_CASE("MinHeap handles many elements", "[priority_queue]") {
    MinHeap pq;
    pq.reserve(1000);

    for (NodeID i = 0; i < 1000; ++i) {
        pq.push_or_decrease(i, static_cast<Weight>(1000 - i));
    }

    Weight prev = -1.0;
    while (!pq.empty()) {
        auto e = pq.pop();
        REQUIRE(e.dist > prev);
        prev = e.dist;
    }
}
