// ============================================================================
//  Daedalus :: graph/Connectivity.hpp
//
//  Structural decomposition, all built on the same observation: during a DFS,
//  the earliest reachable ancestor of a subtree (its "low-link") tells you
//  whether removing the current vertex or edge would disconnect it.
//
//    tarjanStronglyConnectedComponents   one DFS pass, O(V + E)
//    kosarajuStronglyConnectedComponents two passes over the reversed graph,
//                                        conceptually simpler, also O(V + E)
//    findBridges                         edges whose removal disconnects
//    findArticulationPoints              vertices whose removal disconnects
//    condensation                        the DAG of strongly connected parts
//
//  Both SCC algorithms are provided because they are genuinely different
//  arguments for the same result, and comparing their output is a strong test.
// ============================================================================
#ifndef DAEDALUS_GRAPH_CONNECTIVITY_HPP
#define DAEDALUS_GRAPH_CONNECTIVITY_HPP

#include <algorithm>
#include <cstddef>
#include <vector>

#include "daedalus/graph/Graph.hpp"
#include "daedalus/linear/DynamicArray.hpp"

namespace daedalus {

namespace detail {

/// Sorts each group and then the groups themselves, so two algorithms that
/// discover the same partition in different orders compare equal.
template <typename V>
void canonicalise(std::vector<std::vector<V>>& groups) {
    for (auto& group : groups) std::sort(group.begin(), group.end());
    std::sort(groups.begin(), groups.end());
}

}   // namespace detail

/// Tarjan's algorithm: a single DFS maintaining a stack of vertices whose
/// component is not yet closed. A vertex whose low-link equals its own index is
/// the root of a component, and everything above it on the stack belongs to it.
template <typename V, typename W>
[[nodiscard]] std::vector<std::vector<V>> tarjanStronglyConnectedComponents(
    const Graph<V, W>& graph) {
    const std::size_t n = graph.vertexCount();
    constexpr int kUnvisited = -1;

    std::vector<int> index(n, kUnvisited);
    std::vector<int> lowLink(n, 0);
    std::vector<bool> onStack(n, false);
    DynamicArray<VertexId> stack;
    std::vector<std::vector<V>> components;
    int nextIndex = 0;

    struct Walker {
        const Graph<V, W>& graph;
        std::vector<int>& index;
        std::vector<int>& lowLink;
        std::vector<bool>& onStack;
        DynamicArray<VertexId>& stack;
        std::vector<std::vector<V>>& components;
        int& nextIndex;

        void visit(VertexId current) {
            index[current] = nextIndex;
            lowLink[current] = nextIndex;
            ++nextIndex;
            stack.pushBack(current);
            onStack[current] = true;

            for (const auto& edge : graph.outgoing(current)) {
                if (index[edge.to] == -1) {
                    visit(edge.to);
                    lowLink[current] = std::min(lowLink[current], lowLink[edge.to]);
                } else if (onStack[edge.to]) {
                    // A back edge into the current component, not a cross edge
                    // into a finished one -- that distinction is the algorithm.
                    lowLink[current] = std::min(lowLink[current], index[edge.to]);
                }
            }

            if (lowLink[current] != index[current]) return;

            std::vector<V> component;
            for (;;) {
                const VertexId member = stack.popBack();
                onStack[member] = false;
                component.push_back(graph.label(member));
                if (member == current) break;
            }
            components.push_back(std::move(component));
        }
    };

    Walker walker{graph, index, lowLink, onStack, stack, components, nextIndex};
    for (VertexId v = 0; v < n; ++v) {
        if (index[v] == kUnvisited) walker.visit(v);
    }

    detail::canonicalise(components);
    return components;
}

/// Kosaraju's algorithm: order vertices by DFS finish time, then DFS the
/// reversed graph in that order. Each tree of the second pass is one component.
template <typename V, typename W>
[[nodiscard]] std::vector<std::vector<V>> kosarajuStronglyConnectedComponents(
    const Graph<V, W>& graph) {
    const std::size_t n = graph.vertexCount();
    std::vector<bool> visited(n, false);
    std::vector<VertexId> finishOrder;
    finishOrder.reserve(n);

    struct Finisher {
        const Graph<V, W>& graph;
        std::vector<bool>& visited;
        std::vector<VertexId>& finishOrder;

        void visit(VertexId current) {
            visited[current] = true;
            for (const auto& edge : graph.outgoing(current)) {
                if (!visited[edge.to]) visit(edge.to);
            }
            finishOrder.push_back(current);
        }
    };

    Finisher finisher{graph, visited, finishOrder};
    for (VertexId v = 0; v < n; ++v) {
        if (!visited[v]) finisher.visit(v);
    }

    const Graph<V, W> reversed = graph.reversed();
    std::vector<bool> assigned(n, false);
    std::vector<std::vector<V>> components;

    for (std::size_t i = finishOrder.size(); i-- > 0;) {
        const VertexId start = finishOrder[i];
        if (assigned[start]) continue;

        std::vector<V> component;
        DynamicArray<VertexId> stack;
        stack.pushBack(start);
        assigned[start] = true;
        while (!stack.empty()) {
            const VertexId current = stack.popBack();
            component.push_back(graph.label(current));
            for (const auto& edge : reversed.outgoing(current)) {
                if (assigned[edge.to]) continue;
                assigned[edge.to] = true;
                stack.pushBack(edge.to);
            }
        }
        components.push_back(std::move(component));
    }

    detail::canonicalise(components);
    return components;
}

/// Contracts every strongly connected component to a single vertex. The result
/// is always a DAG -- that is the point, and it is how you run a topological
/// algorithm on a graph that has cycles.
template <typename V, typename W>
[[nodiscard]] Graph<std::string, W> condensation(const Graph<V, W>& graph) {
    const auto components = tarjanStronglyConnectedComponents(graph);

    std::vector<std::size_t> componentOf(graph.vertexCount(), 0);
    std::vector<std::string> names(components.size());
    for (std::size_t c = 0; c < components.size(); ++c) {
        std::string name = "{";
        for (std::size_t i = 0; i < components[c].size(); ++i) {
            if (i > 0) name += ",";
            name += formatElement(components[c][i]);
            componentOf[graph.id(components[c][i])] = c;
        }
        name += "}";
        names[c] = name;
    }

    Graph<std::string, W> dag(true);
    for (const std::string& name : names) dag.addVertex(name);
    for (VertexId v = 0; v < graph.vertexCount(); ++v) {
        for (const auto& edge : graph.outgoing(v)) {
            const std::size_t a = componentOf[edge.from];
            const std::size_t b = componentOf[edge.to];
            if (a != b && !dag.hasEdge(names[a], names[b])) {
                dag.addEdge(names[a], names[b], edge.weight);
            }
        }
    }
    return dag;
}

/// Bridges: undirected edges whose removal increases the component count. An
/// edge (u, v) is a bridge exactly when nothing in v's subtree can reach u or
/// above without using that edge.
template <typename V, typename W>
[[nodiscard]] std::vector<LabelledEdge<V, W>> findBridges(const Graph<V, W>& graph) {
    if (graph.directed()) throw GraphError("bridges are defined for undirected graphs");

    const std::size_t n = graph.vertexCount();
    std::vector<int> discovery(n, -1);
    std::vector<int> lowLink(n, 0);
    std::vector<LabelledEdge<V, W>> bridges;
    int timer = 0;

    struct Walker {
        const Graph<V, W>& graph;
        std::vector<int>& discovery;
        std::vector<int>& lowLink;
        std::vector<LabelledEdge<V, W>>& bridges;
        int& timer;

        void visit(VertexId current, VertexId parent) {
            discovery[current] = timer;
            lowLink[current] = timer;
            ++timer;
            bool skippedParentEdge = false;

            for (const auto& edge : graph.outgoing(current)) {
                if (edge.to == parent && !skippedParentEdge) {
                    // Skip the edge we arrived on exactly once, so a genuine
                    // parallel edge still counts as an alternative route.
                    skippedParentEdge = true;
                    continue;
                }
                if (discovery[edge.to] == -1) {
                    visit(edge.to, current);
                    lowLink[current] = std::min(lowLink[current], lowLink[edge.to]);
                    if (lowLink[edge.to] > discovery[current]) {
                        bridges.push_back(LabelledEdge<V, W>{graph.label(current),
                                                             graph.label(edge.to), edge.weight});
                    }
                } else {
                    lowLink[current] = std::min(lowLink[current], discovery[edge.to]);
                }
            }
        }
    };

    Walker walker{graph, discovery, lowLink, bridges, timer};
    for (VertexId v = 0; v < n; ++v) {
        if (discovery[v] == -1) walker.visit(v, static_cast<VertexId>(-1));
    }
    return bridges;
}

/// Articulation points (cut vertices): removing one disconnects the graph. The
/// DFS root is special -- it is a cut vertex only if it has two or more
/// children in the DFS tree.
template <typename V, typename W>
[[nodiscard]] std::vector<V> findArticulationPoints(const Graph<V, W>& graph) {
    if (graph.directed()) {
        throw GraphError("articulation points are defined for undirected graphs");
    }

    const std::size_t n = graph.vertexCount();
    std::vector<int> discovery(n, -1);
    std::vector<int> lowLink(n, 0);
    std::vector<bool> isCutVertex(n, false);
    int timer = 0;

    struct Walker {
        const Graph<V, W>& graph;
        std::vector<int>& discovery;
        std::vector<int>& lowLink;
        std::vector<bool>& isCutVertex;
        int& timer;

        void visit(VertexId current, VertexId parent, bool isRoot) {
            discovery[current] = timer;
            lowLink[current] = timer;
            ++timer;
            int children = 0;
            bool skippedParentEdge = false;

            for (const auto& edge : graph.outgoing(current)) {
                if (edge.to == parent && !skippedParentEdge) {
                    skippedParentEdge = true;
                    continue;
                }
                if (discovery[edge.to] == -1) {
                    ++children;
                    visit(edge.to, current, false);
                    lowLink[current] = std::min(lowLink[current], lowLink[edge.to]);
                    if (!isRoot && lowLink[edge.to] >= discovery[current]) {
                        isCutVertex[current] = true;
                    }
                } else {
                    lowLink[current] = std::min(lowLink[current], discovery[edge.to]);
                }
            }
            if (isRoot && children > 1) isCutVertex[current] = true;
        }
    };

    Walker walker{graph, discovery, lowLink, isCutVertex, timer};
    for (VertexId v = 0; v < n; ++v) {
        if (discovery[v] == -1) walker.visit(v, static_cast<VertexId>(-1), true);
    }

    std::vector<V> points;
    for (VertexId v = 0; v < n; ++v) {
        if (isCutVertex[v]) points.push_back(graph.label(v));
    }
    std::sort(points.begin(), points.end());
    return points;
}

}   // namespace daedalus

#endif   // DAEDALUS_GRAPH_CONNECTIVITY_HPP
