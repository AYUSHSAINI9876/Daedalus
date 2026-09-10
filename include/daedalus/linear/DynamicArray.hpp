// ============================================================================
//  Daedalus :: linear/DynamicArray.hpp
//
//  A growable contiguous array built on raw storage, i.e. std::vector written
//  out by hand. It exists to demonstrate the parts a std::vector hides:
//  manual allocation, placement new, the rule of five, copy-and-swap
//  assignment, and geometric growth giving amortised O(1) append.
//
//  Complexity: operator[] O(1) | pushBack amortised O(1) | insertAt O(n)
//              eraseAt O(n)    | search O(n)             | space O(n)
// ============================================================================
#ifndef DAEDALUS_LINEAR_DYNAMIC_ARRAY_HPP
#define DAEDALUS_LINEAR_DYNAMIC_ARRAY_HPP

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"

namespace daedalus {

template <typename T>
class DynamicArray final : public Sequence<T> {
public:
    using value_type = T;
    using size_type = std::size_t;
    using iterator = T*;
    using const_iterator = const T*;

    static constexpr size_type kInitialCapacity = 4;

    // --- construction --------------------------------------------------------

    DynamicArray() noexcept = default;

    explicit DynamicArray(size_type count, const T& value = T()) {
        reserve(count);
        for (size_type i = 0; i < count; ++i) {
            std::construct_at(data_ + i, value);
            ++size_;
        }
    }

    DynamicArray(std::initializer_list<T> values) {
        reserve(values.size());
        for (const T& value : values) pushBack(value);
    }

    template <typename InputIt,
              typename = std::enable_if_t<!std::is_integral_v<InputIt>>>
    DynamicArray(InputIt first, InputIt last) {
        for (; first != last; ++first) pushBack(*first);
    }

    DynamicArray(const DynamicArray& other) {
        reserve(other.size_);
        for (size_type i = 0; i < other.size_; ++i) {
            std::construct_at(data_ + i, other.data_[i]);
            ++size_;
        }
    }

    DynamicArray(DynamicArray&& other) noexcept
        : data_(other.data_), size_(other.size_), capacity_(other.capacity_) {
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }

    /// Copy-and-swap: strong exception guarantee, one implementation for both
    /// assignment forms.
    DynamicArray& operator=(DynamicArray other) noexcept {
        swap(other);
        return *this;
    }

    ~DynamicArray() override {
        destroyRange(0, size_);
        deallocate(data_);
    }

    void swap(DynamicArray& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
        std::swap(capacity_, other.capacity_);
    }

    // --- capacity ------------------------------------------------------------

    [[nodiscard]] size_type size() const noexcept override { return size_; }
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
    [[nodiscard]] bool empty() const noexcept override { return size_ == 0; }
    [[nodiscard]] std::string name() const override { return "DynamicArray"; }

    void reserve(size_type minimumCapacity) {
        if (minimumCapacity <= capacity_) return;
        reallocate(minimumCapacity);
    }

    /// Releases unused capacity. No-op when already exact.
    void shrinkToFit() {
        if (size_ == capacity_) return;
        reallocate(size_);
    }

    void resize(size_type count, const T& value = T()) {
        if (count < size_) {
            destroyRange(count, size_);
            size_ = count;
            return;
        }
        reserve(count);
        while (size_ < count) {
            std::construct_at(data_ + size_, value);
            ++size_;
        }
    }

    void clear() override {
        destroyRange(0, size_);
        size_ = 0;
    }

    // --- element access ------------------------------------------------------

    [[nodiscard]] T& operator[](size_type index) noexcept { return data_[index]; }
    [[nodiscard]] const T& operator[](size_type index) const noexcept { return data_[index]; }

    [[nodiscard]] T& at(size_type index) override {
        if (index >= size_) throw IndexOutOfRange(index, size_);
        return data_[index];
    }

    [[nodiscard]] const T& at(size_type index) const override {
        if (index >= size_) throw IndexOutOfRange(index, size_);
        return data_[index];
    }

    [[nodiscard]] T& front() {
        if (empty()) throw EmptyContainer("front");
        return data_[0];
    }

    [[nodiscard]] const T& front() const {
        if (empty()) throw EmptyContainer("front");
        return data_[0];
    }

    [[nodiscard]] T& back() {
        if (empty()) throw EmptyContainer("back");
        return data_[size_ - 1];
    }

    [[nodiscard]] const T& back() const {
        if (empty()) throw EmptyContainer("back");
        return data_[size_ - 1];
    }

    [[nodiscard]] T* data() noexcept { return data_; }
    [[nodiscard]] const T* data() const noexcept { return data_; }

    // --- modifiers -----------------------------------------------------------

    void pushBack(const T& value) {
        growIfFull();
        std::construct_at(data_ + size_, value);
        ++size_;
    }

    void pushBack(T&& value) {
        growIfFull();
        std::construct_at(data_ + size_, std::move(value));
        ++size_;
    }

    template <typename... Args>
    T& emplaceBack(Args&&... args) {
        growIfFull();
        std::construct_at(data_ + size_, std::forward<Args>(args)...);
        return data_[size_++];
    }

    /// Removes and returns the last element.
    T popBack() {
        if (empty()) throw EmptyContainer("popBack");
        T value = std::move(data_[size_ - 1]);
        std::destroy_at(data_ + size_ - 1);
        --size_;
        return value;
    }

    void insertAt(size_type index, const T& value) override {
        if (index > size_) throw IndexOutOfRange(index, size_);
        growIfFull();
        // Shift right by one, constructing the new tail slot first.
        //
        // std::move_backward rather than a hand-rolled descending loop: with a
        // raw `for (i = size_ - 1; i > index; --i)` the optimiser cannot bound
        // the trip count and warns at -O3 that data_[i - 1] might underflow.
        // The algorithm states the range explicitly, so there is nothing to
        // infer.
        if (index < size_) {
            std::construct_at(data_ + size_, std::move(data_[size_ - 1]));
            std::move_backward(data_ + index, data_ + size_ - 1, data_ + size_);
            data_[index] = value;
        } else {
            std::construct_at(data_ + size_, value);
        }
        ++size_;
    }

    T eraseAt(size_type index) override {
        if (index >= size_) throw IndexOutOfRange(index, size_);
        T removed = std::move(data_[index]);
        std::move(data_ + index + 1, data_ + size_, data_ + index);
        std::destroy_at(data_ + size_ - 1);
        --size_;
        return removed;
    }

    // --- Collection interface ------------------------------------------------

    void insert(const T& value) override { pushBack(value); }

    bool erase(const T& value) override {
        const auto found = indexOf(value);
        if (found == kNotFound) return false;
        eraseAt(found);
        return true;
    }

    [[nodiscard]] bool contains(const T& value) const override {
        return indexOf(value) != kNotFound;
    }

    [[nodiscard]] std::vector<T> toVector() const override {
        return std::vector<T>(begin(), end());
    }

    static constexpr size_type kNotFound = static_cast<size_type>(-1);

    /// Index of the first element equal to `value`, or kNotFound.
    [[nodiscard]] size_type indexOf(const T& value) const {
        for (size_type i = 0; i < size_; ++i) {
            if (data_[i] == value) return i;
        }
        return kNotFound;
    }

    /// Reverses the array in place.
    void reverse() noexcept {
        for (size_type i = 0, j = size_ == 0 ? 0 : size_ - 1; i < j; ++i, --j) {
            std::swap(data_[i], data_[j]);
        }
    }

    // --- iteration -----------------------------------------------------------

    [[nodiscard]] iterator begin() noexcept { return data_; }
    [[nodiscard]] iterator end() noexcept { return data_ + size_; }
    [[nodiscard]] const_iterator begin() const noexcept { return data_; }
    [[nodiscard]] const_iterator end() const noexcept { return data_ + size_; }
    [[nodiscard]] const_iterator cbegin() const noexcept { return data_; }
    [[nodiscard]] const_iterator cend() const noexcept { return data_ + size_; }

    [[nodiscard]] bool operator==(const DynamicArray& other) const {
        if (size_ != other.size_) return false;
        for (size_type i = 0; i < size_; ++i) {
            if (!(data_[i] == other.data_[i])) return false;
        }
        return true;
    }

private:
    static T* allocate(size_type count) {
        if (count == 0) return nullptr;
        return static_cast<T*>(::operator new(count * sizeof(T)));
    }

    static void deallocate(T* pointer) noexcept {
        if (pointer != nullptr) ::operator delete(pointer);
    }

    void destroyRange(size_type first, size_type last) noexcept {
        for (size_type i = first; i < last; ++i) std::destroy_at(data_ + i);
    }

    void growIfFull() {
        if (size_ == capacity_) {
            reallocate(capacity_ == 0 ? kInitialCapacity : capacity_ * 2);
        }
    }

    /// Moves the live elements into fresh storage of `newCapacity` slots.
    /// move_if_noexcept keeps the strong guarantee for copyable-but-throwing
    /// element types.
    void reallocate(size_type newCapacity) {
        T* fresh = allocate(newCapacity);
        size_type constructed = 0;
        try {
            for (; constructed < size_; ++constructed) {
                std::construct_at(fresh + constructed, std::move_if_noexcept(data_[constructed]));
            }
        } catch (...) {
            for (size_type i = 0; i < constructed; ++i) std::destroy_at(fresh + i);
            deallocate(fresh);
            throw;
        }
        destroyRange(0, size_);
        deallocate(data_);
        data_ = fresh;
        capacity_ = newCapacity;
    }

    T* data_{nullptr};
    size_type size_{0};
    size_type capacity_{0};
};

template <typename T>
void swap(DynamicArray<T>& lhs, DynamicArray<T>& rhs) noexcept {
    lhs.swap(rhs);
}

}  // namespace daedalus

#endif  // DAEDALUS_LINEAR_DYNAMIC_ARRAY_HPP
