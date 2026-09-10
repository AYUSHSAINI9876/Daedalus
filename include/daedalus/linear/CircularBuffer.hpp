// ============================================================================
//  Daedalus :: linear/CircularBuffer.hpp
//
//  Fixed-capacity ring buffer -- the structure behind audio pipelines, log
//  tails and rate limiters. Capacity never changes after construction, so the
//  whole thing is one allocation and every operation is O(1).
//
//  The overflow policy is a constructor argument rather than a second class:
//    Overwrite  drop the oldest element to make room (a sliding window)
//    Reject     throw CapacityExceeded (a bounded work queue)
//
//  Complexity: push/pop/front/back O(1) | space O(capacity)
// ============================================================================
#ifndef DAEDALUS_LINEAR_CIRCULAR_BUFFER_HPP
#define DAEDALUS_LINEAR_CIRCULAR_BUFFER_HPP

#include <cstddef>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"

namespace daedalus {

enum class OverflowPolicy { Overwrite, Reject };

template <typename T>
class CircularBuffer final : public Collection<T> {
public:
    using value_type = T;
    using size_type = std::size_t;

    explicit CircularBuffer(size_type capacity, OverflowPolicy policy = OverflowPolicy::Overwrite)
        : capacity_(capacity), policy_(policy) {
        require(capacity > 0, "CircularBuffer capacity must be greater than zero");
        buffer_ = static_cast<T*>(::operator new(capacity * sizeof(T)));
    }

    CircularBuffer(const CircularBuffer& other)
        : capacity_(other.capacity_), policy_(other.policy_) {
        buffer_ = static_cast<T*>(::operator new(capacity_ * sizeof(T)));
        for (size_type i = 0; i < other.size_; ++i) push(other[i]);
    }

    CircularBuffer(CircularBuffer&& other) noexcept
        : buffer_(other.buffer_),
          capacity_(other.capacity_),
          head_(other.head_),
          size_(other.size_),
          policy_(other.policy_) {
        other.buffer_ = nullptr;
        other.capacity_ = other.head_ = other.size_ = 0;
    }

    CircularBuffer& operator=(CircularBuffer other) noexcept {
        swap(other);
        return *this;
    }

    ~CircularBuffer() override {
        clear();
        if (buffer_ != nullptr) ::operator delete(buffer_);
    }

    void swap(CircularBuffer& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
        std::swap(head_, other.head_);
        std::swap(size_, other.size_);
        std::swap(policy_, other.policy_);
    }

    // --- observers -----------------------------------------------------------

    [[nodiscard]] size_type size() const noexcept override { return size_; }
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept override { return size_ == 0; }
    [[nodiscard]] bool full() const noexcept { return size_ == capacity_; }
    [[nodiscard]] OverflowPolicy policy() const noexcept { return policy_; }
    [[nodiscard]] std::string name() const override { return "CircularBuffer"; }

    void clear() override {
        for (size_type i = 0; i < size_; ++i) std::destroy_at(slot(i));
        size_ = 0;
        head_ = 0;
    }

    // --- modifiers -----------------------------------------------------------

    /// Appends `value`. Returns true when an element was evicted to make room.
    bool push(const T& value) {
        bool evicted = false;
        if (full()) {
            if (policy_ == OverflowPolicy::Reject) throw CapacityExceeded(capacity_);
            std::destroy_at(buffer_ + head_);
            head_ = (head_ + 1) % capacity_;
            --size_;
            evicted = true;
        }
        std::construct_at(slot(size_), value);
        ++size_;
        return evicted;
    }

    T pop() {
        if (empty()) throw EmptyContainer("pop");
        T* first = buffer_ + head_;
        T value = std::move(*first);
        std::destroy_at(first);
        head_ = (head_ + 1) % capacity_;
        --size_;
        return value;
    }

    // --- element access ------------------------------------------------------

    [[nodiscard]] T& operator[](size_type index) noexcept { return *slot(index); }
    [[nodiscard]] const T& operator[](size_type index) const noexcept { return *slot(index); }

    [[nodiscard]] T& at(size_type index) {
        if (index >= size_) throw IndexOutOfRange(index, size_);
        return *slot(index);
    }

    [[nodiscard]] T& front() {
        if (empty()) throw EmptyContainer("front");
        return *slot(0);
    }

    [[nodiscard]] T& back() {
        if (empty()) throw EmptyContainer("back");
        return *slot(size_ - 1);
    }

    // --- Collection interface ------------------------------------------------

    void insert(const T& value) override { push(value); }

    bool erase(const T& value) override {
        for (size_type i = 0; i < size_; ++i) {
            if (!((*this)[i] == value)) continue;
            for (size_type j = i; j + 1 < size_; ++j) (*this)[j] = std::move((*this)[j + 1]);
            std::destroy_at(slot(size_ - 1));
            --size_;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool contains(const T& value) const override {
        for (size_type i = 0; i < size_; ++i) {
            if ((*this)[i] == value) return true;
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

    T* buffer_{nullptr};
    size_type capacity_{0};
    size_type head_{0};
    size_type size_{0};
    OverflowPolicy policy_{OverflowPolicy::Overwrite};
};

}   // namespace daedalus

#endif   // DAEDALUS_LINEAR_CIRCULAR_BUFFER_HPP
