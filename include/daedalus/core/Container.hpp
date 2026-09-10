// ============================================================================
//  Daedalus :: core/Container.hpp
//
//  The abstract interfaces every concrete structure implements. These are what
//  make the library polymorphic rather than a bag of unrelated templates: a
//  caller can hold a std::unique_ptr<Collection<int>> and swap an AVL tree for
//  a hash set without touching the calling code.
//
//      Container<T>            size / empty / clear / name / toString
//        |- Collection<T>      insert / erase / contains / toVector
//        |    |- Sequence<T>   at / insertAt / eraseAt   (index-addressable)
//        |    |- SortedSet<T>  minimum / maximum / height (ordered structures)
//        |- Map<K,V>           put / get / erase / keys
// ============================================================================
#ifndef DAEDALUS_CORE_CONTAINER_HPP
#define DAEDALUS_CORE_CONTAINER_HPP

#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/core/Exception.hpp"

namespace daedalus {

/// Formats a single element for toString(). Falls back to a placeholder for
/// element types that are not streamable, so toString() compiles for any T.
template <typename T>
[[nodiscard]] std::string formatElement(const T& value) {
    if constexpr (Streamable<T>) {
        std::ostringstream os;
        os << value;
        return os.str();
    } else {
        return "<?>";
    }
}

/// Renders any range as "[a, b, c]".
template <typename Range>
[[nodiscard]] std::string joinElements(const Range& range) {
    std::ostringstream os;
    os << "[";
    bool first = true;
    for (const auto& element : range) {
        if (!first) os << ", ";
        os << formatElement(element);
        first = false;
    }
    os << "]";
    return os.str();
}

// ---------------------------------------------------------------------------

/// Root interface: anything that holds a countable number of elements.
template <typename T>
class Container {
public:
    using value_type = T;
    using size_type = std::size_t;

    Container() = default;
    Container(const Container&) = default;
    Container(Container&&) noexcept = default;
    Container& operator=(const Container&) = default;
    Container& operator=(Container&&) noexcept = default;
    virtual ~Container() = default;

    /// Number of elements currently stored.
    [[nodiscard]] virtual size_type size() const noexcept = 0;

    /// Removes every element, leaving the container valid and empty.
    virtual void clear() = 0;

    /// Structure name, e.g. "AVLTree". Used by the CLI and by diagnostics.
    [[nodiscard]] virtual std::string name() const = 0;

    [[nodiscard]] virtual bool empty() const noexcept { return size() == 0; }

    /// Debug rendering. The default prints the structure name and size; every
    /// concrete container overrides it with its own element listing.
    [[nodiscard]] virtual std::string toString() const {
        return name() + "(size=" + std::to_string(size()) + ")";
    }
};

/// A container whose elements can be inserted, erased and searched by value.
template <typename T>
class Collection : public Container<T> {
public:
    /// Adds value. Set-like structures may ignore duplicates.
    virtual void insert(const T& value) = 0;

    /// Removes one occurrence of value; returns false when absent.
    virtual bool erase(const T& value) = 0;

    [[nodiscard]] virtual bool contains(const T& value) const = 0;

    /// Snapshot of the contents in the structure's natural order.
    [[nodiscard]] virtual std::vector<T> toVector() const = 0;

    [[nodiscard]] std::string toString() const override {
        return this->name() + " " + joinElements(toVector());
    }
};

/// A collection addressable by position, i.e. a list.
template <typename T>
class Sequence : public Collection<T> {
public:
    /// Bounds-checked element access; throws IndexOutOfRange.
    [[nodiscard]] virtual T& at(std::size_t index) = 0;
    [[nodiscard]] virtual const T& at(std::size_t index) const = 0;

    /// Inserts before index; index == size() appends.
    virtual void insertAt(std::size_t index, const T& value) = 0;

    /// Removes and returns the element at index.
    virtual T eraseAt(std::size_t index) = 0;
};

/// A collection that keeps its elements ordered and can answer order queries.
template <typename T>
class SortedSet : public Collection<T> {
public:
    /// Smallest / largest stored key; nullopt when empty.
    [[nodiscard]] virtual std::optional<T> minimum() const = 0;
    [[nodiscard]] virtual std::optional<T> maximum() const = 0;

    /// Height in edges; -1 for an empty structure. Exposed because the whole
    /// point of a balanced tree is that this stays O(log n).
    [[nodiscard]] virtual int height() const = 0;
};

/// Key/value association.
template <typename K, typename V>
class Map : public Container<K> {
public:
    /// Inserts or overwrites the value bound to key.
    virtual void put(const K& key, const V& value) = 0;

    /// Value bound to key, or nullopt.
    [[nodiscard]] virtual std::optional<V> get(const K& key) const = 0;

    virtual bool erase(const K& key) = 0;

    [[nodiscard]] virtual bool contains(const K& key) const = 0;

    [[nodiscard]] virtual std::vector<K> keys() const = 0;

    [[nodiscard]] virtual std::vector<std::pair<K, V>> entries() const = 0;

    [[nodiscard]] std::string toString() const override {
        std::ostringstream os;
        os << this->name() << " {";
        bool first = true;
        for (const auto& entry : entries()) {
            if (!first) os << ", ";
            os << formatElement(entry.first) << ": " << formatElement(entry.second);
            first = false;
        }
        os << "}";
        return os.str();
    }
};

}   // namespace daedalus

#endif   // DAEDALUS_CORE_CONTAINER_HPP
