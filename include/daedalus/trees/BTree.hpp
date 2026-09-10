// ============================================================================
//  Daedalus :: trees/BTree.hpp
//
//  B-tree of minimum degree t: every node except the root holds between t-1 and
//  2t-1 keys, and all leaves sit at the same depth. Fanout is the point -- with
//  t = 128 a million keys are three levels deep, so a database or filesystem
//  index reaches any record in three block reads instead of the twenty a binary
//  tree would need.
//
//  Both halves of the algorithm are implemented in full: insertion splits full
//  children on the way down, and deletion restores the minimum occupancy with
//  the borrow-from-sibling and merge cases from CLRS chapter 18.
//
//  Complexity: search/insert/erase O(log_t n) node visits, O(t log_t n) compares
// ============================================================================
#ifndef DAEDALUS_TREES_B_TREE_HPP
#define DAEDALUS_TREES_B_TREE_HPP

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"

namespace daedalus {

template <typename T>
    requires LessThanComparable<T>
class BTree final : public SortedSet<T> {
    struct Node {
        std::vector<T> keys;
        std::vector<Node*> children;   ///< empty when `leaf`
        bool leaf{true};

        ~Node() {
            for (Node* child : children) delete child;
        }
    };

public:
    using value_type = T;
    using size_type = std::size_t;

    /// `minimumDegree` is CLRS's t and must be at least 2.
    explicit BTree(std::size_t minimumDegree = 3) : minDegree_(minimumDegree) {
        require(minimumDegree >= 2, "B-tree minimum degree must be at least 2");
        root_ = new Node();
    }

    BTree(std::initializer_list<T> values, std::size_t minimumDegree = 3) : BTree(minimumDegree) {
        for (const T& value : values) insert(value);
    }

    BTree(const BTree& other) : BTree(other.minDegree_) {
        for (const T& value : other.toVector()) insert(value);
    }

    BTree(BTree&& other) noexcept
        : root_(other.root_), minDegree_(other.minDegree_), size_(other.size_) {
        other.root_ = new Node();
        other.size_ = 0;
    }

    BTree& operator=(BTree other) noexcept {
        std::swap(root_, other.root_);
        std::swap(minDegree_, other.minDegree_);
        std::swap(size_, other.size_);
        return *this;
    }

    ~BTree() override {
        delete root_;
        root_ = nullptr;
    }

    // --- observers -----------------------------------------------------------

    [[nodiscard]] size_type size() const noexcept override { return size_; }
    [[nodiscard]] bool empty() const noexcept override { return size_ == 0; }
    [[nodiscard]] std::string name() const override { return "BTree"; }
    [[nodiscard]] std::size_t minimumDegree() const noexcept { return minDegree_; }

    /// Depth in edges; every leaf is at this depth, which verify() checks.
    [[nodiscard]] int height() const override {
        int levels = 0;
        for (const Node* node = root_; !node->leaf; node = node->children[0]) ++levels;
        return size_ == 0 ? -1 : levels;
    }

    void clear() override {
        delete root_;
        root_ = new Node();
        size_ = 0;
    }

    [[nodiscard]] bool contains(const T& value) const override {
        const Node* node = root_;
        while (node != nullptr) {
            const std::size_t index = lowerBoundIndex(node->keys, value);
            if (index < node->keys.size() && !(value < node->keys[index])) return true;
            if (node->leaf) return false;
            node = node->children[index];
        }
        return false;
    }

    [[nodiscard]] std::optional<T> minimum() const override {
        if (size_ == 0) return std::nullopt;
        const Node* node = root_;
        while (!node->leaf) node = node->children.front();
        return node->keys.front();
    }

    [[nodiscard]] std::optional<T> maximum() const override {
        if (size_ == 0) return std::nullopt;
        const Node* node = root_;
        while (!node->leaf) node = node->children.back();
        return node->keys.back();
    }

    [[nodiscard]] std::vector<T> toVector() const override {
        std::vector<T> out;
        out.reserve(size_);
        traverse(root_, out);
        return out;
    }

    /// Number of nodes, useful for showing how flat the tree stays.
    [[nodiscard]] std::size_t nodeCount() const { return countNodes(root_); }

    /// Checks every B-tree invariant: key ordering inside each node, the
    /// [t-1, 2t-1] occupancy bound for non-root nodes, the child-count
    /// relationship, and that all leaves share one depth.
    [[nodiscard]] bool verifyProperties() const {
        if (root_->keys.size() > 2 * minDegree_ - 1) return false;
        int leafDepth = -1;
        if (!checkNode(root_, true, 0, leafDepth)) return false;
        const std::vector<T> sorted = toVector();
        return std::is_sorted(sorted.begin(), sorted.end()) && sorted.size() == size_;
    }

    // --- mutation ------------------------------------------------------------

    /// Pre-emptive splitting: any full node met on the way down is split before
    /// descending, so an insert never has to walk back up.
    void insert(const T& value) override {
        if (contains(value)) return;

        if (root_->keys.size() == 2 * minDegree_ - 1) {
            Node* freshRoot = new Node();
            freshRoot->leaf = false;
            freshRoot->children.push_back(root_);
            root_ = freshRoot;
            splitChild(freshRoot, 0);
        }
        insertNonFull(root_, value);
        ++size_;
    }

    bool erase(const T& value) override {
        if (!contains(value)) return false;
        removeFrom(root_, value);
        // The root may have been emptied by a merge; drop that level.
        if (root_->keys.empty() && !root_->leaf) {
            Node* oldRoot = root_;
            root_ = oldRoot->children[0];
            oldRoot->children.clear();   // ownership transferred
            delete oldRoot;
        }
        --size_;
        return true;
    }

private:
    // --- small helpers -------------------------------------------------------

    [[nodiscard]] static std::size_t lowerBoundIndex(const std::vector<T>& keys, const T& value) {
        std::size_t index = 0;
        while (index < keys.size() && keys[index] < value) ++index;
        return index;
    }

    static void traverse(const Node* node, std::vector<T>& out) {
        if (node == nullptr) return;
        for (std::size_t i = 0; i < node->keys.size(); ++i) {
            if (!node->leaf) traverse(node->children[i], out);
            out.push_back(node->keys[i]);
        }
        if (!node->leaf) traverse(node->children.back(), out);
    }

    static std::size_t countNodes(const Node* node) {
        std::size_t total = 1;
        for (const Node* child : node->children) total += countNodes(child);
        return total;
    }

    bool checkNode(const Node* node, bool isRoot, int depth, int& leafDepth) const {
        if (!std::is_sorted(node->keys.begin(), node->keys.end())) return false;
        if (!isRoot && node->keys.size() < minDegree_ - 1) return false;
        if (node->keys.size() > 2 * minDegree_ - 1) return false;

        if (node->leaf) {
            if (!node->children.empty()) return false;
            if (leafDepth == -1) leafDepth = depth;
            return leafDepth == depth;
        }
        if (node->children.size() != node->keys.size() + 1) return false;
        for (const Node* child : node->children) {
            if (!checkNode(child, false, depth + 1, leafDepth)) return false;
        }
        return true;
    }

    // --- insertion -----------------------------------------------------------

    /// Splits the full child at `index`, lifting its median into `parent`.
    void splitChild(Node* parent, std::size_t index) {
        Node* full = parent->children[index];
        Node* right = new Node();
        right->leaf = full->leaf;

        const std::size_t median = minDegree_ - 1;
        right->keys.assign(full->keys.begin() + static_cast<std::ptrdiff_t>(median) + 1,
                           full->keys.end());
        if (!full->leaf) {
            right->children.assign(full->children.begin() + static_cast<std::ptrdiff_t>(minDegree_),
                                   full->children.end());
            full->children.resize(minDegree_);
        }
        const T medianKey = full->keys[median];
        full->keys.resize(median);

        parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(index) + 1,
                                right);
        parent->keys.insert(parent->keys.begin() + static_cast<std::ptrdiff_t>(index), medianKey);
    }

    void insertNonFull(Node* node, const T& value) {
        std::size_t index = lowerBoundIndex(node->keys, value);
        if (node->leaf) {
            node->keys.insert(node->keys.begin() + static_cast<std::ptrdiff_t>(index), value);
            return;
        }
        if (node->children[index]->keys.size() == 2 * minDegree_ - 1) {
            splitChild(node, index);
            if (node->keys[index] < value) ++index;
        }
        insertNonFull(node->children[index], value);
    }

    // --- deletion ------------------------------------------------------------

    void removeFrom(Node* node, const T& value) {
        const std::size_t index = lowerBoundIndex(node->keys, value);
        const bool here = index < node->keys.size() && !(value < node->keys[index]);

        if (here) {
            if (node->leaf) {
                node->keys.erase(node->keys.begin() + static_cast<std::ptrdiff_t>(index));
            } else {
                removeFromInternal(node, index);
            }
            return;
        }
        if (node->leaf) return;   // absent; contains() rules this out beforehand

        const bool descendingIntoLast = (index == node->keys.size());
        if (node->children[index]->keys.size() < minDegree_) refill(node, index);

        // A merge at the tail shifts the target child one slot left.
        if (descendingIntoLast && index > node->keys.size()) {
            removeFrom(node->children[index - 1], value);
        } else {
            removeFrom(node->children[index], value);
        }
    }

    void removeFromInternal(Node* node, std::size_t index) {
        const T key = node->keys[index];
        Node* left = node->children[index];
        Node* right = node->children[index + 1];

        if (left->keys.size() >= minDegree_) {
            // Replace with the in-order predecessor and delete that instead.
            const T predecessor = rightmostKey(left);
            node->keys[index] = predecessor;
            removeFrom(left, predecessor);
        } else if (right->keys.size() >= minDegree_) {
            const T successor = leftmostKey(right);
            node->keys[index] = successor;
            removeFrom(right, successor);
        } else {
            // Both siblings are minimal: fuse them around the key, then recurse.
            mergeChildren(node, index);
            removeFrom(node->children[index], key);
        }
    }

    static T rightmostKey(Node* node) {
        while (!node->leaf) node = node->children.back();
        return node->keys.back();
    }

    static T leftmostKey(Node* node) {
        while (!node->leaf) node = node->children.front();
        return node->keys.front();
    }

    /// Guarantees children[index] has at least `minDegree_` keys.
    void refill(Node* node, std::size_t index) {
        if (index > 0 && node->children[index - 1]->keys.size() >= minDegree_) {
            borrowFromLeft(node, index);
        } else if (index < node->keys.size() &&
                   node->children[index + 1]->keys.size() >= minDegree_) {
            borrowFromRight(node, index);
        } else if (index < node->keys.size()) {
            mergeChildren(node, index);
        } else {
            mergeChildren(node, index - 1);
        }
    }

    /// Rotate right: parent key moves down, left sibling's last key moves up.
    static void borrowFromLeft(Node* node, std::size_t index) {
        Node* child = node->children[index];
        Node* sibling = node->children[index - 1];

        child->keys.insert(child->keys.begin(), node->keys[index - 1]);
        node->keys[index - 1] = sibling->keys.back();
        sibling->keys.pop_back();

        if (!child->leaf) {
            child->children.insert(child->children.begin(), sibling->children.back());
            sibling->children.pop_back();
        }
    }

    /// Rotate left: parent key moves down, right sibling's first key moves up.
    static void borrowFromRight(Node* node, std::size_t index) {
        Node* child = node->children[index];
        Node* sibling = node->children[index + 1];

        child->keys.push_back(node->keys[index]);
        node->keys[index] = sibling->keys.front();
        sibling->keys.erase(sibling->keys.begin());

        if (!child->leaf) {
            child->children.push_back(sibling->children.front());
            sibling->children.erase(sibling->children.begin());
        }
    }

    /// Fuses children[index], keys[index] and children[index+1] into one node.
    static void mergeChildren(Node* node, std::size_t index) {
        Node* left = node->children[index];
        Node* right = node->children[index + 1];

        left->keys.push_back(node->keys[index]);
        left->keys.insert(left->keys.end(), right->keys.begin(), right->keys.end());
        if (!left->leaf) {
            left->children.insert(left->children.end(), right->children.begin(),
                                  right->children.end());
        }

        node->keys.erase(node->keys.begin() + static_cast<std::ptrdiff_t>(index));
        node->children.erase(node->children.begin() + static_cast<std::ptrdiff_t>(index) + 1);

        right->children.clear();   // children were adopted by `left`
        delete right;
    }

    Node* root_{nullptr};
    std::size_t minDegree_{3};
    size_type size_{0};
};

}   // namespace daedalus

#endif   // DAEDALUS_TREES_B_TREE_HPP
