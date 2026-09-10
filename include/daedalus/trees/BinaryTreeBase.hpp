// ============================================================================
//  Daedalus :: trees/BinaryTreeBase.hpp
//
//  Everything the five search trees share, written once. The node type is a
//  template parameter rather than a fixed struct, so AVL can carry a height,
//  red-black a colour and treap a priority without any of them paying for the
//  others' fields -- while traversal, validation, rendering and the order
//  queries below are implemented a single time.
//
//  The trees remain runtime-polymorphic through SortedSet<T>: a caller can
//  hold std::unique_ptr<SortedSet<int>> and swap AVL for red-black freely.
//
//  Traversals are provided both iteratively (explicit stack) and, for in-order,
//  via Morris threading in O(1) extra space.
// ============================================================================
#ifndef DAEDALUS_TREES_BINARY_TREE_BASE_HPP
#define DAEDALUS_TREES_BINARY_TREE_BASE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"
#include "daedalus/linear/Deque.hpp"
#include "daedalus/linear/DynamicArray.hpp"

namespace daedalus {

enum class TraversalOrder { PreOrder, InOrder, PostOrder, LevelOrder };

[[nodiscard]] inline std::string toString(TraversalOrder order) {
    switch (order) {
        case TraversalOrder::PreOrder: return "pre-order";
        case TraversalOrder::InOrder: return "in-order";
        case TraversalOrder::PostOrder: return "post-order";
        case TraversalOrder::LevelOrder: return "level-order";
    }
    return "unknown";
}

/// Visitor pattern: lets callers run their own logic at each node without the
/// tree exposing its node type.
template <typename T>
class TreeVisitor {
public:
    virtual ~TreeVisitor() = default;
    virtual void visit(const T& value, int depth) = 0;
};

/// Collects values (and the depth each was found at) during a traversal.
template <typename T>
class CollectingVisitor final : public TreeVisitor<T> {
public:
    void visit(const T& value, int depth) override {
        values.push_back(value);
        depths.push_back(depth);
    }
    std::vector<T> values;
    std::vector<int> depths;
};

/// Applies a plain callable, so lambdas work without defining a class.
template <typename T>
class FunctionVisitor final : public TreeVisitor<T> {
public:
    explicit FunctionVisitor(std::function<void(const T&, int)> action)
        : action_(std::move(action)) {}
    void visit(const T& value, int depth) override { action_(value, depth); }

private:
    std::function<void(const T&, int)> action_;
};

// ---------------------------------------------------------------------------

template <typename T, typename NodeT>
    requires LessThanComparable<T>
class BinaryTreeBase : public SortedSet<T> {
public:
    using value_type = T;
    using size_type = std::size_t;
    using node_type = NodeT;

    BinaryTreeBase() = default;
    BinaryTreeBase(const BinaryTreeBase&) = delete;
    BinaryTreeBase& operator=(const BinaryTreeBase&) = delete;
    BinaryTreeBase(BinaryTreeBase&&) = delete;
    BinaryTreeBase& operator=(BinaryTreeBase&&) = delete;

    /// Frees every node. Safe in the base because node destruction depends on
    /// NodeT (a static type) and never on derived-class state.
    ~BinaryTreeBase() override {
        destroySubtree(root_);
        root_ = nullptr;
    }

    // --- observers -----------------------------------------------------------

    [[nodiscard]] size_type size() const noexcept override { return size_; }
    [[nodiscard]] bool empty() const noexcept override { return size_ == 0; }

    void clear() override {
        destroySubtree(root_);
        root_ = nullptr;
        size_ = 0;
    }

    /// Height in edges; an empty tree is -1 and a single node is 0.
    /// Balanced trees override this with their O(1) bookkeeping.
    [[nodiscard]] int height() const override { return subtreeHeight(root_); }

    [[nodiscard]] bool contains(const T& value) const override {
        return findNode(value) != nullptr;
    }

    [[nodiscard]] std::optional<T> minimum() const override {
        const NodeT* node = leftmost(root_);
        if (node == nullptr) return std::nullopt;
        return node->value;
    }

    [[nodiscard]] std::optional<T> maximum() const override {
        const NodeT* node = rightmost(root_);
        if (node == nullptr) return std::nullopt;
        return node->value;
    }

    [[nodiscard]] std::vector<T> toVector() const override {
        return traversal(TraversalOrder::InOrder);
    }

    // --- traversals ----------------------------------------------------------

    [[nodiscard]] std::vector<T> traversal(TraversalOrder order) const {
        switch (order) {
            case TraversalOrder::PreOrder: return preOrder();
            case TraversalOrder::InOrder: return inOrder();
            case TraversalOrder::PostOrder: return postOrder();
            case TraversalOrder::LevelOrder: return levelOrder();
        }
        return {};
    }

    /// Iterative pre-order using an explicit stack.
    [[nodiscard]] std::vector<T> preOrder() const {
        std::vector<T> out;
        out.reserve(size_);
        if (root_ == nullptr) return out;
        DynamicArray<const NodeT*> stack;
        stack.pushBack(root_);
        while (!stack.empty()) {
            const NodeT* node = stack.popBack();
            out.push_back(node->value);
            if (node->right != nullptr) stack.pushBack(node->right);
            if (node->left != nullptr) stack.pushBack(node->left);
        }
        return out;
    }

    /// Iterative in-order. On a search tree this yields sorted output, which is
    /// what toVector() and every equality test in the suite rely on.
    [[nodiscard]] std::vector<T> inOrder() const {
        std::vector<T> out;
        out.reserve(size_);
        DynamicArray<const NodeT*> stack;
        const NodeT* current = root_;
        while (current != nullptr || !stack.empty()) {
            while (current != nullptr) {
                stack.pushBack(current);
                current = current->left;
            }
            const NodeT* node = stack.popBack();
            out.push_back(node->value);
            current = node->right;
        }
        return out;
    }

    /// Iterative post-order via the reversed "root, right, left" walk.
    [[nodiscard]] std::vector<T> postOrder() const {
        std::vector<T> out;
        out.reserve(size_);
        if (root_ == nullptr) return out;
        DynamicArray<const NodeT*> stack;
        stack.pushBack(root_);
        while (!stack.empty()) {
            const NodeT* node = stack.popBack();
            out.push_back(node->value);
            if (node->left != nullptr) stack.pushBack(node->left);
            if (node->right != nullptr) stack.pushBack(node->right);
        }
        std::reverse(out.begin(), out.end());
        return out;
    }

    /// Breadth-first, left to right.
    [[nodiscard]] std::vector<T> levelOrder() const {
        std::vector<T> out;
        out.reserve(size_);
        if (root_ == nullptr) return out;
        Deque<const NodeT*> queue;
        queue.pushBack(root_);
        while (!queue.empty()) {
            const NodeT* node = queue.popFront();
            out.push_back(node->value);
            if (node->left != nullptr) queue.pushBack(node->left);
            if (node->right != nullptr) queue.pushBack(node->right);
        }
        return out;
    }

    /// Level-order grouped one vector per depth.
    [[nodiscard]] std::vector<std::vector<T>> levels() const {
        std::vector<std::vector<T>> out;
        if (root_ == nullptr) return out;
        Deque<const NodeT*> queue;
        queue.pushBack(root_);
        while (!queue.empty()) {
            const std::size_t width = queue.size();
            std::vector<T> level;
            level.reserve(width);
            for (std::size_t i = 0; i < width; ++i) {
                const NodeT* node = queue.popFront();
                level.push_back(node->value);
                if (node->left != nullptr) queue.pushBack(node->left);
                if (node->right != nullptr) queue.pushBack(node->right);
            }
            out.push_back(std::move(level));
        }
        return out;
    }

    /// Morris in-order traversal: threads each node's predecessor to its
    /// successor, walks, then unthreads. O(n) time in O(1) extra space, the
    /// one traversal that needs neither a stack nor recursion.
    [[nodiscard]] std::vector<T> morrisInOrder() const {
        std::vector<T> out;
        out.reserve(size_);
        NodeT* current = root_;
        while (current != nullptr) {
            if (current->left == nullptr) {
                out.push_back(current->value);
                current = current->right;
                continue;
            }
            NodeT* predecessor = current->left;
            while (predecessor->right != nullptr && predecessor->right != current) {
                predecessor = predecessor->right;
            }
            if (predecessor->right == nullptr) {
                predecessor->right = current;   // thread
                current = current->left;
            } else {
                predecessor->right = nullptr;   // unthread, subtree done
                out.push_back(current->value);
                current = current->right;
            }
        }
        return out;
    }

    /// Runs a visitor over the tree in the requested order, passing depth.
    void accept(TreeVisitor<T>& visitor, TraversalOrder order = TraversalOrder::InOrder) const {
        if (order == TraversalOrder::LevelOrder) {
            acceptLevelOrder(visitor);
            return;
        }
        acceptRecursive(root_, visitor, order, 0);
    }

    // --- structural queries --------------------------------------------------

    /// Verifies the search-tree invariant across the whole tree. Used by the
    /// tests after every insert/erase sequence.
    [[nodiscard]] bool isValidBST() const { return checkBST(root_, nullptr, nullptr); }

    /// True when no node's subtree heights differ by more than one.
    [[nodiscard]] bool isBalanced() const { return balancedHeight(root_) >= -1; }

    [[nodiscard]] std::size_t countLeaves() const { return countLeavesIn(root_); }

    [[nodiscard]] std::size_t countInternalNodes() const { return size_ - countLeaves(); }

    /// Longest path between any two nodes, measured in edges.
    [[nodiscard]] int diameter() const {
        int best = 0;
        (void)diameterHelper(root_, best);
        return best;
    }

    /// k is 1-based; kthSmallest(1) is minimum().
    [[nodiscard]] std::optional<T> kthSmallest(std::size_t k) const {
        if (k == 0 || k > size_) return std::nullopt;
        const std::vector<T> sorted = inOrder();
        return sorted[k - 1];
    }

    [[nodiscard]] std::optional<T> kthLargest(std::size_t k) const {
        if (k == 0 || k > size_) return std::nullopt;
        return kthSmallest(size_ - k + 1);
    }

    /// Smallest stored key strictly greater than `value`.
    [[nodiscard]] std::optional<T> successor(const T& value) const {
        const NodeT* candidate = nullptr;
        const NodeT* node = root_;
        while (node != nullptr) {
            if (value < node->value) {
                candidate = node;
                node = node->left;
            } else {
                node = node->right;
            }
        }
        if (candidate == nullptr) return std::nullopt;
        return candidate->value;
    }

    /// Largest stored key strictly less than `value`.
    [[nodiscard]] std::optional<T> predecessor(const T& value) const {
        const NodeT* candidate = nullptr;
        const NodeT* node = root_;
        while (node != nullptr) {
            if (node->value < value) {
                candidate = node;
                node = node->right;
            } else {
                node = node->left;
            }
        }
        if (candidate == nullptr) return std::nullopt;
        return candidate->value;
    }

    /// Lowest common ancestor, exploiting the BST ordering: the first node
    /// whose key lies between the two targets is their meeting point.
    [[nodiscard]] std::optional<T> lowestCommonAncestor(const T& a, const T& b) const {
        if (!contains(a) || !contains(b)) return std::nullopt;
        const T& low = (a < b) ? a : b;
        const T& high = (a < b) ? b : a;
        const NodeT* node = root_;
        while (node != nullptr) {
            if (high < node->value) {
                node = node->left;
            } else if (node->value < low) {
                node = node->right;
            } else {
                return node->value;
            }
        }
        return std::nullopt;
    }

    /// Every key in [low, high], in ascending order.
    [[nodiscard]] std::vector<T> rangeQuery(const T& low, const T& high) const {
        std::vector<T> out;
        collectRange(root_, low, high, out);
        return out;
    }

    /// Sideways ASCII rendering: the right subtree prints above its parent and
    /// the left below, so the page reads like the drawing on a whiteboard.
    [[nodiscard]] std::string prettyPrint() const {
        if (root_ == nullptr) return "(empty)\n";
        std::ostringstream os;
        render(root_, "", 0, os);
        return os.str();
    }

    [[nodiscard]] std::string toString() const override {
        return this->name() + " " + joinElements(toVector());
    }

protected:
    // --- node helpers shared by every derived tree ---------------------------

    static void destroySubtree(NodeT* node) noexcept {
        if (node == nullptr) return;
        destroySubtree(node->left);
        destroySubtree(node->right);
        delete node;
    }

    [[nodiscard]] static int subtreeHeight(const NodeT* node) {
        if (node == nullptr) return -1;
        return 1 + std::max(subtreeHeight(node->left), subtreeHeight(node->right));
    }

    [[nodiscard]] static NodeT* leftmost(NodeT* node) noexcept {
        while (node != nullptr && node->left != nullptr) node = node->left;
        return node;
    }

    [[nodiscard]] static const NodeT* leftmost(const NodeT* node) noexcept {
        while (node != nullptr && node->left != nullptr) node = node->left;
        return node;
    }

    [[nodiscard]] static NodeT* rightmost(NodeT* node) noexcept {
        while (node != nullptr && node->right != nullptr) node = node->right;
        return node;
    }

    [[nodiscard]] static const NodeT* rightmost(const NodeT* node) noexcept {
        while (node != nullptr && node->right != nullptr) node = node->right;
        return node;
    }

    /// Plain BST descent, correct for every tree in the hierarchy. Splay
    /// overrides it because a lookup there also restructures the tree.
    [[nodiscard]] virtual const NodeT* findNode(const T& value) const {
        const NodeT* node = root_;
        while (node != nullptr) {
            if (value < node->value) {
                node = node->left;
            } else if (node->value < value) {
                node = node->right;
            } else {
                return node;
            }
        }
        return nullptr;
    }

    NodeT* root_{nullptr};
    size_type size_{0};

private:
    static bool checkBST(const NodeT* node, const T* lowerBound, const T* upperBound) {
        if (node == nullptr) return true;
        if (lowerBound != nullptr && !(*lowerBound < node->value)) return false;
        if (upperBound != nullptr && !(node->value < *upperBound)) return false;
        return checkBST(node->left, lowerBound, &node->value) &&
               checkBST(node->right, &node->value, upperBound);
    }

    /// Returns the subtree height, or -2 as a sentinel meaning "unbalanced".
    static int balancedHeight(const NodeT* node) {
        if (node == nullptr) return -1;
        const int left = balancedHeight(node->left);
        if (left == -2) return -2;
        const int right = balancedHeight(node->right);
        if (right == -2) return -2;
        if (std::abs(left - right) > 1) return -2;
        return 1 + std::max(left, right);
    }

    static std::size_t countLeavesIn(const NodeT* node) {
        if (node == nullptr) return 0;
        if (node->left == nullptr && node->right == nullptr) return 1;
        return countLeavesIn(node->left) + countLeavesIn(node->right);
    }

    static int diameterHelper(const NodeT* node, int& best) {
        if (node == nullptr) return -1;
        const int left = diameterHelper(node->left, best);
        const int right = diameterHelper(node->right, best);
        best = std::max(best, left + right + 2);
        return 1 + std::max(left, right);
    }

    static void collectRange(const NodeT* node, const T& low, const T& high, std::vector<T>& out) {
        if (node == nullptr) return;
        if (low < node->value) collectRange(node->left, low, high, out);
        if (!(node->value < low) && !(high < node->value)) out.push_back(node->value);
        if (node->value < high) collectRange(node->right, low, high, out);
    }

    static void acceptRecursive(const NodeT* node, TreeVisitor<T>& visitor, TraversalOrder order,
                                int depth) {
        if (node == nullptr) return;
        if (order == TraversalOrder::PreOrder) visitor.visit(node->value, depth);
        acceptRecursive(node->left, visitor, order, depth + 1);
        if (order == TraversalOrder::InOrder) visitor.visit(node->value, depth);
        acceptRecursive(node->right, visitor, order, depth + 1);
        if (order == TraversalOrder::PostOrder) visitor.visit(node->value, depth);
    }

    void acceptLevelOrder(TreeVisitor<T>& visitor) const {
        if (root_ == nullptr) return;
        Deque<std::pair<const NodeT*, int>> queue;
        queue.pushBack({root_, 0});
        while (!queue.empty()) {
            const auto entry = queue.popFront();
            visitor.visit(entry.first->value, entry.second);
            if (entry.first->left != nullptr) queue.pushBack({entry.first->left, entry.second + 1});
            if (entry.first->right != nullptr) {
                queue.pushBack({entry.first->right, entry.second + 1});
            }
        }
    }

    /// side: 0 root, 1 right child, -1 left child.
    static void render(const NodeT* node, const std::string& prefix, int side,
                       std::ostringstream& os) {
        if (node == nullptr) return;
        const std::string childPrefix = prefix + (side == 0 ? "" : (side > 0 ? "|   " : "    "));
        render(node->right, prefix + (side == 0 ? "" : (side > 0 ? "    " : "|   ")), 1, os);
        os << prefix;
        if (side > 0) os << ".-- ";
        if (side < 0) os << "`-- ";
        os << formatElement(node->value) << "\n";
        render(node->left, childPrefix, -1, os);
    }
};

}   // namespace daedalus

#endif   // DAEDALUS_TREES_BINARY_TREE_BASE_HPP
