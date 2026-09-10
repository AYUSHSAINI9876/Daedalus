// ============================================================================
//  Daedalus :: graph/Traversal.hpp
//
//  Breadth- and depth-first search and everything that falls straight out of
//  them: connected components, bipartite two-colouring, cycle detection and
//  topological ordering.
//
//  DFS is offered both recursively and iteratively. The iterative form is not
//  an academic exercise -- a recursive DFS on a million-vertex path graph
//  overflows the stack, and the tests here prove the iterative one does not.
//
//  Complexity: every routine below is O(V + E) time, O(V) space
// ============================================================================
#ifndef DAEDALUS_GRAPH_TRAVERSAL_HPP
#define DAEDALUS_GRAPH_TRAVERSAL_HPP

#include <algorithm>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <vector>

#include "daedalus/graph/Graph.hpp"
#include "daedalus/linear/Deque.hpp"
#include "daedalus/linear/DynamicArray.hpp"
#include "daedalus/trees/BinaryHeap.hpp"

namespace daedalus {

/// BFS output: visit order plus the tree it induces.
template <typename V>
struct TraversalResult {
    std::vector<V> order;                ///< vertices in the order visited
    std::vector<int> distance;           ///< hops from the source, -1 if unreached
    std::vector<VertexId> predecessor;   ///< BFS/DFS tree parent, kNoParent at roots

    static constexpr VertexId kNoParent = static_cast<VertexId>(-1);

    [[nodiscard]] bool reached(VertexId vertex) const {
        return vertex < distance.size() && distance[vertex] >= 0;
    }
};

/// Breadth-first search from `source`. Distances are in edges, so on an
/// unweighted graph this is already the shortest-path answer.
template <typename V, typename W>
[[nodiscard]] TraversalResult<V> breadthFirstSearch(const Graph<V, W>& graph,
                                                    const std::type_identity_t<V>& source) {
    const VertexId start = graph.id(source);
    const std::size_t n = graph.vertexCount();

    TraversalResult<V> result;
    result.distance.assign(n, -1);
    result.predecessor.assign(n, TraversalResult<V>::kNoParent);

    Deque<VertexId> queue;
    result.distance[start] = 0;
    queue.pushBack(start);

    while (!queue.empty()) {
        const VertexId current = queue.popFront();
        result.order.push_back(graph.label(current));
        for (const auto& edge : graph.outgoing(current)) {
            if (result.distance[edge.to] >= 0) continue;
            result.distance[edge.to] = result.distance[current] + 1;
            result.predecessor[edge.to] = current;
            queue.pushBack(edge.to);
        }
    }
    return result;
}

/// Depth-first search with an explicit stack. Children are pushed in reverse so
/// the visit order matches the recursive version exactly.
template <typename V, typename W>
[[nodiscard]] TraversalResult<V> depthFirstSearch(const Graph<V, W>& graph,
                                                  const std::type_identity_t<V>& source) {
    const VertexId start = graph.id(source);
    const std::size_t n = graph.vertexCount();

    TraversalResult<V> result;
    result.distance.assign(n, -1);
    result.predecessor.assign(n, TraversalResult<V>::kNoParent);

    std::vector<bool> visited(n, false);
    DynamicArray<VertexId> stack;
    stack.pushBack(start);
    result.distance[start] = 0;

    while (!stack.empty()) {
        const VertexId current = stack.popBack();
        if (visited[current]) continue;
        visited[current] = true;
        result.order.push_back(graph.label(current));

        const auto& neighbours = graph.outgoing(current);
        for (std::size_t i = neighbours.size(); i-- > 0;) {
            const VertexId next = neighbours[i].to;
            if (visited[next]) continue;
            if (result.distance[next] < 0) {
                result.distance[next] = result.distance[current] + 1;
                result.predecessor[next] = current;
            }
            stack.pushBack(next);
        }
    }
    return result;
}

/// Recursive DFS, kept alongside the iterative one so the two can be compared.
/// Only safe when the graph's depth is bounded -- see the class comment.
template <typename V, typename W>
[[nodiscard]] std::vector<V> depthFirstSearchRecursive(const Graph<V, W>& graph,
                                                       const std::type_identity_t<V>& source) {
    std::vector<bool> visited(graph.vertexCount(), false);
    std::vector<V> order;

    // A local lambda cannot recurse by name, so the helper is explicit.
    struct Walker {
        const Graph<V, W>& graph;
        std::vector<bool>& visited;
        std::vector<V>& order;

        void visit(VertexId current) {
            visited[current] = true;
            order.push_back(graph.label(current));
            for (const auto& edge : graph.outgoing(current)) {
                if (!visited[edge.to]) visit(edge.to);
            }
        }
    };

    Walker{graph, visited, order}.visit(graph.id(source));
    return order;
}

/// Reconstructs the path recorded in a traversal's predecessor array.
template <typename V, typename W>
[[nodiscard]] std::vector<V> pathTo(const Graph<V, W>& graph, const TraversalResult<V>& traversal,
                                    const std::type_identity_t<V>& target) {
    const VertexId end = graph.id(target);
    if (!traversal.reached(end)) return {};

    std::vector<V> path;
    for (VertexId at = end;; at = traversal.predecessor[at]) {
        path.push_back(graph.label(at));
        if (traversal.predecessor[at] == TraversalResult<V>::kNoParent) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

/// Connected components (undirected) or weakly connected groups reachable by
/// following outgoing edges (directed). Each component is a list of labels.
template <typename V, typename W>
[[nodiscard]] std::vector<std::vector<V>> connectedComponents(const Graph<V, W>& graph) {
    const std::size_t n = graph.vertexCount();
    std::vector<bool> visited(n, false);
    std::vector<std::vector<V>> components;

    for (VertexId start = 0; start < n; ++start) {
        if (visited[start]) continue;
        std::vector<V> component;
        DynamicArray<VertexId> stack;
        stack.pushBack(start);
        visited[start] = true;
        while (!stack.empty()) {
            const VertexId current = stack.popBack();
            component.push_back(graph.label(current));
            for (const auto& edge : graph.outgoing(current)) {
                if (visited[edge.to]) continue;
                visited[edge.to] = true;
                stack.pushBack(edge.to);
            }
        }
        components.push_back(std::move(component));
    }
    return components;
}

template <typename V, typename W>
[[nodiscard]] bool isConnected(const Graph<V, W>& graph) {
    return graph.vertexCount() <= 1 || connectedComponents(graph).size() == 1;
}

/// Two-colouring result. `colours` holds 0 or 1 per vertex when bipartite.
template <typename V>
struct BipartiteResult {
    bool bipartite{false};
    std::vector<int> colours;
    std::vector<V> partitionA;
    std::vector<V> partitionB;
};

/// A graph is bipartite exactly when it has no odd-length cycle, which a BFS
/// two-colouring detects: any edge joining two same-coloured vertices closes
/// an odd cycle.
template <typename V, typename W>
[[nodiscard]] BipartiteResult<V> checkBipartite(const Graph<V, W>& graph) {
    const std::size_t n = graph.vertexCount();
    BipartiteResult<V> result;
    result.colours.assign(n, -1);

    for (VertexId start = 0; start < n; ++start) {
        if (result.colours[start] != -1) continue;
        result.colours[start] = 0;
        Deque<VertexId> queue;
        queue.pushBack(start);
        while (!queue.empty()) {
            const VertexId current = queue.popFront();
            for (const auto& edge : graph.outgoing(current)) {
                if (result.colours[edge.to] == -1) {
                    result.colours[edge.to] = 1 - result.colours[current];
                    queue.pushBack(edge.to);
                } else if (result.colours[edge.to] == result.colours[current]) {
                    result.bipartite = false;
                    result.colours.assign(n, -1);
                    return result;
                }
            }
        }
    }

    result.bipartite = true;
    for (VertexId v = 0; v < n; ++v) {
        (result.colours[v] == 0 ? result.partitionA : result.partitionB).push_back(graph.label(v));
    }
    return result;
}

/// Cycle detection. Undirected graphs need the "ignore the edge we arrived on"
/// rule; directed graphs need the recursion-stack (grey/black) colouring, since
/// revisiting a finished vertex is fine but revisiting an in-progress one is a
/// back edge.
template <typename V, typename W>
[[nodiscard]] bool hasCycle(const Graph<V, W>& graph) {
    const std::size_t n = graph.vertexCount();

    if (!graph.directed()) {
        std::vector<bool> visited(n, false);
        for (VertexId start = 0; start < n; ++start) {
            if (visited[start]) continue;
            DynamicArray<std::pair<VertexId, VertexId>> stack;
            stack.pushBack({start, static_cast<VertexId>(-1)});
            visited[start] = true;
            while (!stack.empty()) {
                const auto entry = stack.popBack();
                for (const auto& edge : graph.outgoing(entry.first)) {
                    if (edge.to == entry.second) continue;   // the edge we came in on
                    if (visited[edge.to]) return true;
                    visited[edge.to] = true;
                    stack.pushBack({edge.to, entry.first});
                }
            }
        }
        return false;
    }

    enum class Mark { White, Grey, Black };
    std::vector<Mark> marks(n, Mark::White);

    struct Walker {
        const Graph<V, W>& graph;
        std::vector<Mark>& marks;

        bool visit(VertexId current) {
            marks[current] = Mark::Grey;
            for (const auto& edge : graph.outgoing(current)) {
                if (marks[edge.to] == Mark::Grey) return true;
                if (marks[edge.to] == Mark::White && visit(edge.to)) return true;
            }
            marks[current] = Mark::Black;
            return false;
        }
    };

    Walker walker{graph, marks};
    for (VertexId start = 0; start < n; ++start) {
        if (marks[start] == Mark::White && walker.visit(start)) return true;
    }
    return false;
}

/// Kahn's algorithm: repeatedly emit a vertex with no remaining incoming edges.
/// Returns nullopt when the graph has a cycle, because then no ordering exists.
/// Ties are broken by the smallest vertex index, making the output stable.
template <typename V, typename W>
[[nodiscard]] std::optional<std::vector<V>> topologicalSort(const Graph<V, W>& graph) {
    if (!graph.directed()) throw GraphError("topological sort requires a directed graph");

    const std::size_t n = graph.vertexCount();
    std::vector<std::size_t> remaining = graph.inDegrees();

    // A min-heap of ready vertices keeps the output deterministic (smallest
    // index first) without the O(V^2) rescan a plain list would need.
    MinHeap<VertexId> ready;
    for (VertexId v = 0; v < n; ++v) {
        if (remaining[v] == 0) ready.push(v);
    }

    std::vector<V> order;
    order.reserve(n);
    while (!ready.empty()) {
        const VertexId next = ready.pop();
        order.push_back(graph.label(next));
        for (const auto& edge : graph.outgoing(next)) {
            if (--remaining[edge.to] == 0) ready.push(edge.to);
        }
    }

    // Anything left has a non-zero in-degree, so it sits on a cycle.
    if (order.size() != n) return std::nullopt;
    return order;
}

/// Topological order from a DFS post-order, reversed. Same result set as Kahn's
/// but a different order, and it is the ordering Kosaraju's algorithm needs.
template <typename V, typename W>
[[nodiscard]] std::optional<std::vector<V>> topologicalSortByDfs(const Graph<V, W>& graph) {
    if (!graph.directed()) throw GraphError("topological sort requires a directed graph");
    if (hasCycle(graph)) return std::nullopt;

    const std::size_t n = graph.vertexCount();
    std::vector<bool> visited(n, false);
    std::vector<V> order;

    struct Walker {
        const Graph<V, W>& graph;
        std::vector<bool>& visited;
        std::vector<V>& order;

        void visit(VertexId current) {
            visited[current] = true;
            for (const auto& edge : graph.outgoing(current)) {
                if (!visited[edge.to]) visit(edge.to);
            }
            order.push_back(graph.label(current));   // post-order
        }
    };

    Walker walker{graph, visited, order};
    for (VertexId v = 0; v < n; ++v) {
        if (!visited[v]) walker.visit(v);
    }
    std::reverse(order.begin(), order.end());
    return order;
}

}   // namespace daedalus

#endif   // DAEDALUS_GRAPH_TRAVERSAL_HPP
