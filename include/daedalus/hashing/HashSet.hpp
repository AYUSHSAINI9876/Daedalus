// ============================================================================
//  Daedalus :: hashing/HashSet.hpp
//
//  Unordered set built on HashMap with a unit value type, plus the four set
//  algebra operations. Membership is O(1) expected, which is what separates it
//  from the ordered SortedSet trees -- those give you sorted iteration and
//  range queries, this gives you speed and nothing else.
//
//  Complexity: insert/erase/contains O(1) expected
//              union/intersection/difference O(n + m) expected
// ============================================================================
#ifndef DAEDALUS_HASHING_HASH_SET_HPP
#define DAEDALUS_HASHING_HASH_SET_HPP

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/core/Container.hpp"
#include "daedalus/hashing/HashMap.hpp"

namespace daedalus {

template <typename T, typename Hash = std::hash<T>>
    requires Hashable<T>
class HashSet final : public Collection<T> {
    /// Zero-cost stand-in for "no value"; only the key matters.
    struct Unit {
        bool operator==(const Unit&) const { return true; }
    };

public:
    using value_type = T;
    using size_type = std::size_t;

    HashSet() = default;

    explicit HashSet(size_type expectedElements) : map_(expectedElements) {}

    HashSet(std::initializer_list<T> values) {
        for (const T& value : values) insert(value);
    }

    explicit HashSet(const std::vector<T>& values) {
        map_.reserve(values.size());
        for (const T& value : values) insert(value);
    }

    [[nodiscard]] size_type size() const noexcept override { return map_.size(); }
    [[nodiscard]] bool empty() const noexcept override { return map_.empty(); }
    [[nodiscard]] std::string name() const override { return "HashSet"; }

    void clear() override { map_.clear(); }

    void insert(const T& value) override { map_.put(value, Unit{}); }

    bool erase(const T& value) override { return map_.erase(value); }

    [[nodiscard]] bool contains(const T& value) const override { return map_.contains(value); }

    [[nodiscard]] std::vector<T> toVector() const override { return map_.keys(); }

    void reserve(size_type expectedElements) { map_.reserve(expectedElements); }

    [[nodiscard]] HashStatistics statistics() const { return map_.statistics(); }

    // --- set algebra ---------------------------------------------------------

    /// Every element of either set.
    [[nodiscard]] HashSet unionWith(const HashSet& other) const {
        HashSet result(size() + other.size());
        for (const T& value : toVector()) result.insert(value);
        for (const T& value : other.toVector()) result.insert(value);
        return result;
    }

    /// Elements present in both. Iterates the smaller set for speed.
    [[nodiscard]] HashSet intersectionWith(const HashSet& other) const {
        const HashSet& smaller = size() <= other.size() ? *this : other;
        const HashSet& larger = size() <= other.size() ? other : *this;
        HashSet result;
        for (const T& value : smaller.toVector()) {
            if (larger.contains(value)) result.insert(value);
        }
        return result;
    }

    /// Elements of this set that are absent from `other`.
    [[nodiscard]] HashSet differenceWith(const HashSet& other) const {
        HashSet result;
        for (const T& value : toVector()) {
            if (!other.contains(value)) result.insert(value);
        }
        return result;
    }

    /// Elements in exactly one of the two sets.
    [[nodiscard]] HashSet symmetricDifferenceWith(const HashSet& other) const {
        HashSet result;
        for (const T& value : toVector()) {
            if (!other.contains(value)) result.insert(value);
        }
        for (const T& value : other.toVector()) {
            if (!contains(value)) result.insert(value);
        }
        return result;
    }

    [[nodiscard]] bool isSubsetOf(const HashSet& other) const {
        if (size() > other.size()) return false;
        for (const T& value : toVector()) {
            if (!other.contains(value)) return false;
        }
        return true;
    }

    [[nodiscard]] bool isDisjointFrom(const HashSet& other) const {
        const HashSet& smaller = size() <= other.size() ? *this : other;
        const HashSet& larger = size() <= other.size() ? other : *this;
        for (const T& value : smaller.toVector()) {
            if (larger.contains(value)) return false;
        }
        return true;
    }

    [[nodiscard]] bool operator==(const HashSet& other) const {
        return size() == other.size() && isSubsetOf(other);
    }

private:
    HashMap<T, Unit, Hash> map_;
};

}   // namespace daedalus

#endif   // DAEDALUS_HASHING_HASH_SET_HPP
