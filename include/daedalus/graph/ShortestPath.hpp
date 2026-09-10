// ============================================================================
//  Daedalus :: graph/ShortestPath.hpp
//
//  Five shortest-path algorithms, because there is no single best one -- the
//  right choice depends entirely on the graph:
//
//    dijkstra          O((V + E) log V)   non-negative weights, one source
//    bellmanFord       O(V * E)           tolerates negative edges, DETECTS
//                                         negative cycles
//    zeroOneBfs        O(V + E)           weights restricted to 0 and 1
//    aStar             O((V + E) log V)   Dijkstra plus an admissible
//                                         heuristic, expands far fewer nodes
//    floydWarshall     O(V^3)             all pairs, dense graphs
//    johnson           O(V*E + V*E log V) all pairs, SPARSE graphs with
//                                         negative edges
//
//  Dijkstra uses lazy deletion: instead of decrease-key it pushes a second
//  entry and discards stale pops. That keeps the heap a plain binary heap and
//  is what almost every real implementation does.
//
//  Unreachable vertices carry `infinity()`, never a silently wrong zero.
// ============================================================================
#ifndef DAEDALUS_GRAPH_SHORTEST_PATH_HPP
#define DAEDALUS_GRAPH_SHORTEST_PATH_HPP

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <type_traits>
#include <vector>

#include "daedalus/graph/Graph.hpp"
#include "daedalus/linear/Deque.hpp"
#include "daedalus/trees/BinaryHeap.hpp"

namespace daedalus {

/// The sentinel used for "no path". Chosen so that `d + w` cannot silently wrap
/// -- callers must check reachability before adding.
template <typename W>
[[nodiscard]] constexpr W pathInfinity() {
    return std::numeric_limits<W>::max() / 2;
}

template <typename V, typename W>
struct ShortestPathResult {
    std::vector<W> distance;
    std::vector<VertexId> predecessor;
    bool negativeCycle{false};

    /// Vertices finalised by the search. This is the honest measure of work
    /// done: it is what A* reduces relative to Dijkstra, and the benchmark and
    /// tests compare the two on exactly this number.
    std::size_t settledCount{0};

    static constexpr VertexId kNoParent = static_cast<VertexId>(-1);

    [[nodiscard]] bool reachable(VertexId vertex) const {
        return vertex < distance.size() && distance[vertex] < pathInfinity<W>();
    }
};

/// Rebuilds the vertex sequence from source to `target`, or an empty vector if
/// no path exists.
template <typename V, typename W>
[[nodiscard]] std::vector<V> reconstructPath(const Graph<V, W>& graph,
                                             const ShortestPathResult<V, W>& result,
                                             const std::type_identity_t<V>& target) {
    const VertexId end = graph.id(target);
    if (!result.reachable(end)) return {};

    std::vector<V> path;
    for (VertexId at = end;; at = result.predecessor[at]) {
        path.push_back(graph.label(at));
        if (result.predecessor[at] == ShortestPathResult<V, W>::kNoParent) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

// ---------------------------------------------------------------------------

/// Dijkstra's algorithm. Requires non-negative weights: with a negative edge a
/// vertex could be improved after being finalised, which the algorithm never
/// revisits, so it would return a wrong answer rather than an error. This
/// implementation throws instead of lying.
template <typename V, typename W>
[[nodiscard]] ShortestPathResult<V, W> dijkstra(const Graph<V, W>& graph, const std::type_identity_t<V>& source) {
    const std::size_t n = graph.vertexCount();
    const VertexId start = graph.id(source);

    ShortestPathResult<V, W> result;
    result.distance.assign(n, pathInfinity<W>());
    result.predecessor.assign(n, ShortestPathResult<V, W>::kNoParent);
    result.distance[start] = W{0};

    using Entry = std::pair<W, VertexId>;
    MinHeap<Entry> frontier;
    frontier.push({W{0}, start});
    std::vector<bool> settled(n, false);

    while (!frontier.empty()) {
        const Entry top = frontier.pop();
        const VertexId current = top.second;
        if (settled[current]) continue;   // a stale entry from lazy deletion
        settled[current] = true;
        ++result.settledCount;

        for (const auto& edge : graph.outgoing(current)) {
            if (edge.weight < W{0}) {
                throw GraphError("dijkstra requires non-negative edge weights; use bellmanFord");
            }
            const W candidate = result.distance[current] + edge.weight;
            if (candidate < result.distance[edge.to]) {
                result.distance[edge.to] = candidate;
                result.predecessor[edge.to] = current;
                frontier.push({candidate, edge.to});
            }
        }
    }
    return result;
}

/// Bellman-Ford. Slower than Dijkstra but handles negative edges, and a V-th
/// round that still improves something proves a negative cycle exists.
template <typename V, typename W>
[[nodiscard]] ShortestPathResult<V, W> bellmanFord(const Graph<V, W>& graph, const std::type_identity_t<V>& source) {
    const std::size_t n = graph.vertexCount();
    const VertexId start = graph.id(source);

    ShortestPathResult<V, W> result;
    result.distance.assign(n, pathInfinity<W>());
    result.predecessor.assign(n, ShortestPathResult<V, W>::kNoParent);
    result.distance[start] = W{0};

    // Every edge, in both directions for an undirected graph.
    std::vector<Edge<W>> allEdges;
    for (VertexId v = 0; v < n; ++v) {
        for (const auto& edge : graph.outgoing(v)) allEdges.push_back(edge);
    }

    for (std::size_t round = 0; round + 1 < n; ++round) {
        bool improved = false;
        for (const auto& edge : allEdges) {
            if (result.distance[edge.from] >= pathInfinity<W>()) continue;
            const W candidate = result.distance[edge.from] + edge.weight;
            if (candidate < result.distance[edge.to]) {
                result.distance[edge.to] = candidate;
                result.predecessor[edge.to] = edge.from;
                improved = true;
            }
        }
        if (!improved) break;   // settled early, the common case
    }

    for (const auto& edge : allEdges) {
        if (result.distance[edge.from] >= pathInfinity<W>()) continue;
        if (result.distance[edge.from] + edge.weight < result.distance[edge.to]) {
            result.negativeCycle = true;
            break;
        }
    }
    return result;
}

/// 0-1 BFS. When every weight is 0 or 1 the frontier only ever holds two
/// distinct distances, so a deque replaces the heap: a zero-weight edge goes to
/// the front, a one-weight edge to the back. Linear time, no log factor.
template <typename V, typename W>
[[nodiscard]] ShortestPathResult<V, W> zeroOneBfs(const Graph<V, W>& graph, const std::type_identity_t<V>& source) {
    const std::size_t n = graph.vertexCount();
    const VertexId start = graph.id(source);

    ShortestPathResult<V, W> result;
    result.distance.assign(n, pathInfinity<W>());
    result.predecessor.assign(n, ShortestPathResult<V, W>::kNoParent);
    result.distance[start] = W{0};

    Deque<VertexId> frontier;
    frontier.pushBack(start);

    while (!frontier.empty()) {
        const VertexId current = frontier.popFront();
        for (const auto& edge : graph.outgoing(current)) {
            if (edge.weight != W{0} && edge.weight != W{1}) {
                throw GraphError("zeroOneBfs requires every edge weight to be 0 or 1");
            }
            const W candidate = result.distance[current] + edge.weight;
            if (candidate < result.distance[edge.to]) {
                result.distance[edge.to] = candidate;
                result.predecessor[edge.to] = current;
                if (edge.weight == W{0}) {
                    frontier.pushFront(edge.to);
                } else {
                    frontier.pushBack(edge.to);
                }
            }
        }
    }
    return result;
}

/// A* search. `heuristic(label)` must never overestimate the remaining cost to
/// the goal (admissibility) or the result can be wrong; with a heuristic that
/// returns zero this degenerates to exactly Dijkstra.
template <typename V, typename W>
[[nodiscard]] ShortestPathResult<V, W> aStar(const Graph<V, W>& graph, const std::type_identity_t<V>& source,
                                             const std::type_identity_t<V>& goal,
                                             const std::function<W(const V&)>& heuristic) {
    const std::size_t n = graph.vertexCount();
    const VertexId start = graph.id(source);
    const VertexId target = graph.id(goal);

    ShortestPathResult<V, W> result;
    result.distance.assign(n, pathInfinity<W>());
    result.predecessor.assign(n, ShortestPathResult<V, W>::kNoParent);
    result.distance[start] = W{0};

    using Entry = std::pair<W, VertexId>;   // (estimated total, vertex)
    MinHeap<Entry> frontier;
    frontier.push({heuristic(source), start});
    std::vector<bool> settled(n, false);

    while (!frontier.empty()) {
        const VertexId current = frontier.pop().second;
        if (settled[current]) continue;
        settled[current] = true;
        ++result.settledCount;
        if (current == target) break;   // the goal is settled, we are done

        for (const auto& edge : graph.outgoing(current)) {
            const W candidate = result.distance[current] + edge.weight;
            if (candidate < result.distance[edge.to]) {
                result.distance[edge.to] = candidate;
                result.predecessor[edge.to] = current;
                frontier.push({candidate + heuristic(graph.label(edge.to)), edge.to});
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------

/// All-pairs result: a distance matrix plus the next-hop matrix needed to walk
/// a path back out.
template <typename V, typename W>
struct AllPairsResult {
    std::vector<std::vector<W>> distance;
    std::vector<std::vector<VertexId>> next;
    bool negativeCycle{false};

    static constexpr VertexId kNoNext = static_cast<VertexId>(-1);

    [[nodiscard]] bool reachable(VertexId from, VertexId to) const {
        return distance[from][to] < pathInfinity<W>();
    }
};

/// Floyd-Warshall: for every intermediate vertex k, see whether routing through
/// it beats the direct best. Three nested loops, and the k loop must be the
/// outermost -- that ordering is the whole proof.
template <typename V, typename W>
[[nodiscard]] AllPairsResult<V, W> floydWarshall(const Graph<V, W>& graph) {
    const std::size_t n = graph.vertexCount();
    AllPairsResult<V, W> result;
    result.distance.assign(n, std::vector<W>(n, pathInfinity<W>()));
    result.next.assign(n, std::vector<VertexId>(n, AllPairsResult<V, W>::kNoNext));

    for (VertexId v = 0; v < n; ++v) {
        result.distance[v][v] = W{0};
        result.next[v][v] = v;
    }
    for (VertexId v = 0; v < n; ++v) {
        for (const auto& edge : graph.outgoing(v)) {
            if (edge.weight < result.distance[edge.from][edge.to]) {
                result.distance[edge.from][edge.to] = edge.weight;
                result.next[edge.from][edge.to] = edge.to;
            }
        }
    }

    for (VertexId k = 0; k < n; ++k) {
        for (VertexId i = 0; i < n; ++i) {
            if (result.distance[i][k] >= pathInfinity<W>()) continue;
            for (VertexId j = 0; j < n; ++j) {
                if (result.distance[k][j] >= pathInfinity<W>()) continue;
                const W candidate = result.distance[i][k] + result.distance[k][j];
                if (candidate < result.distance[i][j]) {
                    result.distance[i][j] = candidate;
                    result.next[i][j] = result.next[i][k];
                }
            }
        }
    }

    // A vertex that can reach itself at negative cost sits on a negative cycle.
    for (VertexId v = 0; v < n; ++v) {
        if (result.distance[v][v] < W{0}) {
            result.negativeCycle = true;
            break;
        }
    }
    return result;
}

/// Walks the next-hop matrix into an explicit vertex sequence.
template <typename V, typename W>
[[nodiscard]] std::vector<V> reconstructPath(const Graph<V, W>& graph,
                                             const AllPairsResult<V, W>& result, const std::type_identity_t<V>& from,
                                             const std::type_identity_t<V>& to) {
    VertexId current = graph.id(from);
    const VertexId target = graph.id(to);
    if (result.next[current][target] == AllPairsResult<V, W>::kNoNext) return {};

    std::vector<V> path{graph.label(current)};
    while (current != target) {
        current = result.next[current][target];
        if (current == AllPairsResult<V, W>::kNoNext) return {};
        path.push_back(graph.label(current));
    }
    return path;
}

/// Johnson's algorithm: reweight every edge with a Bellman-Ford potential so
/// all weights become non-negative, then run Dijkstra from each vertex. On a
/// sparse graph this beats Floyd-Warshall's V^3 while still allowing negative
/// edges. Returns nullopt when a negative cycle makes the problem ill-posed.
template <typename V, typename W>
[[nodiscard]] std::optional<AllPairsResult<V, W>> johnson(const Graph<V, W>& graph) {
    const std::size_t n = graph.vertexCount();
    if (n == 0) return AllPairsResult<V, W>{};

    // Bellman-Ford from a virtual vertex joined to everything at cost zero.
    // Simulated directly rather than by mutating the graph.
    std::vector<W> potential(n, W{0});
    std::vector<Edge<W>> allEdges;
    for (VertexId v = 0; v < n; ++v) {
        for (const auto& edge : graph.outgoing(v)) allEdges.push_back(edge);
    }
    for (std::size_t round = 0; round < n; ++round) {
        bool improved = false;
        for (const auto& edge : allEdges) {
            const W candidate = potential[edge.from] + edge.weight;
            if (candidate < potential[edge.to]) {
                potential[edge.to] = candidate;
                improved = true;
            }
        }
        if (!improved) break;
        if (round + 1 == n) return std::nullopt;   // still improving: negative cycle
    }

    // Reweighted graph: w'(u,v) = w(u,v) + h(u) - h(v), provably non-negative.
    Graph<V, W> reweighted(true);
    for (const V& label : graph.vertices()) reweighted.addVertex(label);
    for (const auto& edge : allEdges) {
        // Exact in integer arithmetic; the clamp only guards floating-point
        // weights, where the result can land a rounding error below zero and
        // trip Dijkstra's non-negativity check.
        const W adjusted = edge.weight + potential[edge.from] - potential[edge.to];
        reweighted.addEdge(graph.label(edge.from), graph.label(edge.to),
                           adjusted < W{0} ? W{0} : adjusted);
    }

    AllPairsResult<V, W> result;
    result.distance.assign(n, std::vector<W>(n, pathInfinity<W>()));
    result.next.assign(n, std::vector<VertexId>(n, AllPairsResult<V, W>::kNoNext));

    for (VertexId source = 0; source < n; ++source) {
        const auto single = dijkstra(reweighted, graph.label(source));
        for (VertexId target = 0; target < n; ++target) {
            if (!single.reachable(target)) continue;
            // Undo the reweighting to recover the true distance.
            result.distance[source][target] =
                single.distance[target] - potential[source] + potential[target];
        }
        // Next-hops from this source's shortest-path tree. Each vertex's first
        // hop is derived from its parent's, so the whole tree costs O(V) rather
        // than O(V) per target.
        std::vector<VertexId> firstHop(n, AllPairsResult<V, W>::kNoNext);
        firstHop[source] = source;
        std::vector<VertexId> chain;
        for (VertexId target = 0; target < n; ++target) {
            if (!single.reachable(target) ||
                firstHop[target] != AllPairsResult<V, W>::kNoNext) {
                continue;
            }
            chain.clear();
            VertexId at = target;
            while (at != source && firstHop[at] == AllPairsResult<V, W>::kNoNext) {
                chain.push_back(at);
                at = single.predecessor[at];
            }
            // Resolve top-down: every parent already has its first hop set.
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                const VertexId parent = single.predecessor[*it];
                firstHop[*it] = (parent == source) ? *it : firstHop[parent];
            }
        }
        for (VertexId target = 0; target < n; ++target) {
            result.next[source][target] = firstHop[target];
        }
    }
    return result;
}

}  // namespace daedalus

#endif  // DAEDALUS_GRAPH_SHORTEST_PATH_HPP
