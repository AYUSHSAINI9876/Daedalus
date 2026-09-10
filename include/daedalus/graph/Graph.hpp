// ============================================================================
//  Daedalus :: graph/Graph.hpp
//
//  Adjacency-list graph, directed or undirected, weighted or not.
//
//  Vertices carry user-facing labels (strings, ints, whatever is Hashable) but
//  are stored internally as dense indices 0..n-1. That split is deliberate: the
//  algorithms in this directory then index straight into flat vectors for
//  distances and visited flags -- no hashing in the inner loop -- while callers
//  still say shortestPath(graph, "Delhi", "Mumbai").
//
//  An undirected edge is stored as the two opposite directed edges, so every
//  algorithm below sees one uniform representation.
//
//  Complexity: addEdge O(1) | hasEdge O(deg v) | neighbours O(1) to reach
//              space O(V + E) -- an adjacency matrix would be O(V^2)
// ============================================================================
#ifndef DAEDALUS_GRAPH_GRAPH_HPP
#define DAEDALUS_GRAPH_GRAPH_HPP

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"

namespace daedalus {

using VertexId = std::size_t;

/// A directed edge in index space.
template <typename W = double>
struct Edge {
    VertexId from{0};
    VertexId to{0};
    W weight{};

    Edge() = default;
    Edge(VertexId f, VertexId t, W w) : from(f), to(t), weight(w) {}

    bool operator==(const Edge& other) const {
        return from == other.from && to == other.to && weight == other.weight;
    }
};

/// An edge with the caller's labels restored, which is what results carry.
template <typename V, typename W>
struct LabelledEdge {
    V from;
    V to;
    W weight;

    bool operator==(const LabelledEdge& other) const {
        return from == other.from && to == other.to && weight == other.weight;
    }
};

template <typename V = std::string, typename W = double>
    requires Hashable<V> && Weight<W>
class Graph {
public:
    using vertex_type = V;
    using weight_type = W;
    using edge_type = Edge<W>;

    explicit Graph(bool directed = false) : directed_(directed) {}

    // --- topology ------------------------------------------------------------

    [[nodiscard]] bool directed() const noexcept { return directed_; }
    [[nodiscard]] std::size_t vertexCount() const noexcept { return labels_.size(); }

    /// Number of edges as the caller added them: an undirected edge counts once
    /// even though it is stored twice.
    [[nodiscard]] std::size_t edgeCount() const noexcept {
        return directed_ ? directedEdgeCount_ : directedEdgeCount_ / 2;
    }

    [[nodiscard]] bool empty() const noexcept { return labels_.empty(); }
    [[nodiscard]] std::string name() const { return directed_ ? "DiGraph" : "Graph"; }

    /// Adds a vertex if absent; returns its index either way.
    VertexId addVertex(const V& label) {
        const auto found = index_.find(label);
        if (found != index_.end()) return found->second;
        const VertexId id = labels_.size();
        labels_.push_back(label);
        adjacency_.emplace_back();
        index_.emplace(label, id);
        return id;
    }

    [[nodiscard]] bool hasVertex(const V& label) const {
        return index_.find(label) != index_.end();
    }

    /// Index of `label`; throws VertexNotFound when absent.
    [[nodiscard]] VertexId id(const V& label) const {
        const auto found = index_.find(label);
        if (found == index_.end()) throw VertexNotFound(formatElement(label));
        return found->second;
    }

    [[nodiscard]] const V& label(VertexId vertex) const {
        if (vertex >= labels_.size()) throw IndexOutOfRange(vertex, labels_.size());
        return labels_[vertex];
    }

    [[nodiscard]] const std::vector<V>& vertices() const noexcept { return labels_; }

    /// Adds an edge, creating either endpoint if needed. In an undirected graph
    /// the reverse edge is added too.
    void addEdge(const V& from, const V& to, W weight = W{1}) {
        const VertexId source = addVertex(from);
        const VertexId target = addVertex(to);
        adjacency_[source].emplace_back(source, target, weight);
        ++directedEdgeCount_;
        if (!directed_ && source != target) {
            adjacency_[target].emplace_back(target, source, weight);
            ++directedEdgeCount_;
        }
    }

    /// Removes the edge (and its mirror when undirected). Returns false when
    /// there was no such edge.
    bool removeEdge(const V& from, const V& to) {
        if (!hasVertex(from) || !hasVertex(to)) return false;
        const VertexId source = id(from);
        const VertexId target = id(to);
        const bool removed = removeDirected(source, target);
        if (removed && !directed_ && source != target) (void)removeDirected(target, source);
        return removed;
    }

    [[nodiscard]] bool hasEdge(const V& from, const V& to) const {
        if (!hasVertex(from) || !hasVertex(to)) return false;
        const VertexId target = id(to);
        for (const edge_type& edge : adjacency_[id(from)]) {
            if (edge.to == target) return true;
        }
        return false;
    }

    /// Weight of the edge, or nullopt when it does not exist.
    [[nodiscard]] std::optional<W> weight(const V& from, const V& to) const {
        if (!hasVertex(from) || !hasVertex(to)) return std::nullopt;
        const VertexId target = id(to);
        for (const edge_type& edge : adjacency_[id(from)]) {
            if (edge.to == target) return edge.weight;
        }
        return std::nullopt;
    }

    // --- adjacency access ----------------------------------------------------

    [[nodiscard]] const std::vector<edge_type>& outgoing(VertexId vertex) const {
        if (vertex >= adjacency_.size()) throw IndexOutOfRange(vertex, adjacency_.size());
        return adjacency_[vertex];
    }

    [[nodiscard]] const std::vector<edge_type>& outgoing(const V& label) const {
        return adjacency_[id(label)];
    }

    [[nodiscard]] std::vector<V> neighbours(const V& label) const {
        std::vector<V> out;
        for (const edge_type& edge : adjacency_[id(label)]) out.push_back(labels_[edge.to]);
        return out;
    }

    /// All edges once each (undirected edges are not duplicated).
    [[nodiscard]] std::vector<edge_type> edges() const {
        std::vector<edge_type> out;
        out.reserve(edgeCount());
        for (VertexId v = 0; v < adjacency_.size(); ++v) {
            for (const edge_type& edge : adjacency_[v]) {
                if (directed_ || edge.from <= edge.to) out.push_back(edge);
            }
        }
        return out;
    }

    [[nodiscard]] std::vector<LabelledEdge<V, W>> labelledEdges() const {
        std::vector<LabelledEdge<V, W>> out;
        for (const edge_type& edge : edges()) {
            out.push_back(LabelledEdge<V, W>{labels_[edge.from], labels_[edge.to], edge.weight});
        }
        return out;
    }

    // --- degrees -------------------------------------------------------------

    [[nodiscard]] std::size_t outDegree(const V& label) const {
        return adjacency_[id(label)].size();
    }

    [[nodiscard]] std::size_t inDegree(const V& label) const {
        const VertexId target = id(label);
        std::size_t count = 0;
        for (const auto& list : adjacency_) {
            for (const edge_type& edge : list) {
                if (edge.to == target) ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::size_t degree(const V& label) const {
        return directed_ ? outDegree(label) + inDegree(label) : outDegree(label);
    }

    /// In-degree of every vertex in one O(V + E) pass, which is what
    /// topological sorting needs.
    [[nodiscard]] std::vector<std::size_t> inDegrees() const {
        std::vector<std::size_t> counts(labels_.size(), 0);
        for (const auto& list : adjacency_) {
            for (const edge_type& edge : list) ++counts[edge.to];
        }
        return counts;
    }

    // --- derived graphs ------------------------------------------------------

    /// Same vertices, every edge flipped. Used by Kosaraju's SCC algorithm.
    [[nodiscard]] Graph reversed() const {
        Graph result(directed_);
        for (const V& vertexLabel : labels_) result.addVertex(vertexLabel);
        for (VertexId v = 0; v < adjacency_.size(); ++v) {
            for (const edge_type& edge : adjacency_[v]) {
                if (!directed_ && edge.from > edge.to) continue;
                result.addEdge(labels_[edge.to], labels_[edge.from], edge.weight);
            }
        }
        return result;
    }

    /// Dense weight matrix with `absent` in the non-edge slots, for
    /// Floyd-Warshall and for small-graph rendering.
    [[nodiscard]] std::vector<std::vector<W>> adjacencyMatrix(W absent) const {
        const std::size_t n = labels_.size();
        std::vector<std::vector<W>> matrix(n, std::vector<W>(n, absent));
        for (std::size_t v = 0; v < n; ++v) {
            matrix[v][v] = W{0};
            for (const edge_type& edge : adjacency_[v]) {
                if (edge.weight < matrix[edge.from][edge.to])
                    matrix[edge.from][edge.to] = edge.weight;
            }
        }
        return matrix;
    }

    void clear() {
        labels_.clear();
        adjacency_.clear();
        index_.clear();
        directedEdgeCount_ = 0;
    }

    [[nodiscard]] std::string toString() const {
        std::ostringstream os;
        os << name() << " V=" << vertexCount() << " E=" << edgeCount() << "\n";
        for (VertexId v = 0; v < adjacency_.size(); ++v) {
            os << "  " << formatElement(labels_[v]) << " -> ";
            bool first = true;
            for (const edge_type& edge : adjacency_[v]) {
                if (!first) os << ", ";
                os << formatElement(labels_[edge.to]) << "(" << edge.weight << ")";
                first = false;
            }
            os << "\n";
        }
        return os.str();
    }

private:
    bool removeDirected(VertexId from, VertexId to) {
        auto& list = adjacency_[from];
        const auto found = std::find_if(list.begin(), list.end(),
                                        [to](const edge_type& edge) { return edge.to == to; });
        if (found == list.end()) return false;
        list.erase(found);
        --directedEdgeCount_;
        return true;
    }

    bool directed_;
    std::vector<V> labels_;
    std::vector<std::vector<edge_type>> adjacency_;
    std::unordered_map<V, VertexId> index_;
    std::size_t directedEdgeCount_{0};
};

/// Convenience alias for the common case.
using DiGraph = Graph<std::string, double>;

}   // namespace daedalus

#endif   // DAEDALUS_GRAPH_GRAPH_HPP
