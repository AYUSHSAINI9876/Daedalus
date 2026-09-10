// ============================================================================
//  Daedalus :: linear/SinglyLinkedList.hpp
//
//  Head/tail singly linked list with the interview-classic operations built in
//  as first-class members: in-place reversal, the slow/fast middle node, cycle
//  detection (Floyd), and nth-from-end.
//
//  Nodes are owned through raw pointers and freed by clear(); a unique_ptr
//  chain would recurse on destruction and blow the stack on a long list, which
//  is exactly the kind of detail this project is meant to show awareness of.
//
//  Complexity: pushFront O(1) | pushBack O(1) | at O(n) | erase O(n)
// ============================================================================
#ifndef DAEDALUS_LINEAR_SINGLY_LINKED_LIST_HPP
#define DAEDALUS_LINEAR_SINGLY_LINKED_LIST_HPP

#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"

namespace daedalus {

template <typename T>
class SinglyLinkedList final : public Sequence<T> {
    struct Node {
        T value;
        Node* next{nullptr};
        explicit Node(const T& v) : value(v) {}
        explicit Node(T&& v) : value(std::move(v)) {}
    };

public:
    using value_type = T;
    using size_type = std::size_t;

    /// Forward iterator, enough for range-for and the standard algorithms that
    /// only need a single pass.
    class Iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = T*;
        using reference = T&;

        Iterator() = default;
        explicit Iterator(Node* node) : node_(node) {}

        reference operator*() const { return node_->value; }
        pointer operator->() const { return &node_->value; }

        Iterator& operator++() {
            node_ = node_->next;
            return *this;
        }
        Iterator operator++(int) {
            Iterator copy = *this;
            ++(*this);
            return copy;
        }
        bool operator==(const Iterator& other) const { return node_ == other.node_; }
        bool operator!=(const Iterator& other) const { return node_ != other.node_; }

    private:
        Node* node_{nullptr};
    };

    class ConstIterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        ConstIterator() = default;
        explicit ConstIterator(const Node* node) : node_(node) {}

        reference operator*() const { return node_->value; }
        pointer operator->() const { return &node_->value; }

        ConstIterator& operator++() {
            node_ = node_->next;
            return *this;
        }
        ConstIterator operator++(int) {
            ConstIterator copy = *this;
            ++(*this);
            return copy;
        }
        bool operator==(const ConstIterator& other) const { return node_ == other.node_; }
        bool operator!=(const ConstIterator& other) const { return node_ != other.node_; }

    private:
        const Node* node_{nullptr};
    };

    // --- construction --------------------------------------------------------

    SinglyLinkedList() = default;

    SinglyLinkedList(std::initializer_list<T> values) {
        for (const T& value : values) pushBack(value);
    }

    SinglyLinkedList(const SinglyLinkedList& other) {
        for (const Node* node = other.head_; node != nullptr; node = node->next) {
            pushBack(node->value);
        }
    }

    SinglyLinkedList(SinglyLinkedList&& other) noexcept
        : head_(other.head_), tail_(other.tail_), size_(other.size_) {
        other.head_ = other.tail_ = nullptr;
        other.size_ = 0;
    }

    SinglyLinkedList& operator=(SinglyLinkedList other) noexcept {
        swap(other);
        return *this;
    }

    ~SinglyLinkedList() override { clear(); }

    void swap(SinglyLinkedList& other) noexcept {
        std::swap(head_, other.head_);
        std::swap(tail_, other.tail_);
        std::swap(size_, other.size_);
    }

    // --- observers -----------------------------------------------------------

    [[nodiscard]] size_type size() const noexcept override { return size_; }
    [[nodiscard]] bool empty() const noexcept override { return size_ == 0; }
    [[nodiscard]] std::string name() const override { return "SinglyLinkedList"; }

    void clear() override {
        Node* node = head_;
        while (node != nullptr) {
            Node* next = node->next;
            delete node;
            node = next;
        }
        head_ = tail_ = nullptr;
        size_ = 0;
    }

    // --- modifiers -----------------------------------------------------------

    void pushFront(const T& value) {
        Node* node = new Node(value);
        node->next = head_;
        head_ = node;
        if (tail_ == nullptr) tail_ = node;
        ++size_;
    }

    void pushBack(const T& value) {
        Node* node = new Node(value);
        if (tail_ == nullptr) {
            head_ = tail_ = node;
        } else {
            tail_->next = node;
            tail_ = node;
        }
        ++size_;
    }

    T popFront() {
        if (empty()) throw EmptyContainer("popFront");
        Node* node = head_;
        T value = std::move(node->value);
        head_ = node->next;
        if (head_ == nullptr) tail_ = nullptr;
        delete node;
        --size_;
        return value;
    }

    /// O(n): a singly linked list has no way back from the tail.
    T popBack() {
        if (empty()) throw EmptyContainer("popBack");
        if (head_ == tail_) return popFront();
        Node* previous = head_;
        while (previous->next != tail_) previous = previous->next;
        T value = std::move(tail_->value);
        delete tail_;
        previous->next = nullptr;
        tail_ = previous;
        --size_;
        return value;
    }

    void insertAt(size_type index, const T& value) override {
        if (index > size_) throw IndexOutOfRange(index, size_);
        if (index == 0) return pushFront(value);
        if (index == size_) return pushBack(value);
        Node* previous = nodeAt(index - 1);
        Node* node = new Node(value);
        node->next = previous->next;
        previous->next = node;
        ++size_;
    }

    T eraseAt(size_type index) override {
        if (index >= size_) throw IndexOutOfRange(index, size_);
        if (index == 0) return popFront();
        Node* previous = nodeAt(index - 1);
        Node* target = previous->next;
        T value = std::move(target->value);
        previous->next = target->next;
        if (target == tail_) tail_ = previous;
        delete target;
        --size_;
        return value;
    }

    // --- element access ------------------------------------------------------

    [[nodiscard]] T& at(size_type index) override {
        if (index >= size_) throw IndexOutOfRange(index, size_);
        return nodeAt(index)->value;
    }

    [[nodiscard]] const T& at(size_type index) const override {
        if (index >= size_) throw IndexOutOfRange(index, size_);
        return nodeAt(index)->value;
    }

    [[nodiscard]] T& front() {
        if (empty()) throw EmptyContainer("front");
        return head_->value;
    }

    [[nodiscard]] T& back() {
        if (empty()) throw EmptyContainer("back");
        return tail_->value;
    }

    // --- Collection interface ------------------------------------------------

    void insert(const T& value) override { pushBack(value); }

    bool erase(const T& value) override {
        Node* previous = nullptr;
        for (Node* node = head_; node != nullptr; previous = node, node = node->next) {
            if (!(node->value == value)) continue;
            if (previous == nullptr) {
                head_ = node->next;
            } else {
                previous->next = node->next;
            }
            if (node == tail_) tail_ = previous;
            delete node;
            --size_;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool contains(const T& value) const override {
        for (const Node* node = head_; node != nullptr; node = node->next) {
            if (node->value == value) return true;
        }
        return false;
    }

    [[nodiscard]] std::vector<T> toVector() const override {
        std::vector<T> out;
        out.reserve(size_);
        for (const Node* node = head_; node != nullptr; node = node->next) {
            out.push_back(node->value);
        }
        return out;
    }

    // --- classic list algorithms ---------------------------------------------

    /// Reverses the list in place by re-pointing every next link. O(n) / O(1).
    void reverse() noexcept {
        Node* previous = nullptr;
        Node* current = head_;
        tail_ = head_;
        while (current != nullptr) {
            Node* next = current->next;
            current->next = previous;
            previous = current;
            current = next;
        }
        head_ = previous;
    }

    /// Middle element by the slow/fast pointer walk. For an even length this
    /// returns the second of the two middles, matching the usual convention.
    [[nodiscard]] std::optional<T> middle() const {
        if (head_ == nullptr) return std::nullopt;
        const Node* slow = head_;
        const Node* fast = head_;
        while (fast != nullptr && fast->next != nullptr) {
            slow = slow->next;
            fast = fast->next->next;
        }
        return slow->value;
    }

    /// nth element counted from the tail; nthFromEnd(1) is the last element.
    [[nodiscard]] std::optional<T> nthFromEnd(size_type n) const {
        if (n == 0 || n > size_) return std::nullopt;
        const Node* lead = head_;
        for (size_type i = 0; i < n; ++i) lead = lead->next;
        const Node* trail = head_;
        while (lead != nullptr) {
            lead = lead->next;
            trail = trail->next;
        }
        return trail->value;
    }

    /// Floyd's tortoise and hare. Always false for a list built through this
    /// class; exposed so the algorithm is testable via makeCycleForTesting().
    [[nodiscard]] bool hasCycle() const noexcept {
        const Node* slow = head_;
        const Node* fast = head_;
        while (fast != nullptr && fast->next != nullptr) {
            slow = slow->next;
            fast = fast->next->next;
            if (slow == fast) return true;
        }
        return false;
    }

    /// Deletes consecutive duplicates, as on a sorted list. Returns how many
    /// nodes were removed.
    size_type removeConsecutiveDuplicates() {
        size_type removed = 0;
        for (Node* node = head_; node != nullptr && node->next != nullptr;) {
            if (node->value == node->next->value) {
                Node* duplicate = node->next;
                node->next = duplicate->next;
                if (duplicate == tail_) tail_ = node;
                delete duplicate;
                --size_;
                ++removed;
            } else {
                node = node->next;
            }
        }
        return removed;
    }

    /// Test hook: links the tail back to the node at `index`, forming a loop.
    /// The list must be repaired with breakCycleForTesting() before it is
    /// destroyed, otherwise clear() would loop forever.
    void makeCycleForTesting(size_type index) {
        if (empty() || index >= size_) throw IndexOutOfRange(index, size_);
        tail_->next = nodeAt(index);
    }

    void breakCycleForTesting() noexcept {
        if (tail_ != nullptr) tail_->next = nullptr;
    }

    // --- iteration -----------------------------------------------------------

    [[nodiscard]] Iterator begin() noexcept { return Iterator(head_); }
    [[nodiscard]] Iterator end() noexcept { return Iterator(nullptr); }
    [[nodiscard]] ConstIterator begin() const noexcept { return ConstIterator(head_); }
    [[nodiscard]] ConstIterator end() const noexcept { return ConstIterator(nullptr); }

private:
    [[nodiscard]] Node* nodeAt(size_type index) const {
        Node* node = head_;
        for (size_type i = 0; i < index; ++i) node = node->next;
        return node;
    }

    Node* head_{nullptr};
    Node* tail_{nullptr};
    size_type size_{0};
};

}  // namespace daedalus

#endif  // DAEDALUS_LINEAR_SINGLY_LINKED_LIST_HPP
