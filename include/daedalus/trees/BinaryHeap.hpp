// ============================================================================
//  Daedalus :: trees/BinaryHeap.hpp
//
//  Array-backed binary heap -- a complete binary tree stored without pointers,
//  where the children of index i live at 2i+1 and 2i+2. That implicit layout is
//  why heapsort needs no extra memory and why the priority queue below has no
//  allocation per element.
//
//  Compare defaults to std::less, giving a MAX-heap, matching the convention of
//  std::priority_queue. MinHeap<T> is the std::greater specialisation.
//
//  Complexity: push O(log n) | pop O(log n) | top O(1)
//              buildHeap O(n) -- strictly better than n pushes at O(n log n)
// ============================================================================
#ifndef DAEDALUS_TREES_BINARY_HEAP_HPP
#define DAEDALUS_TREES_BINARY_HEAP_HPP

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"
#include "daedalus/linear/DynamicArray.hpp"

namespace daedalus {

template <typename T, typename Compare = std::less<T>>
class BinaryHeap final : public Collection<T> {
public:
    using value_type = T;
    using size_type = std::size_t;

    BinaryHeap() = default;

    explicit BinaryHeap(Compare compare) : compare_(std::move(compare)) {}

    BinaryHeap(std::initializer_list<T> values) {
        for (const T& value : values) heap_.pushBack(value);
        buildHeap();
    }

    /// O(n) bulk construction (Floyd's method), not n separate O(log n) pushes.
    explicit BinaryHeap(const std::vector<T>& values, Compare compare = Compare())
        : compare_(std::move(compare)) {
        heap_.reserve(values.size());
        for (const T& value : values) heap_.pushBack(value);
        buildHeap();
    }

    [[nodiscard]] size_type size() const noexcept override { return heap_.size(); }
    [[nodiscard]] bool empty() const noexcept override { return heap_.empty(); }
    [[nodiscard]] std::string name() const override { return "BinaryHeap"; }

    void clear() override { heap_.clear(); }

    /// Highest-priority element under Compare.
    [[nodiscard]] const T& top() const {
        if (heap_.empty()) throw EmptyContainer("top");
        return heap_[0];
    }

    void push(const T& value) {
        heap_.pushBack(value);
        siftUp(heap_.size() - 1);
    }

    /// Removes and returns the top: swap it with the last slot, shrink, sink.
    T pop() {
        if (heap_.empty()) throw EmptyContainer("pop");
        T result = std::move(heap_[0]);
        heap_[0] = std::move(heap_[heap_.size() - 1]);
        (void)heap_.popBack();
        if (!heap_.empty()) siftDown(0);
        return result;
    }

    /// Replaces the top in one sift instead of a pop followed by a push.
    T replaceTop(const T& value) {
        if (heap_.empty()) throw EmptyContainer("replaceTop");
        T result = std::move(heap_[0]);
        heap_[0] = value;
        siftDown(0);
        return result;
    }

    // --- Collection interface ------------------------------------------------

    void insert(const T& value) override { push(value); }

    bool erase(const T& value) override {
        const std::size_t index = heap_.indexOf(value);
        if (index == DynamicArray<T>::kNotFound) return false;
        heap_[index] = std::move(heap_[heap_.size() - 1]);
        (void)heap_.popBack();
        if (index < heap_.size()) {
            siftDown(index);
            siftUp(index);
        }
        return true;
    }

    [[nodiscard]] bool contains(const T& value) const override { return heap_.contains(value); }

    /// Raw array order (level-order over the implicit tree), not sorted.
    [[nodiscard]] std::vector<T> toVector() const override { return heap_.toVector(); }

    /// Contents in priority order, leaving the heap untouched.
    [[nodiscard]] std::vector<T> drainSorted() const {
        BinaryHeap copy(*this);
        std::vector<T> out;
        out.reserve(copy.size());
        while (!copy.empty()) out.push_back(copy.pop());
        return out;
    }

    /// Checks that no child outranks its parent.
    [[nodiscard]] bool isValidHeap() const {
        for (std::size_t parent = 0; parent < heap_.size(); ++parent) {
            const std::size_t left = 2 * parent + 1;
            const std::size_t right = left + 1;
            if (left < heap_.size() && compare_(heap_[parent], heap_[left])) return false;
            if (right < heap_.size() && compare_(heap_[parent], heap_[right])) return false;
        }
        return true;
    }

private:
    /// Floyd's build-heap: sift down every internal node, last to first.
    void buildHeap() {
        if (heap_.size() < 2) return;
        for (std::size_t i = heap_.size() / 2; i-- > 0;) siftDown(i);
    }

    void siftUp(std::size_t index) {
        while (index > 0) {
            const std::size_t parent = (index - 1) / 2;
            if (!compare_(heap_[parent], heap_[index])) break;
            std::swap(heap_[parent], heap_[index]);
            index = parent;
        }
    }

    void siftDown(std::size_t index) {
        const std::size_t count = heap_.size();
        for (;;) {
            const std::size_t left = 2 * index + 1;
            if (left >= count) break;
            std::size_t best = left;
            const std::size_t right = left + 1;
            if (right < count && compare_(heap_[left], heap_[right])) best = right;
            if (!compare_(heap_[index], heap_[best])) break;
            std::swap(heap_[index], heap_[best]);
            index = best;
        }
    }

    DynamicArray<T> heap_;
    Compare compare_{};
};

/// Min-heap: the smallest element sits on top.
template <typename T>
using MinHeap = BinaryHeap<T, std::greater<T>>;

/// Max-heap, spelled out for readability at call sites.
template <typename T>
using MaxHeap = BinaryHeap<T, std::less<T>>;

// ---------------------------------------------------------------------------

/// Priority queue with an explicit priority separate from the payload, which is
/// what schedulers and Dijkstra actually need. Lower priority values come out
/// first, matching the usual "cost" reading.
template <typename T, typename Priority = int>
class PriorityQueue final : public Container<T> {
public:
    struct Entry {
        Priority priority;
        T value;
        std::size_t sequence;  ///< insertion order, to make ties deterministic

        /// Identity is the sequence number: BinaryHeap's Collection interface
        /// needs equality, and two entries are the same entry only if they are
        /// the same push.
        bool operator==(const Entry& other) const { return sequence == other.sequence; }
    };

    struct EntryOrder {
        bool operator()(const Entry& a, const Entry& b) const {
            if (b.priority < a.priority) return true;   // higher cost = lower rank
            if (a.priority < b.priority) return false;
            return b.sequence < a.sequence;             // earlier insert wins ties
        }
    };

    using size_type = std::size_t;

    [[nodiscard]] size_type size() const noexcept override { return heap_.size(); }
    [[nodiscard]] bool empty() const noexcept override { return heap_.empty(); }
    [[nodiscard]] std::string name() const override { return "PriorityQueue"; }

    void clear() override {
        heap_.clear();
        nextSequence_ = 0;
    }

    void push(const T& value, Priority priority) {
        heap_.push(Entry{priority, value, nextSequence_++});
    }

    [[nodiscard]] const T& peek() const { return heap_.top().value; }
    [[nodiscard]] Priority peekPriority() const { return heap_.top().priority; }

    T pop() { return heap_.pop().value; }

    [[nodiscard]] std::string toString() const override {
        return name() + "(size=" + std::to_string(size()) + ")";
    }

private:
    BinaryHeap<Entry, EntryOrder> heap_;
    std::size_t nextSequence_{0};
};

}  // namespace daedalus

#endif  // DAEDALUS_TREES_BINARY_HEAP_HPP
