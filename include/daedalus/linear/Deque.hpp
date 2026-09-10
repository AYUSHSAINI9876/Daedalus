// ============================================================================
//  Daedalus :: linear/Deque.hpp
//
//  Double-ended queue over a growable ring buffer. Both ends are O(1) because
//  the head index moves backwards through the buffer modulo its capacity
//  instead of shifting elements.
//
//  Complexity: pushFront/pushBack/popFront/popBack amortised O(1)
//              operator[] O(1) | space O(n)
// ============================================================================
#ifndef DAEDALUS_LINEAR_DEQUE_HPP
#define DAEDALUS_LINEAR_DEQUE_HPP

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"

namespace daedalus {

template <typename T>
class Deque final : public Sequence<T> {
public:
    using value_type = T;
    using size_type = std::size_t;

    static constexpr size_type kInitialCapacity = 8;

    Deque() = default;

    Deque(std::initializer_list<T> values) {
        reserve(values.size());
        for (const T& value : values) pushBack(value);
    }

    Deque(const Deque& other) {
        reserve(other.size_);
        for (size_type i = 0; i < other.size_; ++i) pushBack(other[i]);
    }

    Deque(Deque&& other) noexcept
        : buffer_(other.buffer_),
          capacity_(other.capacity_),
          head_(other.head_),
          size_(other.size_) {
        other.buffer_ = nullptr;
        other.capacity_ = other.head_ = other.size_ = 0;
    }

    Deque& operator=(Deque other) noexcept {
        swap(other);
        return *this;
    }

    ~Deque() override {
        clear();
        if (buffer_ != nullptr) ::operator delete(buffer_);
    }

    void swap(Deque& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
        std::swap(head_, other.head_);
        std::swap(size_, other.size_);
    }

    // --- observers -----------------------------------------------------------

    [[nodiscard]] size_type size() const noexcept override { return size_; }
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept override { return size_ == 0; }
    [[nodiscard]] std::string name() const override { return "Deque"; }

    void clear() override {
        for (size_type i = 0; i < size_; ++i) std::destroy_at(slot(i));
        size_ = 0;
        head_ = 0;
    }

    void reserve(size_type minimumCapacity) {
        if (minimumCapacity <= capacity_) return;
        reallocate(minimumCapacity);
    }

    // --- modifiers -----------------------------------------------------------

    void pushBack(const T& value) {
        growIfFull();
        std::construct_at(slot(size_), value);
        ++size_;
    }

    void pushFront(const T& value) {
        growIfFull();
        head_ = (head_ + capacity_ - 1) % capacity_;
        std::construct_at(buffer_ + head_, value);
        ++size_;
    }

    T popBack() {
        if (empty()) throw EmptyContainer("popBack");
        T* last = slot(size_ - 1);
        T value = std::move(*last);
        std::destroy_at(last);
        --size_;
        return value;
    }

    T popFront() {
        if (empty()) throw EmptyContainer("popFront");
        T* first = buffer_ + head_;
        T value = std::move(*first);
        std::destroy_at(first);
        head_ = (head_ + 1) % capacity_;
        --size_;
        return value;
    }

    void insertAt(size_type index, const T& value) override {
        if (index > size_) throw IndexOutOfRange(index, size_);
        if (index == 0) return pushFront(value);
        if (index == size_) return pushBack(value);
        // Copy the tail out first: pushBack may reallocate and invalidate back().
        T lastCopy = back();
        pushBack(lastCopy);
        for (size_type i = size_ - 2; i > index; --i) (*this)[i] = std::move((*this)[i - 1]);
        (*this)[index] = value;
    }

    T eraseAt(size_type index) override {
        if (index >= size_) throw IndexOutOfRange(index, size_);
        T removed = std::move((*this)[index]);
        for (size_type i = index; i + 1 < size_; ++i) (*this)[i] = std::move((*this)[i + 1]);
        popBack();
        return removed;
    }

    // --- element access ------------------------------------------------------

    [[nodiscard]] T& operator[](size_type index) noexcept { return *slot(index); }
    [[nodiscard]] const T& operator[](size_type index) const noexcept { return *slot(index); }

    [[nodiscard]] T& at(size_type index) override {
        if (index >= size_) throw IndexOutOfRange(index, size_);
        return *slot(index);
    }

    [[nodiscard]] const T& at(size_type index) const override {
        if (index >= size_) throw IndexOutOfRange(index, size_);
        return *slot(index);
    }

    [[nodiscard]] T& front() {
        if (empty()) throw EmptyContainer("front");
        return *slot(0);
    }

    [[nodiscard]] const T& front() const {
        if (empty()) throw EmptyContainer("front");
        return *slot(0);
    }

    [[nodiscard]] T& back() {
        if (empty()) throw EmptyContainer("back");
        return *slot(size_ - 1);
    }

    [[nodiscard]] const T& back() const {
        if (empty()) throw EmptyContainer("back");
        return *slot(size_ - 1);
    }

    // --- Collection interface ------------------------------------------------

    void insert(const T& value) override { pushBack(value); }

    /// Value-based search is part of the Collection interface, but a Deque is
    /// also the right structure for element types that have no equality at all
    /// -- the thread pool queues std::function, which is not comparable. Rather
    /// than making the whole container un-instantiable for those types, the
    /// search is compiled only when it is meaningful and reports "absent"
    /// otherwise.
    bool erase(const T& value) override {
        if constexpr (EqualityComparable<T>) {
            for (size_type i = 0; i < size_; ++i) {
                if ((*this)[i] == value) {
                    eraseAt(i);
                    return true;
                }
            }
        } else {
            (void)value;
        }
        return false;
    }

    [[nodiscard]] bool contains(const T& value) const override {
        if constexpr (EqualityComparable<T>) {
            for (size_type i = 0; i < size_; ++i) {
                if ((*this)[i] == value) return true;
            }
        } else {
            (void)value;
        }
        return false;
    }

    [[nodiscard]] std::vector<T> toVector() const override {
        std::vector<T> out;
        out.reserve(size_);
        for (size_type i = 0; i < size_; ++i) out.push_back((*this)[i]);
        return out;
    }

private:
    [[nodiscard]] T* slot(size_type logicalIndex) const noexcept {
        return buffer_ + ((head_ + logicalIndex) % capacity_);
    }

    void growIfFull() {
        if (size_ == capacity_) reallocate(capacity_ == 0 ? kInitialCapacity : capacity_ * 2);
    }

    /// Reallocating also re-normalises the ring so head_ returns to zero.
    void reallocate(size_type newCapacity) {
        T* fresh = static_cast<T*>(::operator new(newCapacity * sizeof(T)));
        size_type constructed = 0;
        try {
            for (; constructed < size_; ++constructed) {
                std::construct_at(fresh + constructed, std::move_if_noexcept(*slot(constructed)));
            }
        } catch (...) {
            for (size_type i = 0; i < constructed; ++i) std::destroy_at(fresh + i);
            ::operator delete(fresh);
            throw;
        }
        for (size_type i = 0; i < size_; ++i) std::destroy_at(slot(i));
        if (buffer_ != nullptr) ::operator delete(buffer_);
        buffer_ = fresh;
        capacity_ = newCapacity;
        head_ = 0;
    }

    T* buffer_{nullptr};
    size_type capacity_{0};
    size_type head_{0};
    size_type size_{0};
};

}  // namespace daedalus

#endif  // DAEDALUS_LINEAR_DEQUE_HPP
