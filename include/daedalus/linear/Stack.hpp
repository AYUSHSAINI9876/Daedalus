// ============================================================================
//  Daedalus :: linear/Stack.hpp
//
//  LIFO adapter (Adapter pattern). The backing store is a template parameter,
//  so the same Stack runs on contiguous storage or on a linked list without a
//  line of duplicated logic:
//
//      Stack<int>                              // DynamicArray-backed
//      Stack<int, DoublyLinkedList<int>>       // node-backed
//
//  Complexity: push/pop/peek O(1) amortised | space O(n)
// ============================================================================
#ifndef DAEDALUS_LINEAR_STACK_HPP
#define DAEDALUS_LINEAR_STACK_HPP

#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"
#include "daedalus/linear/DynamicArray.hpp"

namespace daedalus {

template <typename T, typename Store = DynamicArray<T>>
class Stack final : public Collection<T> {
public:
    using value_type = T;
    using size_type = std::size_t;

    Stack() = default;

    Stack(std::initializer_list<T> values) {
        for (const T& value : values) push(value);
    }

    [[nodiscard]] size_type size() const noexcept override { return store_.size(); }
    [[nodiscard]] bool empty() const noexcept override { return store_.empty(); }
    [[nodiscard]] std::string name() const override { return "Stack"; }

    void clear() override { store_.clear(); }

    void push(const T& value) { store_.pushBack(value); }

    /// Removes and returns the top element.
    T pop() {
        if (empty()) throw EmptyContainer("pop");
        return store_.popBack();
    }

    [[nodiscard]] T& peek() {
        if (empty()) throw EmptyContainer("peek");
        return store_.back();
    }

    [[nodiscard]] const T& peek() const {
        if (empty()) throw EmptyContainer("peek");
        return store_.back();
    }

    // --- Collection interface ------------------------------------------------

    void insert(const T& value) override { push(value); }
    bool erase(const T& value) override { return store_.erase(value); }
    [[nodiscard]] bool contains(const T& value) const override { return store_.contains(value); }

    /// Bottom-to-top order, so toString() reads like the physical stack laid
    /// on its side with the top on the right.
    [[nodiscard]] std::vector<T> toVector() const override { return store_.toVector(); }

    [[nodiscard]] const Store& store() const noexcept { return store_; }

private:
    Store store_;
};

}   // namespace daedalus

#endif   // DAEDALUS_LINEAR_STACK_HPP
