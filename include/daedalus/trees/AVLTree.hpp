// ============================================================================
//  Daedalus :: trees/AVLTree.hpp
//
//  Height-balanced BST. Every node keeps its subtree height, and any node whose
//  children differ by more than one level is repaired by one of four rotations
//  (LL, RR, LR, RL) on the way back up the insertion or deletion path.
//
//  AVL keeps a tighter bound than red-black (height <= 1.44 log2 n versus
//  2 log2 n), so lookups are faster; the price is more rotations on write.
//  That trade-off is the reason both are in this library.
//
//  Complexity: search/insert/erase O(log n) worst case | space O(n)
//              rotations per insert <= 1, per erase O(log n)
// ============================================================================
#ifndef DAEDALUS_TREES_AVL_TREE_HPP
#define DAEDALUS_TREES_AVL_TREE_HPP

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <string>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/trees/BinaryTreeBase.hpp"

namespace daedalus {

template <typename T>
struct AVLNode {
    T value;
    AVLNode* left{nullptr};
    AVLNode* right{nullptr};
    int height{1};  ///< in nodes: a leaf is 1, so an empty subtree is 0

    explicit AVLNode(const T& v) : value(v) {}
};

template <typename T>
    requires LessThanComparable<T>
class AVLTree final : public BinaryTreeBase<T, AVLNode<T>> {
    using Base = BinaryTreeBase<T, AVLNode<T>>;
    using Node = AVLNode<T>;

public:
    AVLTree() = default;

    AVLTree(std::initializer_list<T> values) {
        for (const T& value : values) insert(value);
    }

    [[nodiscard]] std::string name() const override { return "AVLTree"; }

    /// O(1) thanks to the stored heights, instead of the base class walk.
    [[nodiscard]] int height() const override {
        return this->root_ == nullptr ? -1 : this->root_->height - 1;
    }

    void insert(const T& value) override {
        bool inserted = false;
        this->root_ = insertInto(this->root_, value, inserted);
        if (inserted) ++this->size_;
    }

    bool erase(const T& value) override {
        bool removed = false;
        this->root_ = eraseFrom(this->root_, value, removed);
        if (removed) --this->size_;
        return removed;
    }

    /// Balance factor of the root: left height minus right height.
    [[nodiscard]] int balanceFactor() const { return balanceOf(this->root_); }

    /// Checks the AVL invariant at every node. The tests call this after each
    /// mutation batch -- a tree that is merely a valid BST is not enough.
    [[nodiscard]] bool isAVLBalanced() const { return checkAVL(this->root_); }

private:
    [[nodiscard]] static int nodeHeight(const Node* node) noexcept {
        return node == nullptr ? 0 : node->height;
    }

    static void refreshHeight(Node* node) noexcept {
        node->height = 1 + std::max(nodeHeight(node->left), nodeHeight(node->right));
    }

    [[nodiscard]] static int balanceOf(const Node* node) noexcept {
        return node == nullptr ? 0 : nodeHeight(node->left) - nodeHeight(node->right);
    }

    /*       y                 x
     *      / \               / \
     *     x   C     -->     A   y
     *    / \                   / \
     *   A   B                 B   C
     */
    static Node* rotateRight(Node* y) noexcept {
        Node* x = y->left;
        y->left = x->right;
        x->right = y;
        refreshHeight(y);
        refreshHeight(x);
        return x;
    }

    static Node* rotateLeft(Node* x) noexcept {
        Node* y = x->right;
        x->right = y->left;
        y->left = x;
        refreshHeight(x);
        refreshHeight(y);
        return y;
    }

    /// Restores the invariant at `node`, assuming its children are balanced.
    static Node* rebalanceNode(Node* node) noexcept {
        refreshHeight(node);
        const int balance = balanceOf(node);
        if (balance > 1) {
            if (balanceOf(node->left) < 0) node->left = rotateLeft(node->left);  // LR
            return rotateRight(node);                                            // LL
        }
        if (balance < -1) {
            if (balanceOf(node->right) > 0) node->right = rotateRight(node->right);  // RL
            return rotateLeft(node);                                                 // RR
        }
        return node;
    }

    static Node* insertInto(Node* node, const T& value, bool& inserted) {
        if (node == nullptr) {
            inserted = true;
            return new Node(value);
        }
        if (value < node->value) {
            node->left = insertInto(node->left, value, inserted);
        } else if (node->value < value) {
            node->right = insertInto(node->right, value, inserted);
        } else {
            return node;  // duplicate, tree unchanged
        }
        return rebalanceNode(node);
    }

    static Node* eraseFrom(Node* node, const T& value, bool& removed) {
        if (node == nullptr) return nullptr;

        if (value < node->value) {
            node->left = eraseFrom(node->left, value, removed);
        } else if (node->value < value) {
            node->right = eraseFrom(node->right, value, removed);
        } else {
            removed = true;
            if (node->left == nullptr || node->right == nullptr) {
                Node* survivor = node->left != nullptr ? node->left : node->right;
                delete node;
                if (survivor == nullptr) return nullptr;
                return rebalanceNode(survivor);
            }
            // Two children: copy the in-order successor up, then delete it.
            Node* successor = node->right;
            while (successor->left != nullptr) successor = successor->left;
            node->value = successor->value;
            bool ignored = false;
            node->right = eraseFrom(node->right, successor->value, ignored);
        }
        return rebalanceNode(node);
    }

    static bool checkAVL(const Node* node) {
        if (node == nullptr) return true;
        const int left = nodeHeight(node->left);
        const int right = nodeHeight(node->right);
        if (std::abs(left - right) > 1) return false;
        if (node->height != 1 + std::max(left, right)) return false;
        return checkAVL(node->left) && checkAVL(node->right);
    }
};

}  // namespace daedalus

#endif  // DAEDALUS_TREES_AVL_TREE_HPP
