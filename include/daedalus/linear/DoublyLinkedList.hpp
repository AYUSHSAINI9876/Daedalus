// ============================================================================
//  Daedalus :: linear/DoublyLinkedList.hpp
//
//  Circular doubly linked list with a sentinel node. The sentinel removes
//  every null check from insert/erase, which is why real implementations
//  (libstdc++ std::list included) are built this way.
//
//  The sentinel is a NodeBase holding only the two links, so the list never
//  requires T to be default-constructible -- a value-carrying sentinel would.
//
//  Complexity: pushFront/pushBack/popFront/popBack O(1) | at O(n)
//              erase-by-iterator O(1)                   | space O(n)
// ============================================================================
#ifndef DAEDALUS_LINEAR_DOUBLY_LINKED_LIST_HPP
#define DAEDALUS_LINEAR_DOUBLY_LINKED_LIST_HPP

#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"

namespace daedalus {

template <typename T>
class DoublyLinkedList final : public Sequence<T> {
    struct NodeBase {
        NodeBase* previous{nullptr};
        NodeBase* next{nullptr};
    };

    struct Node : NodeBase {
        T value;
        explicit Node(const T& v) : value(v) {}
        explicit Node(T&& v) : value(std::move(v)) {}
    };

    static T& valueOf(NodeBase* node) { return static_cast<Node*>(node)->value; }
    static const T& valueOf(const NodeBase* node) { return static_cast<const Node*>(node)->value; }

public:
    using value_type = T;
    using size_type = std::size_t;

    template <bool IsConst>
    class BasicIterator {
        friend class DoublyLinkedList;
        using NodePointer = std::conditional_t<IsConst, const NodeBase*, NodeBase*>;

    public:
        using iterator_category = std::bidirectional_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = std::conditional_t<IsConst, const T&, T&>;
        using pointer = std::conditional_t<IsConst, const T*, T*>;

        BasicIterator() = default;
        explicit BasicIterator(NodePointer node) : node_(node) {}

        /// Implicit iterator -> const_iterator conversion.
        template <bool OtherConst, typename = std::enable_if_t<IsConst && !OtherConst>>
        BasicIterator(const BasicIterator<OtherConst>& other) : node_(other.raw()) {}

        reference operator*() const { return valueOf(node_); }
        pointer operator->() const { return &valueOf(node_); }

        BasicIterator& operator++() {
            node_ = node_->next;
            return *this;
        }
        BasicIterator operator++(int) {
            BasicIterator copy = *this;
            ++(*this);
            return copy;
        }
        BasicIterator& operator--() {
            node_ = node_->previous;
            return *this;
        }
        BasicIterator operator--(int) {
            BasicIterator copy = *this;
            --(*this);
            return copy;
        }

        bool operator==(const BasicIterator& other) const { return node_ == other.node_; }
        bool operator!=(const BasicIterator& other) const { return node_ != other.node_; }

        [[nodiscard]] NodePointer raw() const noexcept { return node_; }

    private:
        NodePointer node_{nullptr};
    };

    using Iterator = BasicIterator<false>;
    using ConstIterator = BasicIterator<true>;

    // --- construction --------------------------------------------------------

    DoublyLinkedList() { resetSentinel(); }

    DoublyLinkedList(std::initializer_list<T> values) {
        resetSentinel();
        for (const T& value : values) pushBack(value);
    }

    DoublyLinkedList(const DoublyLinkedList& other) {
        resetSentinel();
        for (const NodeBase* node = other.sentinel_.next; node != &other.sentinel_;
             node = node->next) {
            pushBack(valueOf(node));
        }
    }

    DoublyLinkedList(DoublyLinkedList&& other) noexcept {
        resetSentinel();
        adopt(other);
    }

    DoublyLinkedList& operator=(DoublyLinkedList other) noexcept {
        swap(other);
        return *this;
    }

    ~DoublyLinkedList() override { clear(); }

    void swap(DoublyLinkedList& other) noexcept {
        DoublyLinkedList temporary;   // empty
        temporary.adopt(other);       // other     -> temporary
        other.adopt(*this);           // this      -> other
        adopt(temporary);             // temporary -> this
    }

    // --- observers -----------------------------------------------------------

    [[nodiscard]] size_type size() const noexcept override { return size_; }
    [[nodiscard]] bool empty() const noexcept override { return size_ == 0; }
    [[nodiscard]] std::string name() const override { return "DoublyLinkedList"; }

    void clear() override {
        NodeBase* node = sentinel_.next;
        while (node != &sentinel_) {
            NodeBase* next = node->next;
            delete static_cast<Node*>(node);
            node = next;
        }
        resetSentinel();
    }

    // --- modifiers -----------------------------------------------------------

    void pushFront(const T& value) { linkBefore(sentinel_.next, new Node(value)); }
    void pushBack(const T& value) { linkBefore(&sentinel_, new Node(value)); }

    T popFront() {
        if (empty()) throw EmptyContainer("popFront");
        return unlink(sentinel_.next);
    }

    T popBack() {
        if (empty()) throw EmptyContainer("popBack");
        return unlink(sentinel_.previous);
    }

    void insertAt(size_type index, const T& value) override {
        if (index > size_) throw IndexOutOfRange(index, size_);
        linkBefore(nodeAt(index), new Node(value));
    }

    T eraseAt(size_type index) override {
        if (index >= size_) throw IndexOutOfRange(index, size_);
        return unlink(nodeAt(index));
    }

    /// O(1) removal given a position. Returns an iterator to the next element.
    Iterator erase(Iterator position) {
        NodeBase* node = position.node_;
        if (node == nullptr || node == &sentinel_) {
            throw InvalidArgument("cannot erase the end() iterator");
        }
        NodeBase* next = node->next;
        unlink(node);
        return Iterator(next);
    }

    // --- element access ------------------------------------------------------

    [[nodiscard]] T& at(size_type index) override {
        if (index >= size_) throw IndexOutOfRange(index, size_);
        return valueOf(nodeAt(index));
    }

    [[nodiscard]] const T& at(size_type index) const override {
        if (index >= size_) throw IndexOutOfRange(index, size_);
        return valueOf(nodeAt(index));
    }

    [[nodiscard]] T& front() {
        if (empty()) throw EmptyContainer("front");
        return valueOf(sentinel_.next);
    }

    [[nodiscard]] T& back() {
        if (empty()) throw EmptyContainer("back");
        return valueOf(sentinel_.previous);
    }

    // --- Collection interface ------------------------------------------------

    void insert(const T& value) override { pushBack(value); }

    bool erase(const T& value) override {
        for (NodeBase* node = sentinel_.next; node != &sentinel_; node = node->next) {
            if (valueOf(node) == value) {
                unlink(node);
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool contains(const T& value) const override {
        for (const NodeBase* node = sentinel_.next; node != &sentinel_; node = node->next) {
            if (valueOf(node) == value) return true;
        }
        return false;
    }

    [[nodiscard]] std::vector<T> toVector() const override {
        std::vector<T> out;
        out.reserve(size_);
        for (const NodeBase* node = sentinel_.next; node != &sentinel_; node = node->next) {
            out.push_back(valueOf(node));
        }
        return out;
    }

    /// Reverses by swapping every node's links, including the sentinel's.
    void reverse() noexcept {
        NodeBase* node = &sentinel_;
        do {
            std::swap(node->previous, node->next);
            node = node->previous;   // previous now holds the old next
        } while (node != &sentinel_);
    }

    // --- iteration -----------------------------------------------------------

    [[nodiscard]] Iterator begin() noexcept { return Iterator(sentinel_.next); }
    [[nodiscard]] Iterator end() noexcept { return Iterator(&sentinel_); }
    [[nodiscard]] ConstIterator begin() const noexcept { return ConstIterator(sentinel_.next); }
    [[nodiscard]] ConstIterator end() const noexcept { return ConstIterator(&sentinel_); }

private:
    void resetSentinel() noexcept {
        sentinel_.next = &sentinel_;
        sentinel_.previous = &sentinel_;
        size_ = 0;
    }

    /// Steals every node from `source`; this list must already be empty.
    void adopt(DoublyLinkedList& source) noexcept {
        if (source.size_ == 0) {
            resetSentinel();
            return;
        }
        sentinel_.next = source.sentinel_.next;
        sentinel_.previous = source.sentinel_.previous;
        sentinel_.next->previous = &sentinel_;
        sentinel_.previous->next = &sentinel_;
        size_ = source.size_;
        source.resetSentinel();
    }

    void linkBefore(NodeBase* position, Node* fresh) noexcept {
        fresh->next = position;
        fresh->previous = position->previous;
        position->previous->next = fresh;
        position->previous = fresh;
        ++size_;
    }

    T unlink(NodeBase* node) {
        Node* typed = static_cast<Node*>(node);
        T value = std::move(typed->value);
        node->previous->next = node->next;
        node->next->previous = node->previous;
        delete typed;
        --size_;
        return value;
    }

    /// Walks from whichever end is closer, halving the average cost.
    [[nodiscard]] NodeBase* nodeAt(size_type index) const {
        NodeBase* sentinel = const_cast<NodeBase*>(&sentinel_);
        if (index <= size_ / 2) {
            NodeBase* node = sentinel->next;
            for (size_type i = 0; i < index; ++i) node = node->next;
            return node;
        }
        NodeBase* node = sentinel;
        for (size_type i = size_; i > index; --i) node = node->previous;
        return node;
    }

    NodeBase sentinel_;
    size_type size_{0};
};

}   // namespace daedalus

#endif   // DAEDALUS_LINEAR_DOUBLY_LINKED_LIST_HPP
