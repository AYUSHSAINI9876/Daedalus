// ============================================================================
//  Daedalus :: trees/BinaryTree.hpp
//
//  A general binary tree -- no ordering invariant, arbitrary shape. The search
//  trees elsewhere in this directory answer questions using the BST property;
//  the algorithms here cannot, so they are genuinely different work: LCA by
//  post-order search rather than by comparison, path enumeration, symmetry,
//  reconstruction from traversals, and serialisation.
//
//  Trees are built from a level-order listing with std::nullopt for absent
//  children, the same notation LeetCode-style problems use:
//
//      BinaryTree<int>::fromLevelOrder({1, 2, 3, std::nullopt, 4})
//
//  Complexity: every algorithm below is a single O(n) walk unless noted.
// ============================================================================
#ifndef DAEDALUS_TREES_BINARY_TREE_HPP
#define DAEDALUS_TREES_BINARY_TREE_HPP

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"
#include "daedalus/linear/Deque.hpp"

namespace daedalus {

template <typename T>
struct GeneralNode {
    T value;
    GeneralNode* left{nullptr};
    GeneralNode* right{nullptr};
    explicit GeneralNode(const T& v) : value(v) {}
};

template <typename T>
class BinaryTree final : public Container<T> {
    using Node = GeneralNode<T>;

public:
    using value_type = T;
    using size_type = std::size_t;

    BinaryTree() = default;

    BinaryTree(const BinaryTree& other) : root_(cloneSubtree(other.root_)), size_(other.size_) {}

    BinaryTree(BinaryTree&& other) noexcept : root_(other.root_), size_(other.size_) {
        other.root_ = nullptr;
        other.size_ = 0;
    }

    BinaryTree& operator=(BinaryTree other) noexcept {
        std::swap(root_, other.root_);
        std::swap(size_, other.size_);
        return *this;
    }

    ~BinaryTree() override { clear(); }

    // --- construction --------------------------------------------------------

    /// Builds from a level-order listing where std::nullopt marks a missing
    /// child. Slots below a missing node are simply absent from the listing.
    static BinaryTree fromLevelOrder(const std::vector<std::optional<T>>& listing) {
        BinaryTree tree;
        if (listing.empty() || !listing[0].has_value()) return tree;

        tree.root_ = new Node(*listing[0]);
        tree.size_ = 1;
        Deque<Node*> pending;
        pending.pushBack(tree.root_);

        std::size_t cursor = 1;
        while (!pending.empty() && cursor < listing.size()) {
            Node* parent = pending.popFront();
            if (cursor < listing.size()) {
                if (listing[cursor].has_value()) {
                    parent->left = new Node(*listing[cursor]);
                    ++tree.size_;
                    pending.pushBack(parent->left);
                }
                ++cursor;
            }
            if (cursor < listing.size()) {
                if (listing[cursor].has_value()) {
                    parent->right = new Node(*listing[cursor]);
                    ++tree.size_;
                    pending.pushBack(parent->right);
                }
                ++cursor;
            }
        }
        return tree;
    }

    /// Reconstructs the unique tree with the given pre-order and in-order
    /// walks. Requires distinct values -- with duplicates the pair does not
    /// determine a single tree.
    static BinaryTree fromPreorderAndInorder(const std::vector<T>& preorder,
                                             const std::vector<T>& inorder) {
        require(preorder.size() == inorder.size(),
                "pre-order and in-order traversals must have the same length");
        BinaryTree tree;
        std::size_t cursor = 0;
        tree.root_ = buildFromTraversals(preorder, inorder, cursor, 0, inorder.size());
        tree.size_ = preorder.size();
        return tree;
    }

    // --- observers -----------------------------------------------------------

    [[nodiscard]] size_type size() const noexcept override { return size_; }
    [[nodiscard]] bool empty() const noexcept override { return size_ == 0; }
    [[nodiscard]] std::string name() const override { return "BinaryTree"; }

    void clear() override {
        destroySubtree(root_);
        root_ = nullptr;
        size_ = 0;
    }

    [[nodiscard]] int height() const { return subtreeHeight(root_); }

    [[nodiscard]] std::vector<T> preOrder() const {
        std::vector<T> out;
        walk(root_, out, 0);
        return out;
    }

    [[nodiscard]] std::vector<T> inOrder() const {
        std::vector<T> out;
        walk(root_, out, 1);
        return out;
    }

    [[nodiscard]] std::vector<T> postOrder() const {
        std::vector<T> out;
        walk(root_, out, 2);
        return out;
    }

    [[nodiscard]] std::vector<std::vector<T>> levels() const {
        std::vector<std::vector<T>> out;
        if (root_ == nullptr) return out;
        Deque<const Node*> queue;
        queue.pushBack(root_);
        while (!queue.empty()) {
            const std::size_t width = queue.size();
            std::vector<T> level;
            level.reserve(width);
            for (std::size_t i = 0; i < width; ++i) {
                const Node* node = queue.popFront();
                level.push_back(node->value);
                if (node->left != nullptr) queue.pushBack(node->left);
                if (node->right != nullptr) queue.pushBack(node->right);
            }
            out.push_back(std::move(level));
        }
        return out;
    }

    /// Level order with alternating direction, left-to-right on even depths.
    [[nodiscard]] std::vector<std::vector<T>> zigzagLevels() const {
        std::vector<std::vector<T>> out = levels();
        for (std::size_t depth = 1; depth < out.size(); depth += 2) {
            std::reverse(out[depth].begin(), out[depth].end());
        }
        return out;
    }

    /// The nodes visible when the tree is viewed from the right-hand side,
    /// i.e. the last node of each level.
    [[nodiscard]] std::vector<T> rightSideView() const {
        std::vector<T> out;
        for (const auto& level : levels()) out.push_back(level.back());
        return out;
    }

    [[nodiscard]] bool contains(const T& value) const { return findNode(root_, value) != nullptr; }

    [[nodiscard]] std::size_t countLeaves() const { return countLeavesIn(root_); }

    [[nodiscard]] bool isBalanced() const { return balancedHeight(root_) >= -1; }

    /// A tree that mirrors itself across the root.
    [[nodiscard]] bool isSymmetric() const { return mirrorEqual(root_, root_); }

    [[nodiscard]] bool isSameAs(const BinaryTree& other) const {
        return identical(root_, other.root_);
    }

    /// True when `candidate` appears somewhere as a complete subtree.
    [[nodiscard]] bool containsSubtree(const BinaryTree& candidate) const {
        if (candidate.root_ == nullptr) return true;
        return subtreeSearch(root_, candidate.root_);
    }

    // --- path algorithms -----------------------------------------------------

    /// Every root-to-leaf path, in left-to-right order.
    [[nodiscard]] std::vector<std::vector<T>> rootToLeafPaths() const {
        std::vector<std::vector<T>> out;
        std::vector<T> current;
        collectPaths(root_, current, out);
        return out;
    }

    /// True when some root-to-leaf path sums exactly to `target`.
    [[nodiscard]] bool hasPathSum(const T& target) const { return pathSumExists(root_, target); }

    /// Largest sum along any downward root-to-leaf path.
    [[nodiscard]] std::optional<T> maxRootToLeafSum() const {
        if (root_ == nullptr) return std::nullopt;
        return maxDownwardSum(root_);
    }

    /// Lowest common ancestor without any ordering assumption: the deepest node
    /// whose subtree contains both values.
    [[nodiscard]] std::optional<T> lowestCommonAncestor(const T& a, const T& b) const {
        if (!contains(a) || !contains(b)) return std::nullopt;
        const Node* found = lcaSearch(root_, a, b);
        if (found == nullptr) return std::nullopt;
        return found->value;
    }

    /// Distance in edges from the root to the node holding `value`.
    [[nodiscard]] std::optional<int> depthOf(const T& value) const {
        int depth = 0;
        if (!depthSearch(root_, value, 0, depth)) return std::nullopt;
        return depth;
    }

    // --- transformation ------------------------------------------------------

    /// Mirrors the tree in place: every node's children are swapped.
    void invert() noexcept { invertSubtree(root_); }

    // --- serialisation -------------------------------------------------------

    /// Pre-order with explicit null markers, which is enough to rebuild the
    /// exact shape. Values are rendered with the shared element formatter.
    [[nodiscard]] std::string serialize() const {
        std::ostringstream os;
        serializeInto(root_, os);
        std::string text = os.str();
        if (!text.empty() && text.back() == ',') text.pop_back();
        return text;
    }

    [[nodiscard]] std::string toString() const override {
        return name() + " " + joinElements(inOrder());
    }

private:
    static void destroySubtree(Node* node) noexcept {
        if (node == nullptr) return;
        destroySubtree(node->left);
        destroySubtree(node->right);
        delete node;
    }

    static Node* cloneSubtree(const Node* node) {
        if (node == nullptr) return nullptr;
        Node* copy = new Node(node->value);
        copy->left = cloneSubtree(node->left);
        copy->right = cloneSubtree(node->right);
        return copy;
    }

    /// order: 0 pre, 1 in, 2 post.
    static void walk(const Node* node, std::vector<T>& out, int order) {
        if (node == nullptr) return;
        if (order == 0) out.push_back(node->value);
        walk(node->left, out, order);
        if (order == 1) out.push_back(node->value);
        walk(node->right, out, order);
        if (order == 2) out.push_back(node->value);
    }

    static int subtreeHeight(const Node* node) {
        if (node == nullptr) return -1;
        return 1 + std::max(subtreeHeight(node->left), subtreeHeight(node->right));
    }

    static int balancedHeight(const Node* node) {
        if (node == nullptr) return -1;
        const int left = balancedHeight(node->left);
        if (left == -2) return -2;
        const int right = balancedHeight(node->right);
        if (right == -2) return -2;
        if ((left > right ? left - right : right - left) > 1) return -2;
        return 1 + std::max(left, right);
    }

    static std::size_t countLeavesIn(const Node* node) {
        if (node == nullptr) return 0;
        if (node->left == nullptr && node->right == nullptr) return 1;
        return countLeavesIn(node->left) + countLeavesIn(node->right);
    }

    static const Node* findNode(const Node* node, const T& value) {
        if (node == nullptr) return nullptr;
        if (node->value == value) return node;
        const Node* found = findNode(node->left, value);
        return found != nullptr ? found : findNode(node->right, value);
    }

    static bool mirrorEqual(const Node* a, const Node* b) {
        if (a == nullptr && b == nullptr) return true;
        if (a == nullptr || b == nullptr) return false;
        if (!(a->value == b->value)) return false;
        return mirrorEqual(a->left, b->right) && mirrorEqual(a->right, b->left);
    }

    static bool identical(const Node* a, const Node* b) {
        if (a == nullptr && b == nullptr) return true;
        if (a == nullptr || b == nullptr) return false;
        if (!(a->value == b->value)) return false;
        return identical(a->left, b->left) && identical(a->right, b->right);
    }

    static bool subtreeSearch(const Node* haystack, const Node* needle) {
        if (haystack == nullptr) return false;
        if (identical(haystack, needle)) return true;
        return subtreeSearch(haystack->left, needle) || subtreeSearch(haystack->right, needle);
    }

    static void collectPaths(const Node* node, std::vector<T>& current,
                             std::vector<std::vector<T>>& out) {
        if (node == nullptr) return;
        current.push_back(node->value);
        if (node->left == nullptr && node->right == nullptr) {
            out.push_back(current);
        } else {
            collectPaths(node->left, current, out);
            collectPaths(node->right, current, out);
        }
        current.pop_back();
    }

    static bool pathSumExists(const Node* node, const T& remaining) {
        if (node == nullptr) return false;
        if (node->left == nullptr && node->right == nullptr) return remaining == node->value;
        const T rest = remaining - node->value;
        return pathSumExists(node->left, rest) || pathSumExists(node->right, rest);
    }

    static T maxDownwardSum(const Node* node) {
        if (node->left == nullptr && node->right == nullptr) return node->value;
        if (node->left == nullptr) return node->value + maxDownwardSum(node->right);
        if (node->right == nullptr) return node->value + maxDownwardSum(node->left);
        return node->value + std::max(maxDownwardSum(node->left), maxDownwardSum(node->right));
    }

    static const Node* lcaSearch(const Node* node, const T& a, const T& b) {
        if (node == nullptr) return nullptr;
        if (node->value == a || node->value == b) return node;
        const Node* left = lcaSearch(node->left, a, b);
        const Node* right = lcaSearch(node->right, a, b);
        if (left != nullptr && right != nullptr) return node;   // split point
        return left != nullptr ? left : right;
    }

    static bool depthSearch(const Node* node, const T& value, int current, int& result) {
        if (node == nullptr) return false;
        if (node->value == value) {
            result = current;
            return true;
        }
        return depthSearch(node->left, value, current + 1, result) ||
               depthSearch(node->right, value, current + 1, result);
    }

    static void invertSubtree(Node* node) noexcept {
        if (node == nullptr) return;
        std::swap(node->left, node->right);
        invertSubtree(node->left);
        invertSubtree(node->right);
    }

    static void serializeInto(const Node* node, std::ostringstream& os) {
        if (node == nullptr) {
            os << "#,";
            return;
        }
        os << formatElement(node->value) << ",";
        serializeInto(node->left, os);
        serializeInto(node->right, os);
    }

    static Node* buildFromTraversals(const std::vector<T>& preorder, const std::vector<T>& inorder,
                                     std::size_t& cursor, std::size_t first, std::size_t last) {
        if (first >= last || cursor >= preorder.size()) return nullptr;
        const T& rootValue = preorder[cursor++];
        std::size_t split = first;
        while (split < last && !(inorder[split] == rootValue)) ++split;
        if (split == last) throw InvalidArgument("traversals are inconsistent");

        Node* node = new Node(rootValue);
        node->left = buildFromTraversals(preorder, inorder, cursor, first, split);
        node->right = buildFromTraversals(preorder, inorder, cursor, split + 1, last);
        return node;
    }

    Node* root_{nullptr};
    size_type size_{0};
};

}   // namespace daedalus

#endif   // DAEDALUS_TREES_BINARY_TREE_HPP
