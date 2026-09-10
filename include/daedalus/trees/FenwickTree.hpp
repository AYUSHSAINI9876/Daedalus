// ============================================================================
//  Daedalus :: trees/FenwickTree.hpp
//
//  Binary indexed tree. Same asymptotics as a segment tree for prefix sums, but
//  one array, no recursion and a much smaller constant -- each index implicitly
//  owns a run of length equal to its lowest set bit, so walking the structure
//  is just `i += i & -i`.
//
//  Two variants:
//    FenwickTree        point update, prefix/range sum, plus an O(log n)
//                       search for the smallest prefix reaching a target
//    RangeFenwickTree   range update, range sum, built from two Fenwick trees
//
//  Complexity: update O(log n) | query O(log n) | space O(n) -- exactly n+1
// ============================================================================
#ifndef DAEDALUS_TREES_FENWICK_TREE_HPP
#define DAEDALUS_TREES_FENWICK_TREE_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "daedalus/core/Exception.hpp"

namespace daedalus {

template <typename T = long long>
class FenwickTree {
public:
    using value_type = T;
    using size_type = std::size_t;

    FenwickTree() = default;

    explicit FenwickTree(size_type count) : tree_(count + 1, T{}), count_(count) {}

    /// O(n) construction: fill the array, then propagate each slot to its
    /// parent once, instead of n separate O(log n) updates.
    explicit FenwickTree(const std::vector<T>& values)
        : tree_(values.size() + 1, T{}), count_(values.size()) {
        for (size_type i = 0; i < count_; ++i) tree_[i + 1] = values[i];
        for (size_type i = 1; i <= count_; ++i) {
            const size_type parent = i + (i & (~i + 1));
            if (parent <= count_) tree_[parent] += tree_[i];
        }
    }

    [[nodiscard]] size_type size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
    [[nodiscard]] std::string name() const { return "FenwickTree"; }

    void reset(size_type count) {
        tree_.assign(count + 1, T{});
        count_ = count;
    }

    /// Adds `delta` at position `index` (0-based).
    void add(size_type index, const T& delta) {
        if (index >= count_) throw IndexOutOfRange(index, count_);
        for (size_type i = index + 1; i <= count_; i += lowestBit(i)) tree_[i] += delta;
    }

    /// Sum of [0, index]. Returns zero for an empty prefix.
    [[nodiscard]] T prefixSum(size_type index) const {
        if (count_ == 0) return T{};
        if (index >= count_) throw IndexOutOfRange(index, count_);
        T total{};
        for (size_type i = index + 1; i > 0; i -= lowestBit(i)) total += tree_[i];
        return total;
    }

    /// Sum of the inclusive range [left, right].
    [[nodiscard]] T rangeSum(size_type left, size_type right) const {
        if (left > right) throw InvalidArgument("fenwick range is inverted");
        if (right >= count_) throw IndexOutOfRange(right, count_);
        T total = prefixSum(right);
        if (left > 0) total -= prefixSum(left - 1);
        return total;
    }

    [[nodiscard]] T at(size_type index) const { return rangeSum(index, index); }

    void set(size_type index, const T& value) { add(index, value - at(index)); }

    /// Smallest index whose prefix sum is at least `target`, or size() when no
    /// prefix reaches it. O(log n) descent -- requires non-negative elements.
    [[nodiscard]] size_type lowerBound(const T& target) const {
        size_type position = 0;
        T remaining = target;
        size_type step = highestPowerOfTwo(count_);
        while (step > 0) {
            const size_type candidate = position + step;
            if (candidate <= count_ && tree_[candidate] < remaining) {
                position = candidate;
                remaining -= tree_[candidate];
            }
            step /= 2;
        }
        return position;  // 0-based index of the first element reaching target
    }

private:
    [[nodiscard]] static size_type lowestBit(size_type i) noexcept { return i & (~i + 1); }

    [[nodiscard]] static size_type highestPowerOfTwo(size_type n) noexcept {
        size_type power = 1;
        while (power * 2 <= n) power *= 2;
        return n == 0 ? 0 : power;
    }

    std::vector<T> tree_;
    size_type count_{0};
};

// ---------------------------------------------------------------------------

/// Range update / range query, from the standard two-BIT decomposition:
/// prefixSum(i) = sum(B1, i) * (i + 1) - sum(B2, i).
template <typename T = long long>
class RangeFenwickTree {
public:
    using size_type = std::size_t;

    RangeFenwickTree() = default;

    explicit RangeFenwickTree(size_type count)
        : multiplier_(count), constant_(count), count_(count) {}

    [[nodiscard]] size_type size() const noexcept { return count_; }
    [[nodiscard]] std::string name() const { return "RangeFenwickTree"; }

    /// Adds `delta` to every element of the inclusive range [left, right].
    void rangeAdd(size_type left, size_type right, const T& delta) {
        if (left > right) throw InvalidArgument("fenwick range is inverted");
        if (right >= count_) throw IndexOutOfRange(right, count_);
        addSuffix(left, delta, -delta * static_cast<T>(left));
        if (right + 1 < count_) {
            addSuffix(right + 1, -delta, delta * static_cast<T>(right + 1));
        }
    }

    [[nodiscard]] T prefixSum(size_type index) const {
        if (count_ == 0) return T{};
        if (index >= count_) throw IndexOutOfRange(index, count_);
        return multiplier_.prefixSum(index) * static_cast<T>(index + 1) +
               constant_.prefixSum(index);
    }

    [[nodiscard]] T rangeSum(size_type left, size_type right) const {
        if (left > right) throw InvalidArgument("fenwick range is inverted");
        T total = prefixSum(right);
        if (left > 0) total -= prefixSum(left - 1);
        return total;
    }

    [[nodiscard]] T at(size_type index) const { return rangeSum(index, index); }

private:
    void addSuffix(size_type from, const T& slope, const T& intercept) {
        multiplier_.add(from, slope);
        constant_.add(from, intercept);
    }

    FenwickTree<T> multiplier_;
    FenwickTree<T> constant_;
    size_type count_{0};
};

}  // namespace daedalus

#endif  // DAEDALUS_TREES_FENWICK_TREE_HPP
