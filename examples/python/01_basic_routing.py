"""Basic routing example — build a graph, compute CH, query distances.

Run: python examples/python/01_basic_routing.py
"""

import gravel


def main():
    # Build a 20x20 grid graph (400 nodes)
    print("Building 20x20 grid graph...")
    graph = gravel.make_grid_graph(20, 20)
    print(f"  {graph.node_count:,} nodes, {graph.edge_count:,} edges\n")

    # Build contraction hierarchy (one-time cost)
    print("Building contraction hierarchy...")
    ch = gravel.build_ch(graph)
    print("  Done\n")

    # Single-pair shortest path query
    query = gravel.CHQuery(ch)

    source = 0       # top-left corner
    target = 399     # bottom-right corner

    print(f"Shortest path from node {source} to node {target}:")
    result = query.route(source, target)
    print(f"  Distance: {result.distance:.2f}")
    print(f"  Path length: {len(result.path)} nodes")
    print(f"  First 5 nodes: {list(result.path[:5])}")
    print(f"  Last 5 nodes: {list(result.path[-5:])}\n")

    # Distance matrix for multiple pairs
    print("Computing distance matrix (3 sources × 3 targets)...")
    sources = [0, 200, 399]
    targets = [100, 250, 350]
    matrix = query.distance_matrix(sources, targets)
    print(f"  {matrix}")


if __name__ == "__main__":
    main()
