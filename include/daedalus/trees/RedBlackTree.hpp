// ============================================================================
//  Daedalus :: trees/RedBlackTree.hpp
//
//  Classic CLRS red-black tree with parent pointers and nullptr standing in for
//  the nil sentinel (a value-carrying sentinel would force T to be default
//  constructible). The five invariants:
//
//    1. every node is red or black
//    2. the root is black
//    3. nullptr leaves count as black
//    4. a red node has no red child
//    5. every root-to-leaf path crosses the same number of black nodes
//
//  Together these bound the height at 2*log2(n+1), which is looser than AVL but
//  bought with far fewer rotations per write -- the reason std::map and
//  std::set are red-black rather than AVL.
//
//  verifyProperties() checks all five and is asserted after every mutation in
//  the randomised tests.
//
//  Complexity: search/insert/erase O(log n) worst case
//              rotations <= 2 per insert, <= 3 per erase
// ============================================================================
#ifndef DAEDALUS_TREES_RED_BLACK_TREE_HPP
#define DAEDALUS_TREES_RED_BLACK_TREE_HPP

#include <cstddef>
#include <initializer_list>
#include <string>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/trees/BinaryTreeBase.hpp"

namespace daedalus {

enum class RBColor { Red, Black };

template <typename T>
struct RBNode {
    T value;
    RBNode* left{nullptr};
    RBNode* right{nullptr};
    RBNode* parent{nullptr};
    RBColor color{RBColor::Red};  ///< new nodes start red; fixup may repaint

    explicit RBNode(const T& v) : value(v) {}
};

template <typename T>
    requires LessThanComparable<T>
class RedBlackTree final : public BinaryTreeBase<T, RBNode<T>> {
    using Base = BinaryTreeBase<T, RBNode<T>>;
    using Node = RBNode<T>;

public:
    RedBlackTree() = default;

    RedBlackTree(std::initializer_list<T> values) {
        for (const T& value : values) insert(value);
    }

    [[nodiscard]] std::string name() const override { return "RedBlackTree"; }

    void insert(const T& value) override {
        Node* parent = nullptr;
        Node* current = this->root_;
        while (current != nullptr) {
            parent = current;
            if (value < current->value) {
                current = current->left;
            } else if (current->value < value) {
                current = current->right;
            } else {
                return;  // duplicate
            }
        }

        Node* fresh = new Node(value);
        fresh->parent = parent;
        if (parent == nullptr) {
            this->root_ = fresh;
        } else if (value < parent->value) {
            parent->left = fresh;
        } else {
            parent->right = fresh;
        }
        ++this->size_;
        insertFixup(fresh);
    }

    bool erase(const T& value) override {
        Node* target = const_cast<Node*>(this->findNode(value));
        if (target == nullptr) return false;
        eraseNode(target);
        --this->size_;
        return true;
    }

    /// Black nodes on any root-to-leaf path, counting nullptr leaves.
    /// Returns -1 when the tree violates property 5.
    [[nodiscard]] int blackHeight() const { return blackHeightOf(this->root_); }

    /// Checks all five red-black properties plus the BST ordering.
    [[nodiscard]] bool verifyProperties() const {
        if (this->root_ != nullptr && this->root_->color != RBColor::Black) return false;
        if (!noRedRed(this->root_)) return false;
        if (blackHeightOf(this->root_) < 0) return false;
        if (!parentLinksConsistent(this->root_, nullptr)) return false;
        return this->isValidBST();
    }

private:
    [[nodiscard]] static RBColor colorOf(const Node* node) noexcept {
        return node == nullptr ? RBColor::Black : node->color;
    }

    [[nodiscard]] static bool isRed(const Node* node) noexcept {
        return colorOf(node) == RBColor::Red;
    }

    void rotateLeft(Node* x) noexcept {
        Node* y = x->right;
        x->right = y->left;
        if (y->left != nullptr) y->left->parent = x;
        y->parent = x->parent;
        if (x->parent == nullptr) {
            this->root_ = y;
        } else if (x == x->parent->left) {
            x->parent->left = y;
        } else {
            x->parent->right = y;
        }
        y->left = x;
        x->parent = y;
    }

    void rotateRight(Node* y) noexcept {
        Node* x = y->left;
        y->left = x->right;
        if (x->right != nullptr) x->right->parent = y;
        x->parent = y->parent;
        if (y->parent == nullptr) {
            this->root_ = x;
        } else if (y == y->parent->right) {
            y->parent->right = x;
        } else {
            y->parent->left = x;
        }
        x->right = y;
        y->parent = x;
    }

    /// Restores properties 2 and 4 after inserting the red node `z`.
    void insertFixup(Node* z) noexcept {
        while (z->parent != nullptr && z->parent->color == RBColor::Red) {
            Node* parent = z->parent;
            Node* grandparent = parent->parent;
            if (grandparent == nullptr) break;

            if (parent == grandparent->left) {
                Node* uncle = grandparent->right;
                if (isRed(uncle)) {
                    // Case 1: recolour and push the problem up two levels.
                    parent->color = RBColor::Black;
                    uncle->color = RBColor::Black;
                    grandparent->color = RBColor::Red;
                    z = grandparent;
                } else {
                    if (z == parent->right) {
                        // Case 2: turn the zig-zag into a straight line.
                        z = parent;
                        rotateLeft(z);
                    }
                    // Case 3: recolour and rotate the grandparent down.
                    z->parent->color = RBColor::Black;
                    z->parent->parent->color = RBColor::Red;
                    rotateRight(z->parent->parent);
                }
            } else {
                Node* uncle = grandparent->left;
                if (isRed(uncle)) {
                    parent->color = RBColor::Black;
                    uncle->color = RBColor::Black;
                    grandparent->color = RBColor::Red;
                    z = grandparent;
                } else {
                    if (z == parent->left) {
                        z = parent;
                        rotateRight(z);
                    }
                    z->parent->color = RBColor::Black;
                    z->parent->parent->color = RBColor::Red;
                    rotateLeft(z->parent->parent);
                }
            }
        }
        this->root_->color = RBColor::Black;
    }

    /// Replaces the subtree rooted at `u` with the one rooted at `v`.
    void transplant(Node* u, Node* v) noexcept {
        if (u->parent == nullptr) {
            this->root_ = v;
        } else if (u == u->parent->left) {
            u->parent->left = v;
        } else {
            u->parent->right = v;
        }
        if (v != nullptr) v->parent = u->parent;
    }

    void eraseNode(Node* z) noexcept {
        Node* movedUp = nullptr;        // the node that took the removed slot
        Node* movedUpParent = nullptr;  // tracked explicitly, movedUp may be null
        RBColor removedColor = z->color;

        if (z->left == nullptr) {
            movedUp = z->right;
            movedUpParent = z->parent;
            transplant(z, z->right);
        } else if (z->right == nullptr) {
            movedUp = z->left;
            movedUpParent = z->parent;
            transplant(z, z->left);
        } else {
            Node* successor = z->right;
            while (successor->left != nullptr) successor = successor->left;
            removedColor = successor->color;
            movedUp = successor->right;

            if (successor->parent == z) {
                movedUpParent = successor;
            } else {
                movedUpParent = successor->parent;
                transplant(successor, successor->right);
                successor->right = z->right;
                successor->right->parent = successor;
            }
            transplant(z, successor);
            successor->left = z->left;
            successor->left->parent = successor;
            successor->color = z->color;
        }

        delete z;
        if (removedColor == RBColor::Black) eraseFixup(movedUp, movedUpParent);
    }

    /// `x` carries an extra black. Push it up until it lands on a red node or
    /// reaches the root. `x` may be null, hence the explicit parent.
    void eraseFixup(Node* x, Node* parent) noexcept {
        while (x != this->root_ && !isRed(x)) {
            if (parent == nullptr) break;

            if (x == parent->left) {
                Node* sibling = parent->right;
                if (sibling == nullptr) break;  // cannot happen in a valid tree

                if (isRed(sibling)) {
                    // Case 1: recolour so the sibling becomes black.
                    sibling->color = RBColor::Black;
                    parent->color = RBColor::Red;
                    rotateLeft(parent);
                    sibling = parent->right;
                    if (sibling == nullptr) break;
                }
                if (!isRed(sibling->left) && !isRed(sibling->right)) {
                    // Case 2: give the extra black to the parent.
                    sibling->color = RBColor::Red;
                    x = parent;
                    parent = x->parent;
                } else {
                    if (!isRed(sibling->right)) {
                        // Case 3: rotate the red child to the outside.
                        if (sibling->left != nullptr) sibling->left->color = RBColor::Black;
                        sibling->color = RBColor::Red;
                        rotateRight(sibling);
                        sibling = parent->right;
                    }
                    // Case 4: terminal rotation, the extra black is absorbed.
                    sibling->color = parent->color;
                    parent->color = RBColor::Black;
                    if (sibling->right != nullptr) sibling->right->color = RBColor::Black;
                    rotateLeft(parent);
                    x = this->root_;
                    parent = nullptr;
                }
            } else {
                Node* sibling = parent->left;
                if (sibling == nullptr) break;

                if (isRed(sibling)) {
                    sibling->color = RBColor::Black;
                    parent->color = RBColor::Red;
                    rotateRight(parent);
                    sibling = parent->left;
                    if (sibling == nullptr) break;
                }
                if (!isRed(sibling->left) && !isRed(sibling->right)) {
                    sibling->color = RBColor::Red;
                    x = parent;
                    parent = x->parent;
                } else {
                    if (!isRed(sibling->left)) {
                        if (sibling->right != nullptr) sibling->right->color = RBColor::Black;
                        sibling->color = RBColor::Red;
                        rotateLeft(sibling);
                        sibling = parent->left;
                    }
                    sibling->color = parent->color;
                    parent->color = RBColor::Black;
                    if (sibling->left != nullptr) sibling->left->color = RBColor::Black;
                    rotateRight(parent);
                    x = this->root_;
                    parent = nullptr;
                }
            }
        }
        if (x != nullptr) x->color = RBColor::Black;
        if (this->root_ != nullptr) this->root_->color = RBColor::Black;
    }

    // --- invariant checking ---------------------------------------------------

    static bool noRedRed(const Node* node) {
        if (node == nullptr) return true;
        if (isRed(node) && (isRed(node->left) || isRed(node->right))) return false;
        return noRedRed(node->left) && noRedRed(node->right);
    }

    /// Black height including the nullptr leaf, or -1 if the two sides differ.
    static int blackHeightOf(const Node* node) {
        if (node == nullptr) return 1;
        const int left = blackHeightOf(node->left);
        if (left < 0) return -1;
        const int right = blackHeightOf(node->right);
        if (right < 0 || left != right) return -1;
        return left + (node->color == RBColor::Black ? 1 : 0);
    }

    static bool parentLinksConsistent(const Node* node, const Node* expectedParent) {
        if (node == nullptr) return true;
        if (node->parent != expectedParent) return false;
        return parentLinksConsistent(node->left, node) &&
               parentLinksConsistent(node->right, node);
    }
};

}  // namespace daedalus

#endif  // DAEDALUS_TREES_RED_BLACK_TREE_HPP
