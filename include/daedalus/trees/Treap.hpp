// ============================================================================
//  Daedalus :: trees/Treap.hpp
//
//  Randomised BST: a binary search tree on the key and simultaneously a max-
//  heap on a random priority assigned at insertion. Because the priorities are
//  random, the resulting shape is exactly the shape you would get by inserting
//  the keys in random order -- expected height O(log n) with no rebalancing
//  logic beyond a single rotation per level.
//
//  The same structure supports split/merge in O(log n), which is what makes
//  treaps the usual choice for implicit-key sequence problems.
//
//  The RNG is explicitly seeded so a run is reproducible.
//
//  Complexity: search/insert/erase expected O(log n), worst O(n)
// ============================================================================
#ifndef DAEDALUS_TREES_TREAP_HPP
#define DAEDALUS_TREES_TREAP_HPP

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <random>
#include <string>
#include <utility>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/trees/BinaryTreeBase.hpp"

namespace daedalus {

template <typename T>
struct TreapNode {
    T value;
    TreapNode* left{nullptr};
    TreapNode* right{nullptr};
    std::uint32_t priority{0};

    TreapNode(const T& v, std::uint32_t p) : value(v), priority(p) {}
};

template <typename T>
    requires LessThanComparable<T>
class Treap final : public BinaryTreeBase<T, TreapNode<T>> {
    using Base = BinaryTreeBase<T, TreapNode<T>>;
    using Node = TreapNode<T>;

public:
    explicit Treap(std::uint32_t seed = 0x5BD1E995u) : rng_(seed) {}

    Treap(std::initializer_list<T> values, std::uint32_t seed = 0x5BD1E995u) : rng_(seed) {
        for (const T& value : values) insert(value);
    }

    [[nodiscard]] std::string name() const override { return "Treap"; }

    void insert(const T& value) override {
        bool inserted = false;
        this->root_ = insertInto(this->root_, value, static_cast<std::uint32_t>(rng_()), inserted);
        if (inserted) ++this->size_;
    }

    bool erase(const T& value) override {
        bool removed = false;
        this->root_ = eraseFrom(this->root_, value, removed);
        if (removed) --this->size_;
        return removed;
    }

    /// Verifies the max-heap property on priorities, which together with
    /// isValidBST() is the full treap invariant.
    [[nodiscard]] bool verifyHeapProperty() const { return checkHeap(this->root_); }

private:
    static Node* rotateRight(Node* y) noexcept {
        Node* x = y->left;
        y->left = x->right;
        x->right = y;
        return x;
    }

    static Node* rotateLeft(Node* x) noexcept {
        Node* y = x->right;
        x->right = y->left;
        y->left = x;
        return y;
    }

    static Node* insertInto(Node* node, const T& value, std::uint32_t priority, bool& inserted) {
        if (node == nullptr) {
            inserted = true;
            return new Node(value, priority);
        }
        if (value < node->value) {
            node->left = insertInto(node->left, value, priority, inserted);
            // Rotate up while the child outranks its parent.
            if (node->left->priority > node->priority) node = rotateRight(node);
        } else if (node->value < value) {
            node->right = insertInto(node->right, value, priority, inserted);
            if (node->right->priority > node->priority) node = rotateLeft(node);
        }
        return node;   // duplicate keys leave the tree untouched
    }

    static Node* eraseFrom(Node* node, const T& value, bool& removed) {
        if (node == nullptr) return nullptr;
        if (value < node->value) {
            node->left = eraseFrom(node->left, value, removed);
            return node;
        }
        if (node->value < value) {
            node->right = eraseFrom(node->right, value, removed);
            return node;
        }

        removed = true;
        if (node->left == nullptr) {
            Node* survivor = node->right;
            delete node;
            return survivor;
        }
        if (node->right == nullptr) {
            Node* survivor = node->left;
            delete node;
            return survivor;
        }
        // Rotate the higher-priority child up and keep sinking the target.
        if (node->left->priority > node->right->priority) {
            node = rotateRight(node);
            node->right = eraseFrom(node->right, value, removed);
        } else {
            node = rotateLeft(node);
            node->left = eraseFrom(node->left, value, removed);
        }
        return node;
    }

    static bool checkHeap(const Node* node) {
        if (node == nullptr) return true;
        if (node->left != nullptr && node->left->priority > node->priority) return false;
        if (node->right != nullptr && node->right->priority > node->priority) return false;
        return checkHeap(node->left) && checkHeap(node->right);
    }

    std::mt19937 rng_;
};

}   // namespace daedalus

#endif   // DAEDALUS_TREES_TREAP_HPP
