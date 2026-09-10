// ============================================================================
//  Unit tests for the graph layer.
// ============================================================================
#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

#include "daedalus/graph/Connectivity.hpp"
#include "daedalus/graph/Graph.hpp"
#include "daedalus/graph/MinimumSpanningTree.hpp"
#include "daedalus/graph/NetworkFlow.hpp"
#include "daedalus/graph/ShortestPath.hpp"
#include "daedalus/graph/Traversal.hpp"
#include "framework/TestFramework.hpp"

using namespace daedalus;

namespace {

using IntGraph = Graph<int, int>;

/// The worked example used across the shortest-path tests.
///
///        A --4-- B --8-- C
///        |     / |     / |
///        8   11  |    2  |
///        |  /    |  /    |
///        H --7-- G --6-- F --9-- E
///          \     |      /
///           1    2     10
///            \   |    /
///              I-- ... (see edges below)
IntGraph weightedRoadNetwork() {
    IntGraph graph(false);
    graph.addEdge(0, 1, 4);
    graph.addEdge(0, 7, 8);
    graph.addEdge(1, 2, 8);
    graph.addEdge(1, 7, 11);
    graph.addEdge(2, 3, 7);
    graph.addEdge(2, 8, 2);
    graph.addEdge(2, 5, 4);
    graph.addEdge(3, 4, 9);
    graph.addEdge(3, 5, 14);
    graph.addEdge(4, 5, 10);
    graph.addEdge(5, 6, 2);
    graph.addEdge(6, 7, 1);
    graph.addEdge(6, 8, 6);
    graph.addEdge(7, 8, 7);
    return graph;
}

}   // namespace

// ============================================================================
//  Graph container
// ============================================================================

DAEDALUS_TEST(Graph, add_vertices_and_edges) {
    Graph<std::string, double> graph(false);
    graph.addEdge("Delhi", "Mumbai", 1400.0);
    graph.addEdge("Mumbai", "Pune", 150.0);

    CHECK_EQ(graph.vertexCount(), 3u);
    CHECK_EQ(graph.edgeCount(), 2u);
    CHECK_TRUE(graph.hasVertex("Pune"));
    CHECK_TRUE(graph.hasEdge("Delhi", "Mumbai"));
    CHECK_TRUE(graph.hasEdge("Mumbai", "Delhi"));   // undirected: both ways
    CHECK_FALSE(graph.hasEdge("Delhi", "Pune"));
    CHECK_NEAR(graph.weight("Mumbai", "Pune").value(), 150.0, 1e-9);
    CHECK_FALSE(graph.weight("Delhi", "Pune").has_value());
}

DAEDALUS_TEST(Graph, directed_edges_only_go_one_way) {
    Graph<std::string, int> graph(true);
    graph.addEdge("a", "b", 1);
    CHECK_TRUE(graph.hasEdge("a", "b"));
    CHECK_FALSE(graph.hasEdge("b", "a"));
    CHECK_EQ(graph.edgeCount(), 1u);
    CHECK_EQ(graph.outDegree("a"), 1u);
    CHECK_EQ(graph.inDegree("a"), 0u);
    CHECK_EQ(graph.inDegree("b"), 1u);
}

DAEDALUS_TEST(Graph, remove_edge_and_missing_vertex_errors) {
    IntGraph graph(false);
    graph.addEdge(1, 2, 5);
    graph.addEdge(2, 3, 5);
    CHECK_TRUE(graph.removeEdge(1, 2));
    CHECK_FALSE(graph.hasEdge(2, 1));   // the mirror must go too
    CHECK_FALSE(graph.removeEdge(1, 2));
    CHECK_EQ(graph.edgeCount(), 1u);
    CHECK_THROWS_AS(graph.id(99), VertexNotFound);
    CHECK_THROWS_AS(graph.id(99), GraphError);
}

DAEDALUS_TEST(Graph, neighbours_degrees_and_reversal) {
    Graph<std::string, int> graph(true);
    graph.addEdge("a", "b", 1);
    graph.addEdge("a", "c", 1);
    graph.addEdge("b", "c", 1);

    CHECK_EQ(graph.neighbours("a"), (std::vector<std::string>{"b", "c"}));
    CHECK_EQ(graph.inDegrees(), (std::vector<std::size_t>{0, 1, 2}));

    const auto reversed = graph.reversed();
    CHECK_TRUE(reversed.hasEdge("b", "a"));
    CHECK_FALSE(reversed.hasEdge("a", "b"));
    CHECK_EQ(reversed.edgeCount(), 3u);
}

DAEDALUS_TEST(Graph, self_loop_and_clear) {
    IntGraph graph(false);
    graph.addEdge(1, 1, 3);
    CHECK_EQ(graph.vertexCount(), 1u);
    CHECK_TRUE(graph.hasEdge(1, 1));
    graph.clear();
    CHECK_TRUE(graph.empty());
    CHECK_EQ(graph.edgeCount(), 0u);
}

// ============================================================================
//  Traversal
// ============================================================================

DAEDALUS_TEST(GraphTraversal, bfs_visits_in_layers) {
    IntGraph graph(false);
    graph.addEdge(0, 1);
    graph.addEdge(0, 2);
    graph.addEdge(1, 3);
    graph.addEdge(2, 3);
    graph.addEdge(3, 4);

    const auto result = breadthFirstSearch(graph, 0);
    CHECK_EQ(result.order, (std::vector<int>{0, 1, 2, 3, 4}));
    CHECK_EQ(result.distance[graph.id(4)], 3);
    CHECK_EQ(result.distance[graph.id(3)], 2);
    CHECK_EQ(pathTo(graph, result, 4), (std::vector<int>{0, 1, 3, 4}));
}

DAEDALUS_TEST(GraphTraversal, bfs_marks_unreachable_vertices) {
    IntGraph graph(false);
    graph.addEdge(0, 1);
    graph.addVertex(9);
    const auto result = breadthFirstSearch(graph, 0);
    CHECK_FALSE(result.reached(graph.id(9)));
    CHECK_EQ(result.distance[graph.id(9)], -1);
    CHECK_EQ(pathTo(graph, result, 9), (std::vector<int>{}));
}

DAEDALUS_TEST(GraphTraversal, dfs_iterative_matches_recursive) {
    IntGraph graph(true);
    graph.addEdge(0, 1);
    graph.addEdge(0, 2);
    graph.addEdge(1, 3);
    graph.addEdge(2, 3);
    graph.addEdge(3, 4);

    const auto iterative = depthFirstSearch(graph, 0);
    const auto recursive = depthFirstSearchRecursive(graph, 0);
    CHECK_EQ(iterative.order, recursive);
    CHECK_EQ(iterative.order, (std::vector<int>{0, 1, 3, 4, 2}));
}

DAEDALUS_TEST(GraphTraversal, iterative_dfs_survives_a_very_deep_graph) {
    // 200k vertices in a single chain. A recursive DFS would blow the stack;
    // this is exactly why the iterative version exists.
    IntGraph chain(true);
    constexpr int kLength = 200000;
    for (int i = 0; i + 1 < kLength; ++i) chain.addEdge(i, i + 1);

    const auto result = depthFirstSearch(chain, 0);
    CHECK_EQ(result.order.size(), static_cast<std::size_t>(kLength));
    CHECK_EQ(result.order.back(), kLength - 1);
}

DAEDALUS_TEST(GraphTraversal, connected_components) {
    IntGraph graph(false);
    graph.addEdge(0, 1);
    graph.addEdge(1, 2);
    graph.addEdge(5, 6);
    graph.addVertex(9);

    const auto components = connectedComponents(graph);
    CHECK_EQ(components.size(), 3u);
    CHECK_FALSE(isConnected(graph));

    IntGraph joined(false);
    joined.addEdge(0, 1);
    joined.addEdge(1, 2);
    CHECK_TRUE(isConnected(joined));
}

DAEDALUS_TEST(GraphTraversal, bipartite_detection) {
    IntGraph evenCycle(false);
    for (int i = 0; i < 6; ++i) evenCycle.addEdge(i, (i + 1) % 6);
    const auto even = checkBipartite(evenCycle);
    CHECK_TRUE(even.bipartite);
    CHECK_EQ(even.partitionA.size(), 3u);
    CHECK_EQ(even.partitionB.size(), 3u);

    IntGraph oddCycle(false);
    for (int i = 0; i < 5; ++i) oddCycle.addEdge(i, (i + 1) % 5);
    CHECK_FALSE(checkBipartite(oddCycle).bipartite);   // odd cycle: never bipartite
}

DAEDALUS_TEST(GraphTraversal, cycle_detection_undirected_and_directed) {
    IntGraph tree(false);
    tree.addEdge(0, 1);
    tree.addEdge(0, 2);
    tree.addEdge(1, 3);
    CHECK_FALSE(hasCycle(tree));
    tree.addEdge(2, 3);
    CHECK_TRUE(hasCycle(tree));

    IntGraph dag(true);
    dag.addEdge(0, 1);
    dag.addEdge(1, 2);
    dag.addEdge(0, 2);   // a diamond is NOT a directed cycle
    CHECK_FALSE(hasCycle(dag));
    dag.addEdge(2, 0);
    CHECK_TRUE(hasCycle(dag));
}

DAEDALUS_TEST(GraphTraversal, topological_sort_of_a_dependency_graph) {
    Graph<std::string, int> build(true);
    build.addEdge("core", "linear");
    build.addEdge("core", "trees");
    build.addEdge("linear", "graph");
    build.addEdge("trees", "graph");
    build.addEdge("graph", "cli");

    const auto order = topologicalSort(build).value();
    CHECK_EQ(order.size(), 5u);

    // Every edge must run forwards in the emitted order.
    for (const auto& edge : build.labelledEdges()) {
        const auto from = std::find(order.begin(), order.end(), edge.from);
        const auto to = std::find(order.begin(), order.end(), edge.to);
        CHECK_TRUE(from < to);
    }
    CHECK_EQ(order.front(), std::string("core"));
    CHECK_EQ(order.back(), std::string("cli"));
}

DAEDALUS_TEST(GraphTraversal, topological_sort_reports_cycles) {
    Graph<std::string, int> cyclic(true);
    cyclic.addEdge("a", "b");
    cyclic.addEdge("b", "c");
    cyclic.addEdge("c", "a");
    CHECK_FALSE(topologicalSort(cyclic).has_value());
    CHECK_FALSE(topologicalSortByDfs(cyclic).has_value());

    Graph<std::string, int> undirected(false);
    undirected.addEdge("a", "b");
    CHECK_THROWS_AS(topologicalSort(undirected), GraphError);
}

DAEDALUS_TEST(GraphTraversal, both_topological_orders_are_valid) {
    Graph<int, int> dag(true);
    dag.addEdge(5, 11);
    dag.addEdge(7, 11);
    dag.addEdge(7, 8);
    dag.addEdge(3, 8);
    dag.addEdge(11, 2);
    dag.addEdge(11, 9);
    dag.addEdge(8, 9);

    for (const auto& order : {topologicalSort(dag).value(), topologicalSortByDfs(dag).value()}) {
        CHECK_EQ(order.size(), dag.vertexCount());
        for (const auto& edge : dag.labelledEdges()) {
            CHECK_TRUE(std::find(order.begin(), order.end(), edge.from) <
                       std::find(order.begin(), order.end(), edge.to));
        }
    }
}

// ============================================================================
//  Shortest paths
// ============================================================================

DAEDALUS_TEST(ShortestPath, dijkstra_on_the_classic_network) {
    const IntGraph graph = weightedRoadNetwork();
    const auto result = dijkstra(graph, 0);
    // The textbook answer for this graph.
    CHECK_EQ(result.distance[graph.id(0)], 0);
    CHECK_EQ(result.distance[graph.id(1)], 4);
    CHECK_EQ(result.distance[graph.id(7)], 8);
    CHECK_EQ(result.distance[graph.id(6)], 9);
    CHECK_EQ(result.distance[graph.id(5)], 11);
    CHECK_EQ(result.distance[graph.id(2)], 12);
    CHECK_EQ(result.distance[graph.id(8)], 14);
    CHECK_EQ(result.distance[graph.id(3)], 19);
    CHECK_EQ(result.distance[graph.id(4)], 21);
    CHECK_EQ(reconstructPath(graph, result, 4), (std::vector<int>{0, 7, 6, 5, 4}));
}

DAEDALUS_TEST(ShortestPath, dijkstra_rejects_negative_weights) {
    IntGraph graph(true);
    graph.addEdge(0, 1, 5);
    graph.addEdge(1, 2, -3);
    CHECK_THROWS_AS(dijkstra(graph, 0), GraphError);
}

DAEDALUS_TEST(ShortestPath, dijkstra_marks_unreachable) {
    IntGraph graph(true);
    graph.addEdge(0, 1, 5);
    graph.addVertex(9);
    const auto result = dijkstra(graph, 0);
    CHECK_FALSE(result.reachable(graph.id(9)));
    CHECK_EQ(reconstructPath(graph, result, 9), (std::vector<int>{}));
}

DAEDALUS_TEST(ShortestPath, bellman_ford_handles_negative_edges) {
    IntGraph graph(true);
    graph.addEdge(0, 1, 4);
    graph.addEdge(0, 2, 5);
    graph.addEdge(1, 3, 3);
    graph.addEdge(2, 1, -3);   // going the long way round is cheaper
    graph.addEdge(3, 4, 2);

    const auto result = bellmanFord(graph, 0);
    CHECK_FALSE(result.negativeCycle);
    CHECK_EQ(result.distance[graph.id(1)], 2);   // 0 -> 2 -> 1 costs 5 - 3
    CHECK_EQ(result.distance[graph.id(3)], 5);
    CHECK_EQ(result.distance[graph.id(4)], 7);
    CHECK_EQ(reconstructPath(graph, result, 4), (std::vector<int>{0, 2, 1, 3, 4}));
}

DAEDALUS_TEST(ShortestPath, bellman_ford_detects_a_negative_cycle) {
    IntGraph graph(true);
    graph.addEdge(0, 1, 1);
    graph.addEdge(1, 2, -1);
    graph.addEdge(2, 3, -1);
    graph.addEdge(3, 1, -1);   // loop with total weight -3
    CHECK_TRUE(bellmanFord(graph, 0).negativeCycle);
}

DAEDALUS_TEST(ShortestPath, bellman_ford_agrees_with_dijkstra) {
    const IntGraph graph = weightedRoadNetwork();
    const auto fast = dijkstra(graph, 0);
    const auto slow = bellmanFord(graph, 0);
    CHECK_EQ(fast.distance, slow.distance);
}

DAEDALUS_TEST(ShortestPath, zero_one_bfs) {
    IntGraph graph(true);
    graph.addEdge(0, 1, 0);
    graph.addEdge(1, 2, 1);
    graph.addEdge(0, 2, 1);
    graph.addEdge(2, 3, 0);
    graph.addEdge(3, 4, 1);

    const auto result = zeroOneBfs(graph, 0);
    CHECK_EQ(result.distance[graph.id(1)], 0);
    CHECK_EQ(result.distance[graph.id(2)], 1);
    CHECK_EQ(result.distance[graph.id(3)], 1);
    CHECK_EQ(result.distance[graph.id(4)], 2);
    CHECK_EQ(result.distance, dijkstra(graph, 0).distance);

    IntGraph heavy(true);
    heavy.addEdge(0, 1, 5);
    CHECK_THROWS_AS(zeroOneBfs(heavy, 0), GraphError);
}

DAEDALUS_TEST(ShortestPath, a_star_with_a_zero_heuristic_equals_dijkstra) {
    const IntGraph graph = weightedRoadNetwork();
    const std::function<int(const int&)> zero = [](const int&) { return 0; };
    const auto astar = aStar(graph, 0, 4, zero);
    const auto plain = dijkstra(graph, 0);
    CHECK_EQ(astar.distance[graph.id(4)], plain.distance[graph.id(4)]);
    CHECK_EQ(reconstructPath(graph, astar, 4), (std::vector<int>{0, 7, 6, 5, 4}));
}

DAEDALUS_TEST(ShortestPath, a_star_on_a_grid_beats_dijkstra_on_work_done) {
    // 40x40 grid of unit-cost right/down moves; Manhattan distance is an exact
    // admissible heuristic. The goal sits at (20,20), NOT the far corner: only
    // then does part of the grid lie off every optimal route, which is the
    // region A* is able to skip. With the goal at the opposite corner every
    // vertex lies on some optimal path and A* provably cannot prune anything.
    constexpr int kSize = 40;
    constexpr int kGoalRow = 20;
    constexpr int kGoalColumn = 20;

    Graph<int, int> grid(true);
    const auto id = [](int row, int column) { return row * kSize + column; };
    for (int row = 0; row < kSize; ++row) {
        for (int column = 0; column < kSize; ++column) {
            if (row + 1 < kSize) grid.addEdge(id(row, column), id(row + 1, column), 1);
            if (column + 1 < kSize) grid.addEdge(id(row, column), id(row, column + 1), 1);
        }
    }

    const int goal = id(kGoalRow, kGoalColumn);
    const std::function<int(const int&)> manhattan = [&](const int& vertex) {
        const int row = vertex / kSize;
        const int column = vertex % kSize;
        // Moves only ever increase row and column, so a vertex past the goal
        // can never reach it. Report infinity-ish to keep the estimate valid.
        if (row > kGoalRow || column > kGoalColumn) return kSize * kSize;
        return (kGoalRow - row) + (kGoalColumn - column);
    };

    const auto astar = aStar(grid, id(0, 0), goal, manhattan);
    const auto plain = dijkstra(grid, id(0, 0));

    // Same answer...
    CHECK_EQ(astar.distance[grid.id(goal)], plain.distance[grid.id(goal)]);
    CHECK_EQ(astar.distance[grid.id(goal)], kGoalRow + kGoalColumn);
    CHECK_EQ(reconstructPath(grid, astar, goal).size(),
             static_cast<std::size_t>(kGoalRow + kGoalColumn + 1));

    // ...for a fraction of the work. Dijkstra settles the whole grid.
    CHECK_EQ(plain.settledCount, static_cast<std::size_t>(kSize * kSize));
    CHECK_LT(astar.settledCount, plain.settledCount);
    // The reachable quadrant is 21x21 = 441 vertices; A* stays inside it.
    CHECK_LE(astar.settledCount, static_cast<std::size_t>((kGoalRow + 1) * (kGoalColumn + 1)));
}

DAEDALUS_TEST(ShortestPath, floyd_warshall_all_pairs) {
    const IntGraph graph = weightedRoadNetwork();
    const auto all = floydWarshall(graph);
    const auto single = dijkstra(graph, 0);
    CHECK_FALSE(all.negativeCycle);

    for (std::size_t target = 0; target < graph.vertexCount(); ++target) {
        CHECK_EQ(all.distance[graph.id(0)][target], single.distance[target]);
    }
    CHECK_EQ(reconstructPath(graph, all, 0, 4), (std::vector<int>{0, 7, 6, 5, 4}));
    CHECK_EQ(all.distance[graph.id(4)][graph.id(0)], 21);   // undirected: symmetric
}

DAEDALUS_TEST(ShortestPath, floyd_warshall_detects_negative_cycles) {
    IntGraph graph(true);
    graph.addEdge(0, 1, 1);
    graph.addEdge(1, 0, -3);
    CHECK_TRUE(floydWarshall(graph).negativeCycle);
}

DAEDALUS_TEST(ShortestPath, johnson_matches_floyd_warshall) {
    IntGraph graph(true);
    graph.addEdge(0, 1, 4);
    graph.addEdge(0, 2, 5);
    graph.addEdge(1, 3, 3);
    graph.addEdge(2, 1, -3);
    graph.addEdge(3, 4, 2);
    graph.addEdge(4, 0, 6);

    const auto johnsonResult = johnson(graph).value();
    const auto floyd = floydWarshall(graph);
    CHECK_FALSE(floyd.negativeCycle);
    for (std::size_t from = 0; from < graph.vertexCount(); ++from) {
        for (std::size_t to = 0; to < graph.vertexCount(); ++to) {
            CHECK_EQ(johnsonResult.distance[from][to], floyd.distance[from][to]);
        }
    }
}

DAEDALUS_TEST(ShortestPath, johnson_rejects_negative_cycles) {
    IntGraph graph(true);
    graph.addEdge(0, 1, 1);
    graph.addEdge(1, 2, -2);
    graph.addEdge(2, 0, -2);
    CHECK_FALSE(johnson(graph).has_value());
}

DAEDALUS_TEST(ShortestPath, all_algorithms_agree_on_random_graphs) {
    std::mt19937 rng(20260910u);
    std::uniform_int_distribution<int> weights(1, 20);

    for (int trial = 0; trial < 20; ++trial) {
        constexpr int kVertices = 25;
        IntGraph graph(true);
        for (int v = 0; v < kVertices; ++v) graph.addVertex(v);
        for (int from = 0; from < kVertices; ++from) {
            for (int to = 0; to < kVertices; ++to) {
                if (from != to && (rng() % 5) == 0) graph.addEdge(from, to, weights(rng));
            }
        }

        const auto viaDijkstra = dijkstra(graph, 0);
        const auto viaBellmanFord = bellmanFord(graph, 0);
        const auto viaFloyd = floydWarshall(graph);
        for (std::size_t v = 0; v < graph.vertexCount(); ++v) {
            CHECK_EQ(viaDijkstra.distance[v], viaBellmanFord.distance[v]);
            CHECK_EQ(viaDijkstra.distance[v], viaFloyd.distance[graph.id(0)][v]);
        }
    }
}

// ============================================================================
//  Minimum spanning tree
// ============================================================================

DAEDALUS_TEST(MinimumSpanningTree, kruskal_and_prim_agree) {
    const IntGraph graph = weightedRoadNetwork();
    const auto viaKruskal = kruskal(graph);
    const auto viaPrim = prim(graph);

    CHECK_EQ(viaKruskal.totalWeight, viaPrim.totalWeight);
    CHECK_EQ(viaKruskal.edges.size(), graph.vertexCount() - 1);
    CHECK_EQ(viaPrim.edges.size(), graph.vertexCount() - 1);
    CHECK_TRUE(viaKruskal.spansEveryVertex);
    CHECK_TRUE(viaPrim.spansEveryVertex);
    CHECK_EQ(viaKruskal.totalWeight, 37);   // the known answer for this graph
}

DAEDALUS_TEST(MinimumSpanningTree, handles_a_disconnected_graph) {
    IntGraph graph(false);
    graph.addEdge(0, 1, 1);
    graph.addEdge(2, 3, 1);

    const auto forest = kruskal(graph);
    CHECK_FALSE(forest.spansEveryVertex);
    CHECK_EQ(forest.componentCount, 2u);
    CHECK_EQ(forest.edges.size(), 2u);
    CHECK_FALSE(prim(graph, 0).spansEveryVertex);
}

DAEDALUS_TEST(MinimumSpanningTree, rejects_directed_graphs) {
    IntGraph directed(true);
    directed.addEdge(0, 1, 1);
    CHECK_THROWS_AS(kruskal(directed), GraphError);
    CHECK_THROWS_AS(prim(directed, 0), GraphError);
}

DAEDALUS_TEST(MinimumSpanningTree, agree_on_random_graphs) {
    std::mt19937 rng(4242u);
    std::uniform_int_distribution<int> weights(1, 100);

    for (int trial = 0; trial < 25; ++trial) {
        constexpr int kVertices = 30;
        IntGraph graph(false);
        for (int v = 0; v < kVertices; ++v) graph.addVertex(v);
        // A spanning path first, so the graph is always connected.
        for (int v = 1; v < kVertices; ++v) graph.addEdge(v - 1, v, weights(rng));
        for (int extra = 0; extra < 60; ++extra) {
            const int from = static_cast<int>(rng() % kVertices);
            const int to = static_cast<int>(rng() % kVertices);
            if (from != to && !graph.hasEdge(from, to)) graph.addEdge(from, to, weights(rng));
        }
        CHECK_EQ(kruskal(graph).totalWeight, prim(graph).totalWeight);
    }
}

DAEDALUS_TEST(MinimumSpanningTree, empty_graph_is_trivially_spanned) {
    IntGraph empty(false);
    CHECK_TRUE(kruskal(empty).spansEveryVertex);
    CHECK_TRUE(prim(empty).spansEveryVertex);
    CHECK_EQ(kruskal(empty).edges.size(), 0u);
}

// ============================================================================
//  Connectivity
// ============================================================================

DAEDALUS_TEST(Connectivity, tarjan_and_kosaraju_find_the_same_components) {
    IntGraph graph(true);
    graph.addEdge(0, 1);
    graph.addEdge(1, 2);
    graph.addEdge(2, 0);   // {0,1,2}
    graph.addEdge(2, 3);
    graph.addEdge(3, 4);
    graph.addEdge(4, 3);   // {3,4}
    graph.addEdge(4, 5);   // {5}

    const auto tarjan = tarjanStronglyConnectedComponents(graph);
    const auto kosaraju = kosarajuStronglyConnectedComponents(graph);
    CHECK_EQ(tarjan, kosaraju);
    CHECK_EQ(tarjan.size(), 3u);
    CHECK_EQ(tarjan[0], (std::vector<int>{0, 1, 2}));
    CHECK_EQ(tarjan[1], (std::vector<int>{3, 4}));
    CHECK_EQ(tarjan[2], (std::vector<int>{5}));
}

DAEDALUS_TEST(Connectivity, scc_algorithms_agree_on_random_digraphs) {
    std::mt19937 rng(31415u);
    for (int trial = 0; trial < 30; ++trial) {
        constexpr int kVertices = 20;
        IntGraph graph(true);
        for (int v = 0; v < kVertices; ++v) graph.addVertex(v);
        for (int from = 0; from < kVertices; ++from) {
            for (int to = 0; to < kVertices; ++to) {
                if (from != to && (rng() % 6) == 0) graph.addEdge(from, to);
            }
        }
        CHECK_EQ(tarjanStronglyConnectedComponents(graph),
                 kosarajuStronglyConnectedComponents(graph));
    }
}

DAEDALUS_TEST(Connectivity, condensation_is_acyclic) {
    IntGraph graph(true);
    graph.addEdge(0, 1);
    graph.addEdge(1, 0);
    graph.addEdge(1, 2);
    graph.addEdge(2, 3);
    graph.addEdge(3, 2);

    const auto dag = condensation(graph);
    CHECK_EQ(dag.vertexCount(), 2u);
    CHECK_EQ(dag.edgeCount(), 1u);
    CHECK_FALSE(hasCycle(dag));
    CHECK_TRUE(topologicalSort(dag).has_value());
}

DAEDALUS_TEST(Connectivity, bridges_in_a_barbell) {
    // Two triangles joined by a single edge: only that edge is a bridge.
    IntGraph graph(false);
    graph.addEdge(0, 1);
    graph.addEdge(1, 2);
    graph.addEdge(2, 0);
    graph.addEdge(2, 3);   // the bridge
    graph.addEdge(3, 4);
    graph.addEdge(4, 5);
    graph.addEdge(5, 3);

    const auto bridges = findBridges(graph);
    CHECK_EQ(bridges.size(), 1u);
    CHECK_EQ(bridges[0].from, 2);
    CHECK_EQ(bridges[0].to, 3);
}

DAEDALUS_TEST(Connectivity, every_edge_of_a_tree_is_a_bridge) {
    IntGraph tree(false);
    tree.addEdge(0, 1);
    tree.addEdge(1, 2);
    tree.addEdge(1, 3);
    CHECK_EQ(findBridges(tree).size(), 3u);

    IntGraph cycle(false);
    for (int i = 0; i < 4; ++i) cycle.addEdge(i, (i + 1) % 4);
    CHECK_EQ(findBridges(cycle).size(), 0u);   // a cycle has no bridge
}

DAEDALUS_TEST(Connectivity, articulation_points) {
    IntGraph graph(false);
    graph.addEdge(0, 1);
    graph.addEdge(1, 2);
    graph.addEdge(2, 0);
    graph.addEdge(2, 3);
    graph.addEdge(3, 4);
    graph.addEdge(4, 5);
    graph.addEdge(5, 3);

    CHECK_EQ(findArticulationPoints(graph), (std::vector<int>{2, 3}));

    IntGraph path(false);
    path.addEdge(0, 1);
    path.addEdge(1, 2);
    CHECK_EQ(findArticulationPoints(path), (std::vector<int>{1}));

    IntGraph cycle(false);
    for (int i = 0; i < 5; ++i) cycle.addEdge(i, (i + 1) % 5);
    CHECK_EQ(findArticulationPoints(cycle).size(), 0u);
}

DAEDALUS_TEST(Connectivity, undirected_only_routines_reject_digraphs) {
    IntGraph directed(true);
    directed.addEdge(0, 1);
    CHECK_THROWS_AS(findBridges(directed), GraphError);
    CHECK_THROWS_AS(findArticulationPoints(directed), GraphError);
}

// ============================================================================
//  Network flow
// ============================================================================

DAEDALUS_TEST(NetworkFlow, edmonds_karp_on_the_clrs_network) {
    Graph<std::string, int> network(true);
    network.addEdge("s", "v1", 16);
    network.addEdge("s", "v2", 13);
    network.addEdge("v1", "v3", 12);
    network.addEdge("v2", "v1", 4);
    network.addEdge("v2", "v4", 14);
    network.addEdge("v3", "v2", 9);
    network.addEdge("v3", "t", 20);
    network.addEdge("v4", "v3", 7);
    network.addEdge("v4", "t", 4);

    const auto result = edmondsKarp(network, "s", "t");
    CHECK_EQ(result.maxFlow, 23);          // the known answer
    CHECK_EQ(result.minCutCapacity, 23);   // max-flow min-cut theorem
}

DAEDALUS_TEST(NetworkFlow, dinic_agrees_with_edmonds_karp) {
    Graph<std::string, int> network(true);
    network.addEdge("s", "a", 10);
    network.addEdge("s", "b", 10);
    network.addEdge("a", "b", 2);
    network.addEdge("a", "c", 4);
    network.addEdge("a", "d", 8);
    network.addEdge("b", "d", 9);
    network.addEdge("d", "c", 6);
    network.addEdge("c", "t", 10);
    network.addEdge("d", "t", 10);

    CHECK_EQ(dinic(network, "s", "t").maxFlow, edmondsKarp(network, "s", "t").maxFlow);
    CHECK_EQ(dinic(network, "s", "t").maxFlow, 19);
}

DAEDALUS_TEST(NetworkFlow, min_cut_separates_source_from_sink) {
    Graph<std::string, int> network(true);
    network.addEdge("s", "a", 3);
    network.addEdge("a", "t", 2);   // the bottleneck

    const auto result = minimumCut(network, "s", "t");
    CHECK_EQ(result.maxFlow, 2);
    CHECK_EQ(result.minCutCapacity, 2);
    CHECK_EQ(result.minCutEdges.size(), 1u);
    CHECK_EQ(result.minCutEdges[0].from, std::string("a"));
    CHECK_EQ(result.minCutEdges[0].to, std::string("t"));
    // The source side must contain s and exclude t.
    CHECK_TRUE(std::find(result.sourceSide.begin(), result.sourceSide.end(), "s") !=
               result.sourceSide.end());
    CHECK_TRUE(std::find(result.sourceSide.begin(), result.sourceSide.end(), "t") ==
               result.sourceSide.end());
}

DAEDALUS_TEST(NetworkFlow, both_algorithms_agree_on_random_networks) {
    std::mt19937 rng(987654u);
    std::uniform_int_distribution<int> capacities(1, 25);

    for (int trial = 0; trial < 25; ++trial) {
        constexpr int kVertices = 14;
        IntGraph network(true);
        for (int v = 0; v < kVertices; ++v) network.addVertex(v);
        for (int from = 0; from < kVertices; ++from) {
            for (int to = from + 1; to < kVertices; ++to) {
                if ((rng() % 3) == 0) network.addEdge(from, to, capacities(rng));
            }
        }
        const auto viaEdmondsKarp = edmondsKarp(network, 0, kVertices - 1);
        const auto viaDinic = dinic(network, 0, kVertices - 1);
        CHECK_EQ(viaEdmondsKarp.maxFlow, viaDinic.maxFlow);
        CHECK_EQ(viaDinic.maxFlow, viaDinic.minCutCapacity);
    }
}

DAEDALUS_TEST(NetworkFlow, source_equal_to_sink_carries_no_flow) {
    Graph<std::string, int> network(true);
    network.addEdge("s", "t", 5);
    CHECK_EQ(edmondsKarp(network, "s", "s").maxFlow, 0);
    CHECK_EQ(dinic(network, "t", "t").maxFlow, 0);
}

DAEDALUS_TEST(Matching, maximum_bipartite_matching) {
    // Applicants on one side, jobs on the other.
    Graph<std::string, int> graph(false);
    graph.addEdge("ann", "backend");
    graph.addEdge("ann", "frontend");
    graph.addEdge("bob", "backend");
    graph.addEdge("cara", "data");
    graph.addEdge("dan", "data");

    const auto matching = maximumBipartiteMatching(graph);
    CHECK_EQ(matching.size, 3u);   // one of ann/bob gets backend, other frontend
    // No job or applicant may appear twice.
    std::vector<std::string> seen;
    for (const auto& pair : matching.pairs) {
        seen.push_back(pair.first);
        seen.push_back(pair.second);
    }
    std::sort(seen.begin(), seen.end());
    CHECK_TRUE(std::unique(seen.begin(), seen.end()) == seen.end());
}

DAEDALUS_TEST(Matching, rejects_non_bipartite_graphs) {
    IntGraph triangle(false);
    for (int i = 0; i < 3; ++i) triangle.addEdge(i, (i + 1) % 3);
    CHECK_THROWS_AS(maximumBipartiteMatching(triangle), GraphError);
}

DAEDALUS_TEST(Matching, perfect_matching_on_a_complete_bipartite_graph) {
    Graph<std::string, int> graph(false);
    for (int left = 0; left < 5; ++left) {
        for (int right = 0; right < 5; ++right) {
            graph.addEdge("L" + std::to_string(left), "R" + std::to_string(right));
        }
    }
    CHECK_EQ(maximumBipartiteMatching(graph).size, 5u);
}
