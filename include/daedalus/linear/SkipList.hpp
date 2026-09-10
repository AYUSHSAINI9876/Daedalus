// ============================================================================
//  Daedalus :: linear/SkipList.hpp
//
//  Probabilistic ordered set: a sorted linked list stacked with express lanes,
//  giving expected O(log n) search without any rotation logic. This is the
//  structure Redis uses for sorted sets, and it is the honest alternative to a
//  red-black tree when you would rather reason about coin flips than cases.
//
//  There is no dummy head node -- the top-level links live in a vector owned by
//  the list, so T never has to be default-constructible. linkAt() treats a null
//  "current" as the head, which keeps insert/erase free of special cases.
//
//  The RNG is seeded explicitly so a run is reproducible; tests depend on that.
//
//  Complexity: search/insert/erase expected O(log n), worst O(n)
//              space expected O(n)
// ============================================================================
#ifndef DAEDALUS_LINEAR_SKIP_LIST_HPP
#define DAEDALUS_LINEAR_SKIP_LIST_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"

namespace daedalus {

template <typename T, typename Compare = std::less<T>>
    requires LessThanComparable<T>
class SkipList final : public SortedSet<T> {
    struct Node {
        T value;
        std::vector<Node*> forward;
        Node(const T& v, std::size_t level) : value(v), forward(level, nullptr) {}
    };

public:
    using value_type = T;
    using size_type = std::size_t;

    /// 24 lanes comfortably covers ~16 million keys at p = 1/2.
    static constexpr std::size_t kMaxLevel = 24;

    explicit SkipList(std::uint32_t seed = 0x9E3779B9u, Compare compare = Compare())
        : compare_(compare), rng_(seed), head_(kMaxLevel, nullptr), seed_(seed) {}

    SkipList(std::initializer_list<T> values, std::uint32_t seed = 0x9E3779B9u)
        : SkipList(seed) {
        for (const T& value : values) insert(value);
    }

    SkipList(const SkipList& other) : SkipList(other.seed_, other.compare_) {
        for (const T& value : other.toVector()) insert(value);
    }

    SkipList(SkipList&& other) noexcept
        : compare_(other.compare_),
          rng_(other.rng_),
          head_(std::move(other.head_)),
          level_(other.level_),
          size_(other.size_),
          seed_(other.seed_) {
        other.head_.assign(kMaxLevel, nullptr);
        other.level_ = 1;
        other.size_ = 0;
    }

    SkipList& operator=(SkipList other) noexcept {
        swap(other);
        return *this;
    }

    ~SkipList() override { destroyAll(); }

    void swap(SkipList& other) noexcept {
        std::swap(compare_, other.compare_);
        std::swap(rng_, other.rng_);
        std::swap(head_, other.head_);
        std::swap(level_, other.level_);
        std::swap(size_, other.size_);
        std::swap(seed_, other.seed_);
    }

    // --- observers -----------------------------------------------------------

    [[nodiscard]] size_type size() const noexcept override { return size_; }
    [[nodiscard]] std::string name() const override { return "SkipList"; }

    /// Express lanes in use minus one, matching the "height in edges"
    /// convention used by the tree structures.
    [[nodiscard]] int height() const override { return static_cast<int>(level_) - 1; }

    void clear() override {
        destroyAll();
        head_.assign(kMaxLevel, nullptr);
        level_ = 1;
        size_ = 0;
    }

    // --- set operations ------------------------------------------------------

    /// Inserts `value` if absent. Duplicates are ignored (set semantics).
    void insert(const T& value) override {
        std::vector<Node*> update(kMaxLevel, nullptr);
        Node* current = descend(value, update);

        Node* next = linkAt(current, 0);
        if (next != nullptr && !compare_(value, next->value)) return;  // already present

        const std::size_t newLevel = randomLevel();
        if (newLevel > level_) level_ = newLevel;

        Node* fresh = new Node(value, newLevel);
        for (std::size_t i = 0; i < newLevel; ++i) {
            fresh->forward[i] = linkAt(update[i], i);
            linkAt(update[i], i) = fresh;
        }
        ++size_;
    }

    bool erase(const T& value) override {
        std::vector<Node*> update(kMaxLevel, nullptr);
        Node* current = descend(value, update);

        Node* target = linkAt(current, 0);
        if (target == nullptr || compare_(value, target->value)) return false;

        for (std::size_t i = 0; i < level_; ++i) {
            if (linkAt(update[i], i) != target) break;
            linkAt(update[i], i) = target->forward[i];
        }
        delete target;
        while (level_ > 1 && head_[level_ - 1] == nullptr) --level_;
        --size_;
        return true;
    }

    [[nodiscard]] bool contains(const T& value) const override {
        return findNode(value) != nullptr;
    }

    [[nodiscard]] std::optional<T> minimum() const override {
        if (head_[0] == nullptr) return std::nullopt;
        return head_[0]->value;
    }

    [[nodiscard]] std::optional<T> maximum() const override {
        const Node* current = nullptr;   // null means "the head"
        for (std::size_t i = level_; i-- > 0;) {
            while (linkAt(current, i) != nullptr) current = linkAt(current, i);
        }
        // The emptiness check has to come AFTER the walk rather than before it.
        // Testing head_[0] up front is equivalent, but the optimiser cannot
        // connect that test to this dereference and warns about a possible null
        // deref at -O3; checking the pointer it actually dereferences is both
        // clearer and provably safe.
        if (current == nullptr) return std::nullopt;
        return current->value;
    }

    [[nodiscard]] std::vector<T> toVector() const override {
        std::vector<T> out;
        out.reserve(size_);
        for (const Node* node = head_[0]; node != nullptr; node = node->forward[0]) {
            out.push_back(node->value);
        }
        return out;
    }

    /// Smallest stored key that is not less than `value`.
    [[nodiscard]] std::optional<T> lowerBound(const T& value) const {
        const Node* current = nullptr;
        for (std::size_t i = level_; i-- > 0;) {
            while (linkAt(current, i) != nullptr && compare_(linkAt(current, i)->value, value)) {
                current = linkAt(current, i);
            }
        }
        const Node* candidate = linkAt(current, 0);
        if (candidate == nullptr) return std::nullopt;
        return candidate->value;
    }

private:
    /// Forward link `i` of `node`, where a null node means the list head.
    [[nodiscard]] Node*& linkAt(Node* node, std::size_t i) noexcept {
        return node != nullptr ? node->forward[i] : head_[i];
    }

    [[nodiscard]] Node* const& linkAt(const Node* node, std::size_t i) const noexcept {
        return node != nullptr ? node->forward[i] : head_[i];
    }

    /// Walks down the lanes recording, per level, the last node before `value`.
    /// Returns that node at level 0 (null when the insertion point is the head).
    Node* descend(const T& value, std::vector<Node*>& update) {
        Node* current = nullptr;
        for (std::size_t i = level_; i-- > 0;) {
            while (linkAt(current, i) != nullptr && compare_(linkAt(current, i)->value, value)) {
                current = linkAt(current, i);
            }
            update[i] = current;
        }
        return current;
    }

    [[nodiscard]] const Node* findNode(const T& value) const {
        const Node* current = nullptr;
        for (std::size_t i = level_; i-- > 0;) {
            while (linkAt(current, i) != nullptr && compare_(linkAt(current, i)->value, value)) {
                current = linkAt(current, i);
            }
        }
        const Node* candidate = linkAt(current, 0);
        if (candidate == nullptr) return nullptr;
        return compare_(value, candidate->value) ? nullptr : candidate;
    }

    /// Geometric distribution with p = 1/2, capped at kMaxLevel.
    [[nodiscard]] std::size_t randomLevel() {
        std::size_t level = 1;
        while (level < kMaxLevel && (rng_() & 1u) == 0u) ++level;
        return level;
    }

    void destroyAll() noexcept {
        Node* node = head_.empty() ? nullptr : head_[0];
        while (node != nullptr) {
            Node* next = node->forward[0];
            delete node;
            node = next;
        }
    }

    Compare compare_{};
    std::mt19937 rng_;
    std::vector<Node*> head_;
    std::size_t level_{1};
    size_type size_{0};
    std::uint32_t seed_{0x9E3779B9u};
};

}  // namespace daedalus

#endif  // DAEDALUS_LINEAR_SKIP_LIST_HPP
