// ============================================================================
//  Daedalus :: linear/Queue.hpp
//
//  FIFO adapter over any store that offers O(1) pushBack/popFront. The default
//  is the ring-buffer Deque, so enqueue and dequeue are both O(1); a
//  DynamicArray would make dequeue O(n), which is exactly why the store is a
//  parameter rather than a hard-coded choice.
//
//  Complexity: enqueue/dequeue/peek O(1) amortised | space O(n)
// ============================================================================
#ifndef DAEDALUS_LINEAR_QUEUE_HPP
#define DAEDALUS_LINEAR_QUEUE_HPP

#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"
#include "daedalus/linear/Deque.hpp"

namespace daedalus {

template <typename T, typename Store = Deque<T>>
class Queue final : public Collection<T> {
public:
    using value_type = T;
    using size_type = std::size_t;

    Queue() = default;

    Queue(std::initializer_list<T> values) {
        for (const T& value : values) enqueue(value);
    }

    [[nodiscard]] size_type size() const noexcept override { return store_.size(); }
    [[nodiscard]] bool empty() const noexcept override { return store_.empty(); }
    [[nodiscard]] std::string name() const override { return "Queue"; }

    void clear() override { store_.clear(); }

    void enqueue(const T& value) { store_.pushBack(value); }

    T dequeue() {
        if (empty()) throw EmptyContainer("dequeue");
        return store_.popFront();
    }

    [[nodiscard]] T& peek() {
        if (empty()) throw EmptyContainer("peek");
        return store_.front();
    }

    [[nodiscard]] const T& peek() const {
        if (empty()) throw EmptyContainer("peek");
        return store_.front();
    }

    [[nodiscard]] T& back() {
        if (empty()) throw EmptyContainer("back");
        return store_.back();
    }

    // --- Collection interface ------------------------------------------------

    void insert(const T& value) override { enqueue(value); }
    bool erase(const T& value) override { return store_.erase(value); }
    [[nodiscard]] bool contains(const T& value) const override { return store_.contains(value); }
    [[nodiscard]] std::vector<T> toVector() const override { return store_.toVector(); }

private:
    Store store_;
};

}  // namespace daedalus

#endif  // DAEDALUS_LINEAR_QUEUE_HPP
