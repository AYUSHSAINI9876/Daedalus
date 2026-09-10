// ============================================================================
//  Daedalus :: graph/MinimumSpanningTree.hpp
//
//  Kruskal and Prim. Both are greedy and both are provably optimal, but they
//  grow the tree from opposite directions:
//
//    Kruskal  sorts every edge and accepts one whenever its endpoints are in
//             different components -- a union-find query. Best on sparse
//             graphs.        O(E log E)
//
//    Prim     grows a single tree, repeatedly taking the cheapest edge leaving
//             it, using a heap as the frontier. Best on dense graphs.
//             O(E log V)
//
//  Both return a forest when the graph is disconnected, and say so, rather than
//  quietly returning a partial answer.
// ============================================================================
#ifndef DAEDALUS_GRAPH_MINIMUM_SPANNING_TREE_HPP
#define DAEDALUS_GRAPH_MINIMUM_SPANNING_TREE_HPP

#include <algorithm>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include "daedalus/graph/Graph.hpp"
#include "daedalus/sets/DisjointSet.hpp"
#include "daedalus/trees/BinaryHeap.hpp"

namespace daedalus {

template <typename V, typename W>
struct SpanningTreeResult {
    std::vector<LabelledEdge<V, W>> edges;
    W totalWeight{};
    bool spansEveryVertex{false};   ///< false means the graph was disconnected
    std::size_t componentCount{0};
};

/// Kruskal's algorithm. The union-find "were these already connected?" test is
/// exactly the cycle check, which is why the two structures belong together.
template <typename V, typename W>
[[nodiscard]] SpanningTreeResult<V, W> kruskal(const Graph<V, W>& graph) {
    if (graph.directed()) {
        throw GraphError("a minimum spanning tree is only defined for undirected graphs");
    }

    const std::size_t n = graph.vertexCount();
    SpanningTreeResult<V, W> result;
    result.componentCount = n;
    if (n == 0) {
        result.spansEveryVertex = true;
        return result;
    }

    std::vector<Edge<W>> candidates = graph.edges();
    std::sort(candidates.begin(), candidates.end(),
              [](const Edge<W>& a, const Edge<W>& b) { return a.weight < b.weight; });

    DisjointSet components(n);
    for (const Edge<W>& edge : candidates) {
        if (!components.unite(edge.from, edge.to)) continue;   // would close a cycle
        result.edges.push_back(
            LabelledEdge<V, W>{graph.label(edge.from), graph.label(edge.to), edge.weight});
        result.totalWeight += edge.weight;
        if (result.edges.size() == n - 1) break;   // tree is complete
    }

    result.componentCount = components.componentCount();
    result.spansEveryVertex = result.componentCount == 1;
    return result;
}

/// Prim's algorithm, started from `source` (or vertex 0 when omitted). Uses
/// lazy deletion in the heap, the same trick as Dijkstra.
template <typename V, typename W>
[[nodiscard]] SpanningTreeResult<V, W> prim(const Graph<V, W>& graph,
                                            const std::type_identity_t<V>& source) {
    if (graph.directed()) {
        throw GraphError("a minimum spanning tree is only defined for undirected graphs");
    }

    const std::size_t n = graph.vertexCount();
    SpanningTreeResult<V, W> result;
    if (n == 0) {
        result.spansEveryVertex = true;
        return result;
    }

    const VertexId start = graph.id(source);
    std::vector<bool> inTree(n, false);

    // (weight, to, from) so the heap orders by weight first.
    struct Candidate {
        W weight{};
        VertexId to{0};
        VertexId from{0};
        bool operator==(const Candidate& other) const {
            return weight == other.weight && to == other.to && from == other.from;
        }
    };
    struct Cheapest {
        bool operator()(const Candidate& a, const Candidate& b) const {
            if (b.weight < a.weight) return true;   // min-heap
            if (a.weight < b.weight) return false;
            return b.to < a.to;   // deterministic tie-break
        }
    };

    BinaryHeap<Candidate, Cheapest> frontier;
    inTree[start] = true;
    for (const auto& edge : graph.outgoing(start)) {
        frontier.push(Candidate{edge.weight, edge.to, start});
    }

    std::size_t attached = 1;
    while (!frontier.empty() && attached < n) {
        const Candidate best = frontier.pop();
        if (inTree[best.to]) continue;   // stale entry

        inTree[best.to] = true;
        ++attached;
        result.edges.push_back(
            LabelledEdge<V, W>{graph.label(best.from), graph.label(best.to), best.weight});
        result.totalWeight += best.weight;

        for (const auto& edge : graph.outgoing(best.to)) {
            if (!inTree[edge.to]) frontier.push(Candidate{edge.weight, edge.to, best.to});
        }
    }

    result.spansEveryVertex = attached == n;
    result.componentCount = result.spansEveryVertex ? 1 : n - result.edges.size();
    return result;
}

/// Prim from the graph's first vertex, for callers that do not care where it
/// starts (the resulting total weight is the same either way).
template <typename V, typename W>
[[nodiscard]] SpanningTreeResult<V, W> prim(const Graph<V, W>& graph) {
    if (graph.vertexCount() == 0) {
        SpanningTreeResult<V, W> empty;
        empty.spansEveryVertex = true;
        return empty;
    }
    return prim(graph, graph.label(0));
}

}   // namespace daedalus

#endif   // DAEDALUS_GRAPH_MINIMUM_SPANNING_TREE_HPP
