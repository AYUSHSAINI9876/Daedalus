// ============================================================================
//  Daedalus :: graph/NetworkFlow.hpp
//
//  Maximum flow, minimum cut and bipartite matching.
//
//    edmondsKarp   BFS augmenting paths           O(V * E^2)
//    dinic         level graph + blocking flow    O(V^2 * E), and O(E * sqrt(V))
//                                                 on unit-capacity networks
//    minimumCut    falls out of a max flow by the max-flow min-cut theorem
//    maximumBipartiteMatching  Kuhn's algorithm   O(V * E)
//
//  The residual network is the central idea: every arc gets a paired reverse
//  arc of capacity zero, and pushing flow forward is the same operation as
//  cancelling flow backward. That is what lets a greedy path choice be undone
//  later, and it is why the algorithm is correct rather than merely plausible.
// ============================================================================
#ifndef DAEDALUS_GRAPH_NETWORK_FLOW_HPP
#define DAEDALUS_GRAPH_NETWORK_FLOW_HPP

#include <algorithm>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include "daedalus/graph/Graph.hpp"
#include "daedalus/graph/Traversal.hpp"
#include "daedalus/linear/Deque.hpp"

namespace daedalus {

template <typename V, typename W>
struct MaxFlowResult {
    W maxFlow{};
    std::vector<V> sourceSide;                     ///< vertices reachable in the residual graph
    std::vector<LabelledEdge<V, W>> minCutEdges;   ///< saturated edges crossing the cut
    W minCutCapacity{};
};

namespace detail {

/// Residual network. `reverse` is the index of the paired arc in the other
/// vertex's list, so cancelling flow is an O(1) lookup.
template <typename W>
class ResidualNetwork {
public:
    struct Arc {
        VertexId to{0};
        W capacity{};
        W flow{};
        std::size_t reverse{0};

        [[nodiscard]] W residual() const { return capacity - flow; }
    };

    explicit ResidualNetwork(std::size_t vertexCount) : adjacency_(vertexCount) {}

    void addArc(VertexId from, VertexId to, W capacity) {
        const std::size_t forwardIndex = adjacency_[from].size();
        const std::size_t backwardIndex = adjacency_[to].size();
        adjacency_[from].push_back(Arc{to, capacity, W{}, backwardIndex});
        adjacency_[to].push_back(Arc{from, W{}, W{}, forwardIndex});
    }

    [[nodiscard]] std::vector<Arc>& operator[](VertexId vertex) { return adjacency_[vertex]; }
    [[nodiscard]] const std::vector<Arc>& operator[](VertexId vertex) const {
        return adjacency_[vertex];
    }
    [[nodiscard]] std::size_t size() const noexcept { return adjacency_.size(); }

    void push(VertexId from, std::size_t arcIndex, W amount) {
        Arc& forward = adjacency_[from][arcIndex];
        forward.flow += amount;
        adjacency_[forward.to][forward.reverse].flow -= amount;
    }

    /// Vertices still reachable from `source` along arcs with spare capacity.
    /// After a maximum flow this set is exactly the source side of a minimum
    /// cut.
    [[nodiscard]] std::vector<bool> reachableFrom(VertexId source) const {
        std::vector<bool> seen(adjacency_.size(), false);
        Deque<VertexId> queue;
        seen[source] = true;
        queue.pushBack(source);
        while (!queue.empty()) {
            const VertexId current = queue.popFront();
            for (const Arc& arc : adjacency_[current]) {
                if (arc.residual() > W{0} && !seen[arc.to]) {
                    seen[arc.to] = true;
                    queue.pushBack(arc.to);
                }
            }
        }
        return seen;
    }

private:
    std::vector<std::vector<Arc>> adjacency_;
};

/// Builds the residual network for a graph. An undirected edge becomes a pair
/// of opposite arcs, each with the full capacity.
template <typename V, typename W>
[[nodiscard]] ResidualNetwork<W> buildResidual(const Graph<V, W>& graph) {
    ResidualNetwork<W> network(graph.vertexCount());
    for (VertexId v = 0; v < graph.vertexCount(); ++v) {
        for (const auto& edge : graph.outgoing(v)) {
            if (edge.weight < W{0}) throw GraphError("flow capacities must be non-negative");
            if (!graph.directed() && edge.from > edge.to) continue;   // added once already
            network.addArc(edge.from, edge.to, edge.weight);
            if (!graph.directed() && edge.from != edge.to) {
                network.addArc(edge.to, edge.from, edge.weight);
            }
        }
    }
    return network;
}

/// Fills in the min-cut half of a result from a saturated residual network.
template <typename V, typename W>
void describeMinimumCut(const Graph<V, W>& graph, const ResidualNetwork<W>& network,
                        VertexId source, MaxFlowResult<V, W>& result) {
    const std::vector<bool> onSourceSide = network.reachableFrom(source);
    for (VertexId v = 0; v < graph.vertexCount(); ++v) {
        if (onSourceSide[v]) result.sourceSide.push_back(graph.label(v));
    }
    for (VertexId v = 0; v < graph.vertexCount(); ++v) {
        if (!onSourceSide[v]) continue;
        for (const auto& edge : graph.outgoing(v)) {
            if (onSourceSide[edge.to]) continue;
            result.minCutEdges.push_back(
                LabelledEdge<V, W>{graph.label(edge.from), graph.label(edge.to), edge.weight});
            result.minCutCapacity += edge.weight;
        }
    }
}

}   // namespace detail

// ---------------------------------------------------------------------------

/// Edmonds-Karp: Ford-Fulkerson where the augmenting path is always the
/// shortest one. Choosing the shortest path is what bounds the iteration count
/// -- plain Ford-Fulkerson with a bad path choice can take capacity-many
/// rounds, or fail to terminate at all on irrational capacities.
template <typename V, typename W>
[[nodiscard]] MaxFlowResult<V, W> edmondsKarp(const Graph<V, W>& graph,
                                              const std::type_identity_t<V>& source,
                                              const std::type_identity_t<V>& sink) {
    const VertexId from = graph.id(source);
    const VertexId to = graph.id(sink);
    MaxFlowResult<V, W> result;
    if (from == to) return result;

    auto network = detail::buildResidual(graph);
    const std::size_t n = network.size();
    constexpr W kNoArc = static_cast<W>(0);
    (void)kNoArc;

    for (;;) {
        // BFS for a shortest augmenting path, recording the arc used to enter
        // each vertex so the path can be replayed.
        std::vector<VertexId> cameFrom(n, static_cast<VertexId>(-1));
        std::vector<std::size_t> cameVia(n, 0);
        std::vector<bool> seen(n, false);
        Deque<VertexId> queue;
        seen[from] = true;
        queue.pushBack(from);

        while (!queue.empty() && !seen[to]) {
            const VertexId current = queue.popFront();
            const auto& arcs = network[current];
            for (std::size_t i = 0; i < arcs.size(); ++i) {
                if (arcs[i].residual() <= W{0} || seen[arcs[i].to]) continue;
                seen[arcs[i].to] = true;
                cameFrom[arcs[i].to] = current;
                cameVia[arcs[i].to] = i;
                queue.pushBack(arcs[i].to);
            }
        }
        if (!seen[to]) break;   // no augmenting path remains: flow is maximum

        // Bottleneck along the path, then push that much through it.
        W bottleneck = std::numeric_limits<W>::max();
        for (VertexId at = to; at != from; at = cameFrom[at]) {
            const auto& arc = network[cameFrom[at]][cameVia[at]];
            bottleneck = std::min(bottleneck, arc.residual());
        }
        for (VertexId at = to; at != from; at = cameFrom[at]) {
            network.push(cameFrom[at], cameVia[at], bottleneck);
        }
        result.maxFlow += bottleneck;
    }

    detail::describeMinimumCut(graph, network, from, result);
    return result;
}

/// Dinic's algorithm. Each phase builds a level graph by BFS and then saturates
/// a blocking flow with DFS, advancing an iterator per vertex so no arc is
/// examined twice within a phase. The level graph's depth strictly increases
/// each phase, which bounds the number of phases at V.
template <typename V, typename W>
[[nodiscard]] MaxFlowResult<V, W> dinic(const Graph<V, W>& graph,
                                        const std::type_identity_t<V>& source,
                                        const std::type_identity_t<V>& sink) {
    const VertexId from = graph.id(source);
    const VertexId to = graph.id(sink);
    MaxFlowResult<V, W> result;
    if (from == to) return result;

    auto network = detail::buildResidual(graph);
    const std::size_t n = network.size();
    std::vector<int> level(n, -1);
    std::vector<std::size_t> nextArc(n, 0);

    // Depth-first search restricted to arcs that go exactly one level deeper.
    struct Blocking {
        detail::ResidualNetwork<W>& network;
        std::vector<int>& level;
        std::vector<std::size_t>& nextArc;
        VertexId sink;

        W augment(VertexId current, W limit) {
            if (current == sink || limit == W{0}) return limit;
            auto& arcs = network[current];
            for (std::size_t& i = nextArc[current]; i < arcs.size(); ++i) {
                auto& arc = arcs[i];
                if (arc.residual() <= W{0} || level[arc.to] != level[current] + 1) continue;
                const W pushed = augment(arc.to, std::min(limit, arc.residual()));
                if (pushed > W{0}) {
                    network.push(current, i, pushed);
                    return pushed;
                }
            }
            return W{0};
        }
    };

    Blocking blocking{network, level, nextArc, to};

    for (;;) {
        std::fill(level.begin(), level.end(), -1);
        Deque<VertexId> queue;
        level[from] = 0;
        queue.pushBack(from);
        while (!queue.empty()) {
            const VertexId current = queue.popFront();
            for (const auto& arc : network[current]) {
                if (arc.residual() > W{0} && level[arc.to] < 0) {
                    level[arc.to] = level[current] + 1;
                    queue.pushBack(arc.to);
                }
            }
        }
        if (level[to] < 0) break;   // sink unreachable: done

        std::fill(nextArc.begin(), nextArc.end(), std::size_t{0});
        for (;;) {
            const W pushed = blocking.augment(from, std::numeric_limits<W>::max());
            if (pushed <= W{0}) break;
            result.maxFlow += pushed;
        }
    }

    detail::describeMinimumCut(graph, network, from, result);
    return result;
}

/// Minimum cut, stated as its own function because that is usually the question
/// being asked. Equal in value to the maximum flow (Ford-Fulkerson theorem).
template <typename V, typename W>
[[nodiscard]] MaxFlowResult<V, W> minimumCut(const Graph<V, W>& graph,
                                             const std::type_identity_t<V>& source,
                                             const std::type_identity_t<V>& sink) {
    return dinic(graph, source, sink);
}

// ---------------------------------------------------------------------------

template <typename V>
struct MatchingResult {
    std::vector<std::pair<V, V>> pairs;
    std::size_t size{0};
};

/// Kuhn's algorithm for maximum bipartite matching: for each left vertex, try
/// to find an augmenting path that reshuffles existing matches to free a slot.
/// The graph must be bipartite; the two sides are discovered automatically.
template <typename V, typename W>
[[nodiscard]] MatchingResult<V> maximumBipartiteMatching(const Graph<V, W>& graph) {
    const auto colouring = checkBipartite(graph);
    if (!colouring.bipartite) {
        throw GraphError("maximum bipartite matching requires a bipartite graph");
    }

    const std::size_t n = graph.vertexCount();
    std::vector<VertexId> matchedWith(n, static_cast<VertexId>(-1));
    constexpr VertexId kUnmatched = static_cast<VertexId>(-1);

    struct Augmenter {
        const Graph<V, W>& graph;
        std::vector<VertexId>& matchedWith;
        std::vector<bool> seen;

        bool tryAssign(VertexId left) {
            for (const auto& edge : graph.outgoing(left)) {
                if (seen[edge.to]) continue;
                seen[edge.to] = true;
                if (matchedWith[edge.to] == static_cast<VertexId>(-1) ||
                    tryAssign(matchedWith[edge.to])) {
                    matchedWith[edge.to] = left;
                    matchedWith[left] = edge.to;
                    return true;
                }
            }
            return false;
        }
    };

    Augmenter augmenter{graph, matchedWith, std::vector<bool>(n, false)};
    MatchingResult<V> result;

    for (VertexId v = 0; v < n; ++v) {
        if (colouring.colours[v] != 0) continue;   // left side only
        if (matchedWith[v] != kUnmatched) continue;
        augmenter.seen.assign(n, false);
        (void)augmenter.tryAssign(v);
    }

    for (VertexId v = 0; v < n; ++v) {
        if (colouring.colours[v] != 0 || matchedWith[v] == kUnmatched) continue;
        result.pairs.push_back({graph.label(v), graph.label(matchedWith[v])});
    }
    result.size = result.pairs.size();
    return result;
}

}   // namespace daedalus

#endif   // DAEDALUS_GRAPH_NETWORK_FLOW_HPP
