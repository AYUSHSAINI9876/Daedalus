// ============================================================================
//  Daedalus :: trees/SegmentTree.hpp
//
//  Range query structures over a fixed-size array.
//
//    SegmentTree<T, Op>       point update, range query, any associative Op
//    LazySegmentTree<T, P>    range update, range query, via lazy propagation
//
//  The lazy variant is the interesting one: a range update stops at the O(log n)
//  nodes that exactly cover the range and leaves a pending delta there, pushing
//  it down only when a later query needs to look inside. Without that, a range
//  update would be O(n).
//
//  Both are iterative-free recursive implementations on a 4n array, which is
//  the standard safe size bound for a non-power-of-two n.
//
//  Complexity: build O(n) | query O(log n) | update O(log n) | space O(n)
// ============================================================================
#ifndef DAEDALUS_TREES_SEGMENT_TREE_HPP
#define DAEDALUS_TREES_SEGMENT_TREE_HPP

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <string>
#include <vector>

#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"

namespace daedalus {

// --- combining policies ------------------------------------------------------
//
// Each policy supplies the monoid (identity + combine) and states how a
// uniform "+delta on every element" update changes an aggregate over `length`
// elements. For a sum that scales with the length; for min/max it does not.

template <typename T>
struct SumPolicy {
    static constexpr const char* name = "sum";
    [[nodiscard]] static T identity() { return T{}; }
    [[nodiscard]] static T combine(const T& a, const T& b) { return a + b; }
    [[nodiscard]] static T applyDelta(const T& value, const T& delta, std::size_t length) {
        return value + delta * static_cast<T>(length);
    }
};

template <typename T>
struct MinPolicy {
    static constexpr const char* name = "min";
    [[nodiscard]] static T identity() { return std::numeric_limits<T>::max(); }
    [[nodiscard]] static T combine(const T& a, const T& b) { return a < b ? a : b; }
    [[nodiscard]] static T applyDelta(const T& value, const T& delta, std::size_t) {
        return value + delta;
    }
};

template <typename T>
struct MaxPolicy {
    static constexpr const char* name = "max";
    [[nodiscard]] static T identity() { return std::numeric_limits<T>::lowest(); }
    [[nodiscard]] static T combine(const T& a, const T& b) { return a < b ? b : a; }
    [[nodiscard]] static T applyDelta(const T& value, const T& delta, std::size_t) {
        return value + delta;
    }
};

// ---------------------------------------------------------------------------

/// Point update, range query.
template <typename T, typename Policy = SumPolicy<T>>
class SegmentTree {
public:
    using value_type = T;
    using size_type = std::size_t;

    SegmentTree() = default;

    explicit SegmentTree(const std::vector<T>& values) { assign(values); }

    /// Rebuilds the tree over `values`. O(n).
    void assign(const std::vector<T>& values) {
        count_ = values.size();
        tree_.assign(count_ == 0 ? 1 : 4 * count_, Policy::identity());
        if (count_ > 0) build(1, 0, count_ - 1, values);
    }

    [[nodiscard]] size_type size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] std::string name() const { return std::string("SegmentTree<") + Policy::name + ">"; }

    /// Sets position `index` to `value`.
    void update(size_type index, const T& value) {
        if (index >= count_) throw IndexOutOfRange(index, count_);
        update(1, 0, count_ - 1, index, value);
    }

    /// Aggregate over the inclusive range [left, right].
    [[nodiscard]] T query(size_type left, size_type right) const {
        if (count_ == 0) return Policy::identity();
        if (left > right) throw InvalidArgument("segment tree query range is inverted");
        if (right >= count_) throw IndexOutOfRange(right, count_);
        return query(1, 0, count_ - 1, left, right);
    }

    [[nodiscard]] T queryAll() const {
        return count_ == 0 ? Policy::identity() : query(0, count_ - 1);
    }

private:
    void build(size_type node, size_type low, size_type high, const std::vector<T>& values) {
        if (low == high) {
            tree_[node] = values[low];
            return;
        }
        const size_type middle = low + (high - low) / 2;
        build(2 * node, low, middle, values);
        build(2 * node + 1, middle + 1, high, values);
        tree_[node] = Policy::combine(tree_[2 * node], tree_[2 * node + 1]);
    }

    void update(size_type node, size_type low, size_type high, size_type index, const T& value) {
        if (low == high) {
            tree_[node] = value;
            return;
        }
        const size_type middle = low + (high - low) / 2;
        if (index <= middle) {
            update(2 * node, low, middle, index, value);
        } else {
            update(2 * node + 1, middle + 1, high, index, value);
        }
        tree_[node] = Policy::combine(tree_[2 * node], tree_[2 * node + 1]);
    }

    [[nodiscard]] T query(size_type node, size_type low, size_type high, size_type left,
                          size_type right) const {
        if (right < low || high < left) return Policy::identity();     // disjoint
        if (left <= low && high <= right) return tree_[node];          // fully covered
        const size_type middle = low + (high - low) / 2;
        return Policy::combine(query(2 * node, low, middle, left, right),
                               query(2 * node + 1, middle + 1, high, left, right));
    }

    std::vector<T> tree_;
    size_type count_{0};
};

// ---------------------------------------------------------------------------

/// Range update (add a delta to every element in a range) plus range query.
template <typename T, typename Policy = SumPolicy<T>>
class LazySegmentTree {
public:
    using value_type = T;
    using size_type = std::size_t;

    LazySegmentTree() = default;

    explicit LazySegmentTree(const std::vector<T>& values) { assign(values); }

    void assign(const std::vector<T>& values) {
        count_ = values.size();
        const size_type slots = count_ == 0 ? 1 : 4 * count_;
        tree_.assign(slots, Policy::identity());
        pending_.assign(slots, T{});
        if (count_ > 0) build(1, 0, count_ - 1, values);
    }

    [[nodiscard]] size_type size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] std::string name() const {
        return std::string("LazySegmentTree<") + Policy::name + ">";
    }

    /// Adds `delta` to every element in the inclusive range [left, right].
    void rangeAdd(size_type left, size_type right, const T& delta) {
        if (count_ == 0) return;
        if (left > right) throw InvalidArgument("segment tree update range is inverted");
        if (right >= count_) throw IndexOutOfRange(right, count_);
        rangeAdd(1, 0, count_ - 1, left, right, delta);
    }

    [[nodiscard]] T query(size_type left, size_type right) {
        if (count_ == 0) return Policy::identity();
        if (left > right) throw InvalidArgument("segment tree query range is inverted");
        if (right >= count_) throw IndexOutOfRange(right, count_);
        return query(1, 0, count_ - 1, left, right);
    }

    [[nodiscard]] T at(size_type index) { return query(index, index); }

private:
    void build(size_type node, size_type low, size_type high, const std::vector<T>& values) {
        if (low == high) {
            tree_[node] = values[low];
            return;
        }
        const size_type middle = low + (high - low) / 2;
        build(2 * node, low, middle, values);
        build(2 * node + 1, middle + 1, high, values);
        tree_[node] = Policy::combine(tree_[2 * node], tree_[2 * node + 1]);
    }

    /// Applies a stored delta to a node and defers it to the node's children.
    void applyTo(size_type node, size_type low, size_type high, const T& delta) {
        tree_[node] = Policy::applyDelta(tree_[node], delta, high - low + 1);
        if (low != high) {
            pending_[2 * node] += delta;
            pending_[2 * node + 1] += delta;
        }
    }

    void push(size_type node, size_type low, size_type high) {
        if (pending_[node] == T{}) return;
        applyTo(node, low, high, pending_[node]);
        pending_[node] = T{};
    }

    void rangeAdd(size_type node, size_type low, size_type high, size_type left, size_type right,
                  const T& delta) {
        push(node, low, high);
        if (right < low || high < left) return;
        if (left <= low && high <= right) {
            applyTo(node, low, high, delta);
            return;
        }
        const size_type middle = low + (high - low) / 2;
        rangeAdd(2 * node, low, middle, left, right, delta);
        rangeAdd(2 * node + 1, middle + 1, high, left, right, delta);
        push(2 * node, low, middle);
        push(2 * node + 1, middle + 1, high);
        tree_[node] = Policy::combine(tree_[2 * node], tree_[2 * node + 1]);
    }

    [[nodiscard]] T query(size_type node, size_type low, size_type high, size_type left,
                          size_type right) {
        push(node, low, high);
        if (right < low || high < left) return Policy::identity();
        if (left <= low && high <= right) return tree_[node];
        const size_type middle = low + (high - low) / 2;
        return Policy::combine(query(2 * node, low, middle, left, right),
                               query(2 * node + 1, middle + 1, high, left, right));
    }

    std::vector<T> tree_;
    std::vector<T> pending_;
    size_type count_{0};
};

}  // namespace daedalus

#endif  // DAEDALUS_TREES_SEGMENT_TREE_HPP
