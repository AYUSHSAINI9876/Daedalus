// ============================================================================
//  Daedalus :: core/Comparator.hpp
//
//  Ordering as a first-class object (Strategy pattern). Templates in this
//  library accept a plain functor for zero-overhead use, but the runtime
//  hierarchy below lets ordering be chosen from a config file or a CLI flag,
//  which a template parameter cannot do.
// ============================================================================
#ifndef DAEDALUS_CORE_COMPARATOR_HPP
#define DAEDALUS_CORE_COMPARATOR_HPP

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "daedalus/core/Concepts.hpp"

namespace daedalus {

/// Abstract three-way ordering strategy.
template <typename T>
class Comparator {
public:
    virtual ~Comparator() = default;

    /// Negative if a orders before b, zero if equivalent, positive otherwise.
    [[nodiscard]] virtual int compare(const T& a, const T& b) const = 0;

    [[nodiscard]] virtual std::string name() const = 0;

    /// Lets any Comparator be passed straight to std::sort and friends.
    [[nodiscard]] bool operator()(const T& a, const T& b) const { return compare(a, b) < 0; }
};

template <typename T>
    requires LessThanComparable<T>
class Ascending final : public Comparator<T> {
public:
    [[nodiscard]] int compare(const T& a, const T& b) const override {
        if (a < b) return -1;
        if (b < a) return 1;
        return 0;
    }
    [[nodiscard]] std::string name() const override { return "Ascending"; }
};

template <typename T>
    requires LessThanComparable<T>
class Descending final : public Comparator<T> {
public:
    [[nodiscard]] int compare(const T& a, const T& b) const override {
        if (b < a) return -1;
        if (a < b) return 1;
        return 0;
    }
    [[nodiscard]] std::string name() const override { return "Descending"; }
};

/// Adapts an arbitrary "less" predicate into the hierarchy.
template <typename T>
class PredicateComparator final : public Comparator<T> {
public:
    explicit PredicateComparator(std::function<bool(const T&, const T&)> less,
                                 std::string label = "Predicate")
        : less_(std::move(less)), label_(std::move(label)) {}

    [[nodiscard]] int compare(const T& a, const T& b) const override {
        if (less_(a, b)) return -1;
        if (less_(b, a)) return 1;
        return 0;
    }
    [[nodiscard]] std::string name() const override { return label_; }

private:
    std::function<bool(const T&, const T&)> less_;
    std::string label_;
};

/// Reverses any comparator without copying it (Decorator pattern).
template <typename T>
class ReverseComparator final : public Comparator<T> {
public:
    explicit ReverseComparator(std::shared_ptr<const Comparator<T>> inner)
        : inner_(std::move(inner)) {}

    [[nodiscard]] int compare(const T& a, const T& b) const override {
        return -inner_->compare(a, b);
    }
    [[nodiscard]] std::string name() const override { return "Reverse(" + inner_->name() + ")"; }

private:
    std::shared_ptr<const Comparator<T>> inner_;
};

template <typename T>
[[nodiscard]] std::shared_ptr<const Comparator<T>> ascending() {
    return std::make_shared<const Ascending<T>>();
}

template <typename T>
[[nodiscard]] std::shared_ptr<const Comparator<T>> descending() {
    return std::make_shared<const Descending<T>>();
}

}   // namespace daedalus

#endif   // DAEDALUS_CORE_COMPARATOR_HPP
