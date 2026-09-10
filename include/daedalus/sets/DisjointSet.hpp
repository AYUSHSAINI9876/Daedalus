// ============================================================================
//  Daedalus :: sets/DisjointSet.hpp
//
//  Union-find with both optimisations that matter: union by size, so the
//  shallower tree is always hung under the deeper one, and path compression, so
//  every find flattens the path it walked. Together they give an amortised cost
//  of O(alpha(n)) -- the inverse Ackermann function, which is below 5 for any n
//  that fits in memory, so effectively constant.
//
//  Either optimisation alone gives O(log n). Both together give near-constant,
//  and the test suite measures the resulting tree depth to show it.
//
//  This is what makes Kruskal's MST algorithm practical, and it also answers
//  dynamic connectivity queries in a streaming setting where a graph traversal
//  would have to start over each time.
//
//  Complexity: find/unite O(alpha(n)) amortised | space O(n)
// ============================================================================
#ifndef DAEDALUS_SETS_DISJOINT_SET_HPP
#define DAEDALUS_SETS_DISJOINT_SET_HPP

#include <cstddef>
#include <numeric>
#include <string>
#include <vector>

#include "daedalus/core/Exception.hpp"

namespace daedalus {

class DisjointSet {
public:
    using size_type = std::size_t;

    explicit DisjointSet(size_type elementCount)
        : parent_(elementCount), componentSize_(elementCount, 1), componentCount_(elementCount) {
        std::iota(parent_.begin(), parent_.end(), size_type{0});
    }

    [[nodiscard]] std::string name() const { return "DisjointSet"; }

    /// Total number of elements, connected or not.
    [[nodiscard]] size_type size() const noexcept { return parent_.size(); }

    /// Number of disjoint components remaining.
    [[nodiscard]] size_type componentCount() const noexcept { return componentCount_; }

    /// Representative of the component containing `element`, compressing the
    /// path on the way back out. Iterative so a deep chain cannot overflow the
    /// stack -- a recursive find is the usual textbook version and the usual
    /// production bug.
    [[nodiscard]] size_type find(size_type element) {
        checkBounds(element);
        size_type root = element;
        while (parent_[root] != root) root = parent_[root];
        while (parent_[element] != root) {
            const size_type next = parent_[element];
            parent_[element] = root;
            element = next;
        }
        return root;
    }

    /// Non-mutating lookup, for const contexts. O(depth), no compression.
    [[nodiscard]] size_type findWithoutCompression(size_type element) const {
        checkBounds(element);
        while (parent_[element] != element) element = parent_[element];
        return element;
    }

    /// Merges the two components. Returns false when they were already joined,
    /// which is exactly the cycle test Kruskal's algorithm needs.
    bool unite(size_type a, size_type b) {
        size_type rootA = find(a);
        size_type rootB = find(b);
        if (rootA == rootB) return false;

        // Union by size: hang the smaller tree under the larger.
        if (componentSize_[rootA] < componentSize_[rootB]) std::swap(rootA, rootB);
        parent_[rootB] = rootA;
        componentSize_[rootA] += componentSize_[rootB];
        --componentCount_;
        return true;
    }

    [[nodiscard]] bool connected(size_type a, size_type b) { return find(a) == find(b); }

    /// Size of the component containing `element`.
    [[nodiscard]] size_type componentSizeOf(size_type element) {
        return componentSize_[find(element)];
    }

    /// Size of the largest component.
    [[nodiscard]] size_type largestComponentSize() const {
        size_type largest = 0;
        for (size_type i = 0; i < parent_.size(); ++i) {
            if (parent_[i] == i && componentSize_[i] > largest) largest = componentSize_[i];
        }
        return largest;
    }

    /// Members of each component, grouped. Components appear in order of their
    /// smallest member, and members within a component are ascending.
    [[nodiscard]] std::vector<std::vector<size_type>> components() {
        std::vector<std::vector<size_type>> grouped;
        std::vector<size_type> slotOf(parent_.size(), kNoSlot);
        for (size_type i = 0; i < parent_.size(); ++i) {
            const size_type root = find(i);
            if (slotOf[root] == kNoSlot) {
                slotOf[root] = grouped.size();
                grouped.emplace_back();
            }
            grouped[slotOf[root]].push_back(i);
        }
        return grouped;
    }

    /// Deepest chain still present. With path compression this stays tiny; the
    /// tests assert on it to prove the optimisation is actually working.
    [[nodiscard]] size_type maximumDepth() const {
        size_type deepest = 0;
        for (size_type i = 0; i < parent_.size(); ++i) {
            size_type depth = 0;
            size_type node = i;
            while (parent_[node] != node) {
                node = parent_[node];
                ++depth;
            }
            if (depth > deepest) deepest = depth;
        }
        return deepest;
    }

    void reset() {
        std::iota(parent_.begin(), parent_.end(), size_type{0});
        componentSize_.assign(parent_.size(), 1);
        componentCount_ = parent_.size();
    }

private:
    static constexpr size_type kNoSlot = static_cast<size_type>(-1);

    void checkBounds(size_type element) const {
        if (element >= parent_.size()) throw IndexOutOfRange(element, parent_.size());
    }

    std::vector<size_type> parent_;
    std::vector<size_type> componentSize_;
    size_type componentCount_;
};

}   // namespace daedalus

#endif   // DAEDALUS_SETS_DISJOINT_SET_HPP
