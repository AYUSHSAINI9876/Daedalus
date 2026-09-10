// ============================================================================
//  Daedalus :: trees/SplayTree.hpp
//
//  Self-adjusting BST (Sleator and Tarjan). It stores no balance metadata at
//  all -- no heights, no colours. Instead every access rotates the touched key
//  to the root, so recently used keys stay cheap to reach. Any single operation
//  can cost O(n), but any sequence of m operations costs O(m log n) amortised.
//
//  This uses the top-down splay, which restructures in one downward pass
//  instead of walking down and then rotating back up.
//
//  Note that lookup mutates the tree. contains() is still declared const
//  because the observable set of keys never changes -- the const_cast marks a
//  deliberate case of logical rather than bitwise constness.
//
//  Complexity: search/insert/erase O(log n) amortised, O(n) worst single op
// ============================================================================
#ifndef DAEDALUS_TREES_SPLAY_TREE_HPP
#define DAEDALUS_TREES_SPLAY_TREE_HPP

#include <cstddef>
#include <initializer_list>
#include <string>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/trees/BinaryTreeBase.hpp"

namespace daedalus {

template <typename T>
struct SplayNode {
    T value;
    SplayNode* left{nullptr};
    SplayNode* right{nullptr};

    explicit SplayNode(const T& v) : value(v) {}
};

template <typename T>
    requires LessThanComparable<T>
class SplayTree final : public BinaryTreeBase<T, SplayNode<T>> {
    using Base = BinaryTreeBase<T, SplayNode<T>>;
    using Node = SplayNode<T>;

public:
    SplayTree() = default;

    SplayTree(std::initializer_list<T> values) {
        for (const T& value : values) insert(value);
    }

    [[nodiscard]] std::string name() const override { return "SplayTree"; }

    void insert(const T& value) override {
        if (this->root_ == nullptr) {
            this->root_ = new Node(value);
            ++this->size_;
            return;
        }

        this->root_ = splay(this->root_, value);
        if (!(value < this->root_->value) && !(this->root_->value < value)) {
            return;   // already present, and now at the root
        }

        Node* fresh = new Node(value);
        if (value < this->root_->value) {
            fresh->right = this->root_;
            fresh->left = this->root_->left;
            this->root_->left = nullptr;
        } else {
            fresh->left = this->root_;
            fresh->right = this->root_->right;
            this->root_->right = nullptr;
        }
        this->root_ = fresh;
        ++this->size_;
    }

    bool erase(const T& value) override {
        if (this->root_ == nullptr) return false;

        this->root_ = splay(this->root_, value);
        if (value < this->root_->value || this->root_->value < value) return false;

        Node* doomed = this->root_;
        if (doomed->left == nullptr) {
            this->root_ = doomed->right;
        } else {
            // Splaying the left subtree on the erased key lifts its maximum to
            // the top, and that node has no right child -- so the right subtree
            // attaches there directly.
            Node* newRoot = splay(doomed->left, value);
            newRoot->right = doomed->right;
            this->root_ = newRoot;
        }
        delete doomed;
        --this->size_;
        return true;
    }

    /// Lookup splays the key to the root, which is the whole point of the
    /// structure -- see the class comment on logical constness.
    [[nodiscard]] bool contains(const T& value) const override {
        if (this->root_ == nullptr) return false;
        auto* self = const_cast<SplayTree*>(this);
        self->root_ = splay(self->root_, value);
        return !(value < self->root_->value) && !(self->root_->value < value);
    }

    /// The key sitting at the root, i.e. the most recently touched one.
    [[nodiscard]] std::optional<T> mostRecentlyAccessed() const {
        if (this->root_ == nullptr) return std::nullopt;
        return this->root_->value;
    }

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

    /// Top-down splay: descends towards `key`, peeling the path into a left and
    /// a right spine, then reassembles them under the final node. If `key` is
    /// absent, the last node visited ends up at the root instead.
    static Node* splay(Node* root, const T& key) {
        if (root == nullptr) return nullptr;

        Node* leftSpine = nullptr;    // root of the accumulated smaller keys
        Node* leftMax = nullptr;      // its rightmost node
        Node* rightSpine = nullptr;   // root of the accumulated larger keys
        Node* rightMin = nullptr;     // its leftmost node

        for (;;) {
            if (key < root->value) {
                if (root->left == nullptr) break;
                if (key < root->left->value) {
                    root = rotateRight(root);   // zig-zig
                    if (root->left == nullptr) break;
                }
                if (rightMin == nullptr) {
                    rightSpine = root;
                } else {
                    rightMin->left = root;
                }
                rightMin = root;
                root = root->left;
            } else if (root->value < key) {
                if (root->right == nullptr) break;
                if (root->right->value < key) {
                    root = rotateLeft(root);   // zag-zag
                    if (root->right == nullptr) break;
                }
                if (leftMax == nullptr) {
                    leftSpine = root;
                } else {
                    leftMax->right = root;
                }
                leftMax = root;
                root = root->right;
            } else {
                break;
            }
        }

        // Reassemble: the spines hang back off the new root's children.
        if (leftMax != nullptr) {
            leftMax->right = root->left;
            root->left = leftSpine;
        }
        if (rightMin != nullptr) {
            rightMin->left = root->right;
            root->right = rightSpine;
        }
        return root;
    }
};

}   // namespace daedalus

#endif   // DAEDALUS_TREES_SPLAY_TREE_HPP
