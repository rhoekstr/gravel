#include "gravel/core/array_graph.h"
#include "gravel/ch/contraction.h"
#include "gravel/ch/ch_query.h"
#include "gravel/core/dijkstra.h"
#include "gravel/validation/synthetic_graphs.h"
#include <chrono>
#include <iostream>
#include <iomanip>
#include <random>
#include <vector>

using namespace gravel;
using Clock = std::chrono::high_resolution_clock;

struct BenchResult {
    uint32_t nodes;
    uint32_t edges;
    double ch_build_ms;
    double ch_query_avg_us;
    double dijkstra_avg_us;
    double speedup;
    size_t unpack_map_entries;
    double ch_memory_mb;
};

double to_ms(Clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
}

double to_us(Clock::duration d) {
    return std::chrono::duration<double, std::micro>(d).count();
}

BenchResult bench_graph(const ArrayGraph& graph, uint32_t query_pairs) {
    BenchResult r{};
    r.nodes = graph.node_count();
    r.edges = graph.edge_count();

    // Build CH
    auto t0 = Clock::now();
    auto ch = build_ch(graph);
    auto t1 = Clock::now();
    r.ch_build_ms = to_ms(t1 - t0);

    r.unpack_map_entries = ch.unpack_map.size();

    // Estimate CH memory: up/down SoA CSR + levels + order + unpack_map
    r.ch_memory_mb = (
        ch.up_offsets.size() * 4 +
        ch.up_targets.size() * 4 +
        ch.up_weights.size() * 8 +
        ch.up_shortcut_mid.size() * 4 +
        ch.down_offsets.size() * 4 +
        ch.down_targets.size() * 4 +
        ch.down_weights.size() * 8 +
        ch.down_shortcut_mid.size() * 4 +
        ch.node_levels.size() * 2 +
        ch.order.size() * 4 +
        ch.unpack_map.size() * (8 + 4 + /* bucket overhead ~16 */ 16)
    ) / (1024.0 * 1024.0);

    CHQuery query(ch);

    // Generate random pairs
    std::mt19937 rng(12345);
    std::uniform_int_distribution<NodeID> dist(0, graph.node_count() - 1);
    std::vector<std::pair<NodeID, NodeID>> pairs(query_pairs);
    for (auto& [s, t] : pairs) {
        s = dist(rng);
        t = dist(rng);
    }

    // Benchmark CH queries
    auto t2 = Clock::now();
    for (const auto& [s, t] : pairs) {
        query.distance(s, t);
    }
    auto t3 = Clock::now();
    r.ch_query_avg_us = to_us(t3 - t2) / query_pairs;

    // Benchmark Dijkstra queries (fewer pairs for large graphs)
    uint32_t dij_pairs = std::min(query_pairs, std::min(100u, query_pairs));
    if (graph.node_count() > 50000) dij_pairs = 10;

    auto t4 = Clock::now();
    for (uint32_t i = 0; i < dij_pairs; ++i) {
        dijkstra_pair(graph, pairs[i].first, pairs[i].second);
    }
    auto t5 = Clock::now();
    r.dijkstra_avg_us = to_us(t5 - t4) / dij_pairs;

    r.speedup = r.dijkstra_avg_us / r.ch_query_avg_us;

    return r;
}

void print_header() {
    std::cout << std::left
              << std::setw(10) << "Nodes"
              << std::setw(12) << "Edges"
              << std::setw(14) << "CH Build(ms)"
              << std::setw(14) << "CH Query(us)"
              << std::setw(14) << "Dijkstra(us)"
              << std::setw(10) << "Speedup"
              << std::setw(12) << "CH Mem(MB)"
              << std::setw(14) << "UnpackMap"
              << "\n";
    std::cout << std::string(100, '-') << "\n";
}

void print_row(const BenchResult& r) {
    std::cout << std::left << std::fixed
              << std::setw(10) << r.nodes
              << std::setw(12) << r.edges
              << std::setw(14) << std::setprecision(1) << r.ch_build_ms
              << std::setw(14) << std::setprecision(2) << r.ch_query_avg_us
              << std::setw(14) << std::setprecision(1) << r.dijkstra_avg_us
              << std::setw(10) << std::setprecision(0) << r.speedup << "x"
              << std::setw(12) << std::setprecision(2) << r.ch_memory_mb
              << std::setw(14) << r.unpack_map_entries
              << std::endl;
}

int main() {
    std::cout << "=== Gravel Performance Benchmark ===\n\n";

    // Random graph scaling
    std::cout << "--- Random Graph Scaling (edges ~= 4*nodes) ---\n";
    print_header();

    struct GraphSize { uint32_t nodes; uint32_t edges; };
    std::vector<GraphSize> sizes = {
        {1000, 4000},
        {5000, 20000},
        {10000, 40000},
        {25000, 100000},
        {50000, 200000},
    };

    for (const auto& [n, m] : sizes) {
        auto graph = make_random_graph(n, m, 1.0, 100.0, 42);
        auto r = bench_graph(*graph, 10000);
        print_row(r);
    }

    // Grid graph scaling
    std::cout << "\n--- Grid Graph Scaling ---\n";
    print_header();

    std::vector<uint32_t> grid_sizes = {32, 50, 100, 150, 200};
    for (uint32_t s : grid_sizes) {
        auto graph = make_grid_graph(s, s);
        auto r = bench_graph(*graph, 10000);
        print_row(r);
    }

    // Distance matrix benchmark
    std::cout << "\n--- Distance Matrix (100x100 on various graph sizes) ---\n";
    std::cout << std::left
              << std::setw(10) << "Nodes"
              << std::setw(14) << "Matrix(ms)"
              << std::setw(14) << "Per-pair(us)"
              << "\n";
    std::cout << std::string(38, '-') << "\n";

    for (const auto& [n, m] : sizes) {
        if (n > 100000) continue;
        auto graph = make_random_graph(n, m, 1.0, 100.0, 42);
        auto ch = build_ch(*graph);
        CHQuery query(ch);

        std::mt19937 rng(99);
        std::uniform_int_distribution<NodeID> dist(0, n - 1);
        std::vector<NodeID> origins(100), destinations(100);
        for (auto& o : origins) o = dist(rng);
        for (auto& d : destinations) d = dist(rng);

        auto t0 = Clock::now();
        auto matrix = query.distance_matrix(origins, destinations);
        auto t1 = Clock::now();

        double matrix_ms = to_ms(t1 - t0);
        double per_pair = to_us(t1 - t0) / (100.0 * 100.0);

        std::cout << std::left << std::fixed
                  << std::setw(10) << n
                  << std::setw(14) << std::setprecision(1) << matrix_ms
                  << std::setw(14) << std::setprecision(2) << per_pair
                  << std::endl;
    }

    // Route (with path unpacking) benchmark
    std::cout << "\n--- Route with Path Unpacking ---\n";
    std::cout << std::left
              << std::setw(10) << "Nodes"
              << std::setw(16) << "Distance(us)"
              << std::setw(16) << "Route(us)"
              << std::setw(14) << "Overhead"
              << "\n";
    std::cout << std::string(56, '-') << "\n";

    for (const auto& [n, m] : sizes) {
        if (n > 100000) continue;
        auto graph = make_random_graph(n, m, 1.0, 100.0, 42);
        auto ch = build_ch(*graph);
        CHQuery query(ch);

        std::mt19937 rng(42);
        std::uniform_int_distribution<NodeID> dist(0, n - 1);
        uint32_t pairs = 10000;

        std::vector<std::pair<NodeID, NodeID>> test_pairs(pairs);
        for (auto& [s, t] : test_pairs) {
            s = dist(rng);
            t = dist(rng);
        }

        auto t0 = Clock::now();
        for (const auto& [s, t] : test_pairs) {
            query.distance(s, t);
        }
        auto t1 = Clock::now();
        double dist_us = to_us(t1 - t0) / pairs;

        auto t2 = Clock::now();
        for (const auto& [s, t] : test_pairs) {
            query.route(s, t);
        }
        auto t3 = Clock::now();
        double route_us = to_us(t3 - t2) / pairs;

        std::cout << std::left << std::fixed
                  << std::setw(10) << n
                  << std::setw(16) << std::setprecision(2) << dist_us
                  << std::setw(16) << std::setprecision(2) << route_us
                  << std::setw(14) << std::setprecision(1) << (route_us / dist_us) << "x"
                  << std::endl;
    }

    std::cout << "\nDone." << std::endl;
    return 0;
}
