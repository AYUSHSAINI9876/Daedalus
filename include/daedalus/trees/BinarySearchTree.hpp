// ============================================================================
//  Daedalus :: trees/BinarySearchTree.hpp
//
//  The unbalanced baseline. Every operation is O(h), and h is O(log n) only if
//  the insertion order is kind -- feed it sorted data and it degenerates into a
//  linked list. That failure is not a flaw to hide -- a test asserts on it and
//  the benchmark measures it -- because it is the entire motivation for the
//  AVL, red-black, splay and treap variants that follow.
//
//  Set semantics: inserting an existing key is a no-op.
//
//  Complexity: search/insert/erase O(h) -- O(log n) balanced, O(n) worst case
// ============================================================================
#ifndef DAEDALUS_TREES_BINARY_SEARCH_TREE_HPP
#define DAEDALUS_TREES_BINARY_SEARCH_TREE_HPP

#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/trees/BinaryTreeBase.hpp"

namespace daedalus {

template <typename T>
struct BSTNode {
    T value;
    BSTNode* left{nullptr};
    BSTNode* right{nullptr};

    explicit BSTNode(const T& v) : value(v) {}
};

template <typename T>
    requires LessThanComparable<T>
class BinarySearchTree final : public BinaryTreeBase<T, BSTNode<T>> {
    using Base = BinaryTreeBase<T, BSTNode<T>>;
    using Node = BSTNode<T>;

public:
    BinarySearchTree() = default;

    BinarySearchTree(std::initializer_list<T> values) {
        for (const T& value : values) insert(value);
    }

    [[nodiscard]] std::string name() const override { return "BinarySearchTree"; }

    /// Iterative insert. Duplicates are ignored.
    void insert(const T& value) override {
        Node** link = &this->root_;
        while (*link != nullptr) {
            Node* node = *link;
            if (value < node->value) {
                link = &node->left;
            } else if (node->value < value) {
                link = &node->right;
            } else {
                return;  // already present
            }
        }
        *link = new Node(value);
        ++this->size_;
    }

    /// Standard three-case deletion. A node with two children is replaced by
    /// its in-order successor, whose own removal is by construction a one- or
    /// zero-child case.
    bool erase(const T& value) override {
        Node** link = &this->root_;
        while (*link != nullptr) {
            Node* node = *link;
            if (value < node->value) {
                link = &node->left;
            } else if (node->value < value) {
                link = &node->right;
            } else {
                eraseNode(link);
                --this->size_;
                return true;
            }
        }
        return false;
    }

    /// Rebuilds the tree as a perfectly balanced one over the same keys.
    /// O(n): the in-order walk is already sorted, so the middle element of
    /// each range becomes that subtree's root.
    void rebalance() {
        const std::vector<T> sorted = this->inOrder();
        this->clear();
        this->root_ = buildBalanced(sorted, 0, sorted.size());
        this->size_ = sorted.size();
    }

    /// Replaces the contents with a perfectly balanced tree over `sorted`,
    /// which must already be in ascending order with no duplicates.
    void buildFromSorted(const std::vector<T>& sorted) {
        this->clear();
        this->root_ = buildBalanced(sorted, 0, sorted.size());
        this->size_ = sorted.size();
    }

private:
    /// `link` is the pointer slot referring to the node being erased, so the
    /// parent's child pointer is updated without ever tracking a parent.
    static void eraseNode(Node** link) {
        Node* node = *link;
        if (node->left == nullptr) {
            *link = node->right;
            delete node;
            return;
        }
        if (node->right == nullptr) {
            *link = node->left;
            delete node;
            return;
        }
        // Two children: take the in-order successor (leftmost of right subtree).
        Node** successorLink = &node->right;
        while ((*successorLink)->left != nullptr) successorLink = &(*successorLink)->left;
        Node* successor = *successorLink;
        node->value = successor->value;
        *successorLink = successor->right;
        delete successor;
    }

    static Node* buildBalanced(const std::vector<T>& sorted, std::size_t first, std::size_t last) {
        if (first >= last) return nullptr;
        const std::size_t middle = first + (last - first) / 2;
        Node* node = new Node(sorted[middle]);
        node->left = buildBalanced(sorted, first, middle);
        node->right = buildBalanced(sorted, middle + 1, last);
        return node;
    }
};

}  // namespace daedalus

#endif  // DAEDALUS_TREES_BINARY_SEARCH_TREE_HPP
