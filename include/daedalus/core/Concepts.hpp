// ============================================================================
//  Daedalus :: core/Concepts.hpp
//
//  C++20 concepts used to constrain the templates. They exist so that misuse
//  produces a one-line diagnostic at the call site instead of a page of
//  instantiation noise from deep inside a container.
// ============================================================================
#ifndef DAEDALUS_CORE_CONCEPTS_HPP
#define DAEDALUS_CORE_CONCEPTS_HPP

#include <concepts>
#include <cstddef>
#include <functional>
#include <iosfwd>
#include <string>
#include <type_traits>

namespace daedalus {

/// Supports `a < b` yielding something convertible to bool.
template <typename T>
concept LessThanComparable = requires(const T& a, const T& b) {
    { a < b } -> std::convertible_to<bool>;
};

/// Supports `a == b`.
template <typename T>
concept EqualityComparable = requires(const T& a, const T& b) {
    { a == b } -> std::convertible_to<bool>;
};

/// Usable as an ordered key (BST, AVL, heap, ...).
template <typename T>
concept Comparable = LessThanComparable<T> && EqualityComparable<T>;

/// Usable as a hash-table key.
template <typename T>
concept Hashable = EqualityComparable<T> && requires(const T& value) {
    { std::hash<T>{}(value) } -> std::convertible_to<std::size_t>;
};

/// Streamable to std::ostream, which is what `toString()` relies on.
template <typename T>
concept Streamable = requires(std::ostream& os, const T& value) {
    { os << value } -> std::same_as<std::ostream&>;
};

/// Numeric edge weight: arithmetic, ordered, and closed under addition.
template <typename T>
concept Weight = std::is_arithmetic_v<T> && LessThanComparable<T>;

/// A binary predicate imposing a strict weak ordering on T.
template <typename F, typename T>
concept StrictWeakOrder = std::predicate<F, const T&, const T&>;

/// A unary callable invoked with each element, used by traversal visitors.
template <typename F, typename T>
concept ElementVisitor = std::invocable<F, const T&>;

}  // namespace daedalus

#endif  // DAEDALUS_CORE_CONCEPTS_HPP
