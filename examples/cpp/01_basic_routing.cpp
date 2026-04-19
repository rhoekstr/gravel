// Basic routing example — build a graph, compute CH, query distances.
//
// Build: g++ -std=c++20 -I../../include 01_basic_routing.cpp \
//            -L../../build -lgravel-core -lgravel-ch -o basic_routing

#include <gravel/core/array_graph.h>
#include <gravel/validation/synthetic_graphs.h>
#include <gravel/ch/contraction.h>
#include <gravel/ch/ch_query.h>

#include <iostream>
#include <vector>

int main() {
    using namespace gravel;

    // Build a 20x20 grid graph
    std::cout << "Building 20x20 grid graph...\n";
    auto graph = make_grid_graph(20, 20);
    std::cout << "  " << graph->node_count() << " nodes, "
              << graph->edge_count() << " edges\n\n";

    // Build contraction hierarchy
    std::cout << "Building contraction hierarchy...\n";
    auto ch = build_ch(*graph);
    std::cout << "  Done\n\n";

    // Single-pair shortest path query
    CHQuery query(ch);

    NodeID source = 0;
    NodeID target = 399;

    std::cout << "Shortest path from " << source << " to " << target << ":\n";
    auto result = query.route(source, target);
    std::cout << "  Distance: " << result.distance << "\n";
    std::cout << "  Path length: " << result.path.size() << " nodes\n";

    // Distance matrix
    std::cout << "\nDistance matrix (3x3):\n";
    std::vector<NodeID> sources = {0, 200, 399};
    std::vector<NodeID> targets = {100, 250, 350};
    auto matrix = query.distance_matrix(sources, targets);

    for (size_t i = 0; i < sources.size(); ++i) {
        for (size_t j = 0; j < targets.size(); ++j) {
            std::cout << "  " << matrix[i * targets.size() + j];
        }
        std::cout << "\n";
    }

    return 0;
}
