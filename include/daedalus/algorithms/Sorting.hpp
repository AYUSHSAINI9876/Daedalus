// ============================================================================
//  Daedalus :: algorithms/Sorting.hpp
//
//  Twelve sorting algorithms behind one interface (Strategy pattern), each
//  reporting its own stability, memory use and complexity, and each emitting
//  comparison/swap events to any attached observer.
//
//  That instrumentation is the point. "Quicksort is faster than bubble sort" is
//  a claim; comparisons=1,247 versus comparisons=499,500 on the same input is a
//  measurement, and the benchmark prints exactly that table.
//
//    algorithm       best        average     worst       space     stable
//    ------------------------------------------------------------------------
//    bubble          O(n)        O(n^2)      O(n^2)      O(1)      yes
//    insertion       O(n)        O(n^2)      O(n^2)      O(1)      yes
//    selection       O(n^2)      O(n^2)      O(n^2)      O(1)      no
//    shell           O(n log n)  O(n^1.3)    O(n^1.5)    O(1)      no
//    merge           O(n log n)  O(n log n)  O(n log n)  O(n)      yes
//    quick           O(n log n)  O(n log n)  O(n^2)      O(log n)  no
//    heap            O(n log n)  O(n log n)  O(n log n)  O(1)      no
//    intro           O(n log n)  O(n log n)  O(n log n)  O(log n)  no
//    counting        O(n + k)    O(n + k)    O(n + k)    O(k)      yes
//    radix           O(d(n + k)) same        same        O(n + k)  yes
//    bucket          O(n + k)    O(n + k)    O(n^2)      O(n)      yes
//    tim             O(n)        O(n log n)  O(n log n)  O(n)      yes
//
//  Quicksort uses median-of-three pivoting and three-way partitioning, so the
//  usual killers -- already-sorted input and many duplicates -- do not degrade
//  it. Introsort adds the real-world guarantee: it counts its own recursion
//  depth and falls back to heapsort before the worst case can materialise.
// ============================================================================
#ifndef DAEDALUS_ALGORITHMS_SORTING_HPP
#define DAEDALUS_ALGORITHMS_SORTING_HPP

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/core/Exception.hpp"
#include "daedalus/core/Observer.hpp"

namespace daedalus {

/// Abstract sorting strategy. Derives from AlgorithmSubject so any instance can
/// be instrumented at runtime without touching the algorithm's code.
template <typename T>
class SortStrategy : public AlgorithmSubject {
public:
    using Compare = std::function<bool(const T&, const T&)>;

    ~SortStrategy() override = default;

    /// Sorts `data` in ascending order under `less`.
    virtual void sort(std::vector<T>& data, const Compare& less) = 0;

    void sort(std::vector<T>& data) {
        sort(data, [](const T& a, const T& b) { return a < b; });
    }

    [[nodiscard]] virtual std::string name() const = 0;

    /// True when equal elements keep their relative order.
    [[nodiscard]] virtual bool stable() const = 0;

    /// True when the extra space is O(1) or O(log n) rather than O(n).
    [[nodiscard]] virtual bool inPlace() const = 0;

    [[nodiscard]] virtual std::string averageComplexity() const = 0;
    [[nodiscard]] virtual std::string worstComplexity() const = 0;

protected:
    /// Every comparison in every strategy goes through here, so the observer
    /// counts are comparable across algorithms.
    [[nodiscard]] bool before(const Compare& less, const T& a, const T& b) const {
        if (hasObservers()) emit(AlgorithmEvent{EventType::Comparison, 0, 0});
        return less(a, b);
    }

    void exchange(std::vector<T>& data, std::size_t i, std::size_t j) const {
        if (i == j) return;
        if (hasObservers()) emit(EventType::Swap, i, j);
        std::swap(data[i], data[j]);
    }

    void assign(std::vector<T>& data, std::size_t index, const T& value) const {
        if (hasObservers()) emit(EventType::Move, index, index);
        data[index] = value;
    }
};

// --- quadratic family --------------------------------------------------------

/// Repeatedly bubbles the largest remaining element to the end. The early-exit
/// flag is what makes the best case O(n) on already-sorted input.
template <typename T>
class BubbleSort final : public SortStrategy<T> {
public:
    using Compare = typename SortStrategy<T>::Compare;
    using SortStrategy<T>::sort;   // keep the one-argument overload visible

    void sort(std::vector<T>& data, const Compare& less) override {
        const std::size_t n = data.size();
        for (std::size_t pass = 0; pass + 1 < n; ++pass) {
            bool swapped = false;
            for (std::size_t i = 0; i + 1 < n - pass; ++i) {
                if (this->before(less, data[i + 1], data[i])) {
                    this->exchange(data, i, i + 1);
                    swapped = true;
                }
            }
            if (!swapped) break;
        }
    }

    [[nodiscard]] std::string name() const override { return "bubble"; }
    [[nodiscard]] bool stable() const override { return true; }
    [[nodiscard]] bool inPlace() const override { return true; }
    [[nodiscard]] std::string averageComplexity() const override { return "O(n^2)"; }
    [[nodiscard]] std::string worstComplexity() const override { return "O(n^2)"; }
};

/// Grows a sorted prefix by sliding each new element back into place. Genuinely
/// the fastest choice for small or nearly-sorted arrays, which is why the
/// hybrid sorts below fall back to it.
template <typename T>
class InsertionSort final : public SortStrategy<T> {
public:
    using Compare = typename SortStrategy<T>::Compare;
    using SortStrategy<T>::sort;   // keep the one-argument overload visible

    void sort(std::vector<T>& data, const Compare& less) override {
        sortRange(data, 0, data.size(), less);
    }

    /// Exposed so IntroSort and TimSort can reuse it on a sub-range.
    void sortRange(std::vector<T>& data, std::size_t first, std::size_t last,
                   const Compare& less) {
        for (std::size_t i = first + 1; i < last; ++i) {
            T key = std::move(data[i]);
            std::size_t j = i;
            while (j > first && this->before(less, key, data[j - 1])) {
                data[j] = std::move(data[j - 1]);
                if (this->hasObservers()) this->emit(EventType::Move, j, j - 1);
                --j;
            }
            data[j] = std::move(key);
        }
    }

    [[nodiscard]] std::string name() const override { return "insertion"; }
    [[nodiscard]] bool stable() const override { return true; }
    [[nodiscard]] bool inPlace() const override { return true; }
    [[nodiscard]] std::string averageComplexity() const override { return "O(n^2)"; }
    [[nodiscard]] std::string worstComplexity() const override { return "O(n^2)"; }
};

/// Always exactly n(n-1)/2 comparisons but at most n-1 swaps -- the right
/// choice when writes are far more expensive than reads (e.g. flash memory).
template <typename T>
class SelectionSort final : public SortStrategy<T> {
public:
    using Compare = typename SortStrategy<T>::Compare;
    using SortStrategy<T>::sort;   // keep the one-argument overload visible

    void sort(std::vector<T>& data, const Compare& less) override {
        const std::size_t n = data.size();
        for (std::size_t i = 0; i + 1 < n; ++i) {
            std::size_t smallest = i;
            for (std::size_t j = i + 1; j < n; ++j) {
                if (this->before(less, data[j], data[smallest])) smallest = j;
            }
            this->exchange(data, i, smallest);
        }
    }

    [[nodiscard]] std::string name() const override { return "selection"; }
    [[nodiscard]] bool stable() const override { return false; }
    [[nodiscard]] bool inPlace() const override { return true; }
    [[nodiscard]] std::string averageComplexity() const override { return "O(n^2)"; }
    [[nodiscard]] std::string worstComplexity() const override { return "O(n^2)"; }
};

/// Insertion sort over decreasing gaps, so early passes move elements a long
/// way cheaply. Uses Knuth's 3h+1 sequence.
template <typename T>
class ShellSort final : public SortStrategy<T> {
public:
    using Compare = typename SortStrategy<T>::Compare;
    using SortStrategy<T>::sort;   // keep the one-argument overload visible

    void sort(std::vector<T>& data, const Compare& less) override {
        const std::size_t n = data.size();
        std::size_t gap = 1;
        while (gap < n / 3) gap = 3 * gap + 1;

        for (; gap >= 1; gap = (gap - 1) / 3) {
            for (std::size_t i = gap; i < n; ++i) {
                T key = std::move(data[i]);
                std::size_t j = i;
                while (j >= gap && this->before(less, key, data[j - gap])) {
                    data[j] = std::move(data[j - gap]);
                    if (this->hasObservers()) this->emit(EventType::Move, j, j - gap);
                    j -= gap;
                }
                data[j] = std::move(key);
            }
            if (gap == 1) break;
        }
    }

    [[nodiscard]] std::string name() const override { return "shell"; }
    [[nodiscard]] bool stable() const override { return false; }
    [[nodiscard]] bool inPlace() const override { return true; }
    [[nodiscard]] std::string averageComplexity() const override { return "O(n^1.3)"; }
    [[nodiscard]] std::string worstComplexity() const override { return "O(n^1.5)"; }
};

// --- O(n log n) family -------------------------------------------------------

/// Classic top-down merge sort. Allocates its scratch buffer once rather than
/// per merge, which is the difference between a textbook version and a usable
/// one.
template <typename T>
class MergeSort final : public SortStrategy<T> {
public:
    using Compare = typename SortStrategy<T>::Compare;
    using SortStrategy<T>::sort;   // keep the one-argument overload visible

    void sort(std::vector<T>& data, const Compare& less) override {
        if (data.size() < 2) return;
        std::vector<T> scratch(data.size());
        split(data, scratch, 0, data.size(), less);
    }

    [[nodiscard]] std::string name() const override { return "merge"; }
    [[nodiscard]] bool stable() const override { return true; }
    [[nodiscard]] bool inPlace() const override { return false; }
    [[nodiscard]] std::string averageComplexity() const override { return "O(n log n)"; }
    [[nodiscard]] std::string worstComplexity() const override { return "O(n log n)"; }

private:
    void split(std::vector<T>& data, std::vector<T>& scratch, std::size_t first,
               std::size_t last, const Compare& less) {
        if (last - first < 2) return;
        const std::size_t middle = first + (last - first) / 2;
        if (this->hasObservers()) this->emit(EventType::Partition, first, last);
        split(data, scratch, first, middle, less);
        split(data, scratch, middle, last, less);
        merge(data, scratch, first, middle, last, less);
    }

    void merge(std::vector<T>& data, std::vector<T>& scratch, std::size_t first,
               std::size_t middle, std::size_t last, const Compare& less) {
        std::size_t left = first;
        std::size_t right = middle;
        std::size_t out = first;

        while (left < middle && right < last) {
            // Taking from the left on ties is what makes the sort stable.
            if (this->before(less, data[right], data[left])) {
                scratch[out++] = std::move(data[right++]);
            } else {
                scratch[out++] = std::move(data[left++]);
            }
        }
        while (left < middle) scratch[out++] = std::move(data[left++]);
        while (right < last) scratch[out++] = std::move(data[right++]);
        for (std::size_t i = first; i < last; ++i) {
            data[i] = std::move(scratch[i]);
            if (this->hasObservers()) this->emit(EventType::Move, i, i);
        }
    }
};

/// Quicksort with median-of-three pivoting and Dijkstra's three-way partition.
/// The three-way split means a run of equal keys is consumed in one pass
/// instead of being repeatedly re-partitioned -- the difference between O(n)
/// and O(n^2) on an array of one repeated value.
template <typename T>
class QuickSort final : public SortStrategy<T> {
public:
    using Compare = typename SortStrategy<T>::Compare;
    using SortStrategy<T>::sort;   // keep the one-argument overload visible

    void sort(std::vector<T>& data, const Compare& less) override {
        if (data.size() < 2) return;
        quicksort(data, 0, data.size() - 1, less);
    }

    [[nodiscard]] std::string name() const override { return "quick"; }
    [[nodiscard]] bool stable() const override { return false; }
    [[nodiscard]] bool inPlace() const override { return true; }
    [[nodiscard]] std::string averageComplexity() const override { return "O(n log n)"; }
    [[nodiscard]] std::string worstComplexity() const override { return "O(n^2)"; }

private:
    void quicksort(std::vector<T>& data, std::size_t low, std::size_t high,
                   const Compare& less) {
        while (low < high) {
            if (high - low < 12) {   // small ranges: insertion sort wins outright
                insertion_.sortRange(data, low, high + 1, less);
                return;
            }
            if (this->hasObservers()) this->emit(EventType::Partition, low, high);

            medianOfThree(data, low, high, less);
            const T pivot = data[low + (high - low) / 2];

            // Three-way partition: [low,lt) < pivot, [lt,gt] == pivot, (gt,high] > pivot
            std::size_t lt = low;
            std::size_t i = low;
            std::size_t gt = high;
            while (i <= gt) {
                if (this->before(less, data[i], pivot)) {
                    this->exchange(data, lt++, i++);
                } else if (this->before(less, pivot, data[i])) {
                    this->exchange(data, i, gt);
                    if (gt == 0) break;
                    --gt;
                } else {
                    ++i;
                }
            }

            // Recurse into the smaller side, loop on the larger: bounds the
            // stack at O(log n) even on an adversarial input.
            if (lt - low < high - gt) {
                if (lt > low) quicksort(data, low, lt - 1, less);
                low = gt + 1;
            } else {
                if (high > gt) quicksort(data, gt + 1, high, less);
                if (lt == low) return;
                high = lt - 1;
            }
        }
    }

    /// Sorts first/middle/last so the middle slot holds their median.
    void medianOfThree(std::vector<T>& data, std::size_t low, std::size_t high,
                       const Compare& less) {
        const std::size_t middle = low + (high - low) / 2;
        if (this->before(less, data[middle], data[low])) this->exchange(data, low, middle);
        if (this->before(less, data[high], data[low])) this->exchange(data, low, high);
        if (this->before(less, data[high], data[middle])) this->exchange(data, middle, high);
    }

    InsertionSort<T> insertion_;
};

/// Heapsort: build a max-heap in place, then repeatedly swap the root to the
/// end. The only O(n log n) sort that is genuinely O(1) in extra space.
template <typename T>
class HeapSort final : public SortStrategy<T> {
public:
    using Compare = typename SortStrategy<T>::Compare;
    using SortStrategy<T>::sort;   // keep the one-argument overload visible

    void sort(std::vector<T>& data, const Compare& less) override {
        sortRange(data, 0, data.size(), less);
    }

    /// Exposed for IntroSort's fallback path.
    void sortRange(std::vector<T>& data, std::size_t first, std::size_t last,
                   const Compare& less) {
        const std::size_t count = last - first;
        if (count < 2) return;
        for (std::size_t i = count / 2; i-- > 0;) siftDown(data, first, i, count, less);
        for (std::size_t end = count; end-- > 1;) {
            this->exchange(data, first, first + end);
            siftDown(data, first, 0, end, less);
        }
    }

    [[nodiscard]] std::string name() const override { return "heap"; }
    [[nodiscard]] bool stable() const override { return false; }
    [[nodiscard]] bool inPlace() const override { return true; }
    [[nodiscard]] std::string averageComplexity() const override { return "O(n log n)"; }
    [[nodiscard]] std::string worstComplexity() const override { return "O(n log n)"; }

private:
    void siftDown(std::vector<T>& data, std::size_t offset, std::size_t root, std::size_t count,
                  const Compare& less) {
        for (;;) {
            const std::size_t left = 2 * root + 1;
            if (left >= count) return;
            std::size_t largest = left;
            const std::size_t right = left + 1;
            if (right < count && this->before(less, data[offset + left], data[offset + right])) {
                largest = right;
            }
            if (!this->before(less, data[offset + root], data[offset + largest])) return;
            this->exchange(data, offset + root, offset + largest);
            root = largest;
        }
    }
};

/// Introsort: quicksort until the recursion gets suspiciously deep, then
/// heapsort. This is what std::sort actually is, and it is why std::sort has a
/// hard O(n log n) worst case despite being quicksort-based.
template <typename T>
class IntroSort final : public SortStrategy<T> {
public:
    using Compare = typename SortStrategy<T>::Compare;
    using SortStrategy<T>::sort;   // keep the one-argument overload visible

    void sort(std::vector<T>& data, const Compare& less) override {
        if (data.size() < 2) return;
        std::size_t limit = 0;
        for (std::size_t n = data.size(); n > 1; n /= 2) ++limit;
        fallbackCount_ = 0;
        introsort(data, 0, data.size(), 2 * limit, less);
        insertion_.sortRange(data, 0, data.size(), less);
        fallbacks_ = fallbackCount_;
    }

    /// How many times the heapsort escape hatch fired during the last sort.
    [[nodiscard]] std::size_t fallbackCount() const noexcept { return fallbacks_; }

    [[nodiscard]] std::string name() const override { return "intro"; }
    [[nodiscard]] bool stable() const override { return false; }
    [[nodiscard]] bool inPlace() const override { return true; }
    [[nodiscard]] std::string averageComplexity() const override { return "O(n log n)"; }
    [[nodiscard]] std::string worstComplexity() const override { return "O(n log n)"; }

private:
    void introsort(std::vector<T>& data, std::size_t first, std::size_t last, std::size_t budget,
                   const Compare& less) {
        while (last - first > 16) {
            if (budget == 0) {
                ++fallbackCount_;
                heap_.sortRange(data, first, last, less);
                return;
            }
            --budget;
            const std::size_t pivot = partition(data, first, last, less);
            introsort(data, pivot, last, budget, less);
            last = pivot;
        }
    }

    std::size_t partition(std::vector<T>& data, std::size_t first, std::size_t last,
                          const Compare& less) {
        const std::size_t middle = first + (last - first) / 2;
        if (this->before(less, data[middle], data[first])) this->exchange(data, first, middle);
        if (this->before(less, data[last - 1], data[first])) this->exchange(data, first, last - 1);
        if (this->before(less, data[last - 1], data[middle])) {
            this->exchange(data, middle, last - 1);
        }
        const T pivot = data[middle];

        std::size_t low = first;
        std::size_t high = last - 1;
        for (;;) {
            while (this->before(less, data[low], pivot)) ++low;
            while (this->before(less, pivot, data[high])) --high;
            if (low >= high) return low;
            this->exchange(data, low, high);
            ++low;
            if (high == 0) return low;
            --high;
        }
    }

    HeapSort<T> heap_;
    InsertionSort<T> insertion_;
    std::size_t fallbackCount_{0};
    std::size_t fallbacks_{0};
};

/// A simplified Timsort: detect existing ascending runs, extend short ones with
/// insertion sort, then merge runs pairwise. Real Timsort adds galloping and a
/// run-stack invariant; the run detection alone already gives the O(n) best
/// case on partially ordered data, which is the property worth showing.
template <typename T>
class TimSort final : public SortStrategy<T> {
public:
    using Compare = typename SortStrategy<T>::Compare;
    using SortStrategy<T>::sort;   // keep the one-argument overload visible

    static constexpr std::size_t kMinimumRun = 32;

    void sort(std::vector<T>& data, const Compare& less) override {
        const std::size_t n = data.size();
        if (n < 2) return;

        std::vector<std::size_t> runStarts;
        std::size_t position = 0;
        while (position < n) {
            std::size_t end = position + 1;
            while (end < n && !this->before(less, data[end], data[end - 1])) ++end;
            if (end - position < kMinimumRun) {
                end = std::min(position + kMinimumRun, n);
                insertion_.sortRange(data, position, end, less);
            }
            runStarts.push_back(position);
            position = end;
        }
        runStarts.push_back(n);
        runCount_ = runStarts.size() - 1;

        std::vector<T> scratch(n);
        while (runStarts.size() > 2) {
            std::vector<std::size_t> merged;
            for (std::size_t i = 0; i + 2 < runStarts.size(); i += 2) {
                merge(data, scratch, runStarts[i], runStarts[i + 1], runStarts[i + 2], less);
                merged.push_back(runStarts[i]);
            }
            if ((runStarts.size() - 1) % 2 == 1) merged.push_back(runStarts[runStarts.size() - 2]);
            merged.push_back(n);
            runStarts = std::move(merged);
        }
    }

    /// Natural runs found in the last sort. On sorted input this is 1.
    [[nodiscard]] std::size_t runCount() const noexcept { return runCount_; }

    [[nodiscard]] std::string name() const override { return "tim"; }
    [[nodiscard]] bool stable() const override { return true; }
    [[nodiscard]] bool inPlace() const override { return false; }
    [[nodiscard]] std::string averageComplexity() const override { return "O(n log n)"; }
    [[nodiscard]] std::string worstComplexity() const override { return "O(n log n)"; }

private:
    void merge(std::vector<T>& data, std::vector<T>& scratch, std::size_t first,
               std::size_t middle, std::size_t last, const Compare& less) {
        std::size_t left = first;
        std::size_t right = middle;
        std::size_t out = first;
        while (left < middle && right < last) {
            if (this->before(less, data[right], data[left])) {
                scratch[out++] = data[right++];
            } else {
                scratch[out++] = data[left++];
            }
        }
        while (left < middle) scratch[out++] = data[left++];
        while (right < last) scratch[out++] = data[right++];
        for (std::size_t i = first; i < last; ++i) data[i] = scratch[i];
    }

    InsertionSort<T> insertion_;
    std::size_t runCount_{0};
};

// --- non-comparison family ---------------------------------------------------
//
// These beat the O(n log n) comparison lower bound by not comparing at all --
// they read the keys' structure instead. That is only possible for integer-like
// keys with a bounded range, which is exactly the restriction below.

/// Counting sort over a bounded integer range. Linear when the range is
/// comparable to n; catastrophic in memory when it is not, so the range is
/// checked and a clear error is thrown rather than a bad_alloc.
template <typename T>
    requires std::is_integral_v<T>
class CountingSort final : public SortStrategy<T> {
public:
    using Compare = typename SortStrategy<T>::Compare;
    using SortStrategy<T>::sort;   // keep the one-argument overload visible

    static constexpr std::size_t kMaximumRange = 1u << 22;   // 4M buckets

    void sort(std::vector<T>& data, const Compare&) override { sortAscending(data); }

    void sortAscending(std::vector<T>& data) {
        if (data.size() < 2) return;
        const auto bounds = std::minmax_element(data.begin(), data.end());
        const long long smallest = static_cast<long long>(*bounds.first);
        const long long largest = static_cast<long long>(*bounds.second);
        const unsigned long long span =
            static_cast<unsigned long long>(largest - smallest) + 1ull;
        if (span > kMaximumRange) {
            throw InvalidArgument("counting sort needs a bounded key range; use radix or quick");
        }

        std::vector<std::size_t> counts(static_cast<std::size_t>(span), 0);
        for (const T& value : data) ++counts[static_cast<std::size_t>(value - smallest)];
        // Prefix sums turn counts into end positions, which is what makes the
        // stable placement pass possible.
        for (std::size_t i = 1; i < counts.size(); ++i) counts[i] += counts[i - 1];

        std::vector<T> output(data.size());
        for (std::size_t i = data.size(); i-- > 0;) {
            const std::size_t bucket = static_cast<std::size_t>(data[i] - smallest);
            output[--counts[bucket]] = data[i];
        }
        data = std::move(output);
    }

    [[nodiscard]] std::string name() const override { return "counting"; }
    [[nodiscard]] bool stable() const override { return true; }
    [[nodiscard]] bool inPlace() const override { return false; }
    [[nodiscard]] std::string averageComplexity() const override { return "O(n + k)"; }
    [[nodiscard]] std::string worstComplexity() const override { return "O(n + k)"; }
};

/// LSD radix sort: eight passes of counting sort, one per byte. Handles
/// negatives by flipping the sign bit, so the unsigned ordering matches the
/// signed one.
template <typename T>
    requires std::is_integral_v<T>
class RadixSort final : public SortStrategy<T> {
public:
    using Compare = typename SortStrategy<T>::Compare;
    using SortStrategy<T>::sort;   // keep the one-argument overload visible
    using Unsigned = std::make_unsigned_t<T>;

    void sort(std::vector<T>& data, const Compare&) override { sortAscending(data); }

    void sortAscending(std::vector<T>& data) {
        if (data.size() < 2) return;

        // Bias into unsigned space so negatives order correctly.
        constexpr Unsigned kSignBit = Unsigned{1} << (sizeof(T) * 8 - 1);
        std::vector<Unsigned> keys(data.size());
        for (std::size_t i = 0; i < data.size(); ++i) {
            keys[i] = static_cast<Unsigned>(data[i]) ^ kSignBit;
        }

        std::vector<Unsigned> scratch(keys.size());
        for (std::size_t shift = 0; shift < sizeof(T) * 8; shift += 8) {
            std::size_t counts[257] = {0};
            for (Unsigned key : keys) ++counts[((key >> shift) & 0xFFu) + 1];
            for (std::size_t i = 1; i < 257; ++i) counts[i] += counts[i - 1];
            for (Unsigned key : keys) scratch[counts[(key >> shift) & 0xFFu]++] = key;
            keys.swap(scratch);
        }

        for (std::size_t i = 0; i < data.size(); ++i) {
            data[i] = static_cast<T>(keys[i] ^ kSignBit);
        }
    }

    [[nodiscard]] std::string name() const override { return "radix"; }
    [[nodiscard]] bool stable() const override { return true; }
    [[nodiscard]] bool inPlace() const override { return false; }
    [[nodiscard]] std::string averageComplexity() const override { return "O(d(n + k))"; }
    [[nodiscard]] std::string worstComplexity() const override { return "O(d(n + k))"; }
};

/// Bucket sort: scatter into n buckets by value, insertion sort each, then
/// concatenate. Linear on uniformly distributed input, quadratic when
/// everything lands in one bucket -- an honest average-case-only win.
///
/// Restricted to arithmetic types: bucketing needs the key's numeric value,
/// not just an ordering.
template <typename T>
    requires std::is_arithmetic_v<T>
class BucketSort final : public SortStrategy<T> {
public:
    using Compare = typename SortStrategy<T>::Compare;
    using SortStrategy<T>::sort;   // keep the one-argument overload visible

    void sort(std::vector<T>& data, const Compare& less) override {
        const std::size_t n = data.size();
        if (n < 2) return;

        const auto bounds = std::minmax_element(data.begin(), data.end());
        const double smallest = static_cast<double>(*bounds.first);
        const double largest = static_cast<double>(*bounds.second);
        if (!(smallest < largest)) return;   // all equal, already sorted

        std::vector<std::vector<T>> buckets(n);
        const double span = largest - smallest;
        for (const T& value : data) {
            const double normalised = (static_cast<double>(value) - smallest) / span;
            std::size_t index = static_cast<std::size_t>(normalised * static_cast<double>(n - 1));
            if (index >= n) index = n - 1;
            buckets[index].push_back(value);
        }

        std::size_t out = 0;
        for (std::vector<T>& bucket : buckets) {
            insertion_.sortRange(bucket, 0, bucket.size(), less);
            for (const T& value : bucket) data[out++] = value;
        }
    }

    [[nodiscard]] std::string name() const override { return "bucket"; }
    [[nodiscard]] bool stable() const override { return true; }
    [[nodiscard]] bool inPlace() const override { return false; }
    [[nodiscard]] std::string averageComplexity() const override { return "O(n + k)"; }
    [[nodiscard]] std::string worstComplexity() const override { return "O(n^2)"; }

private:
    InsertionSort<T> insertion_;
};

// --- factory -----------------------------------------------------------------

/// Builds a strategy by name (Factory pattern), which is what lets the CLI and
/// the benchmark accept an algorithm as a string argument.
template <typename T>
[[nodiscard]] std::unique_ptr<SortStrategy<T>> makeSortStrategy(const std::string& algorithm) {
    if (algorithm == "bubble") return std::make_unique<BubbleSort<T>>();
    if (algorithm == "insertion") return std::make_unique<InsertionSort<T>>();
    if (algorithm == "selection") return std::make_unique<SelectionSort<T>>();
    if (algorithm == "shell") return std::make_unique<ShellSort<T>>();
    if (algorithm == "merge") return std::make_unique<MergeSort<T>>();
    if (algorithm == "quick") return std::make_unique<QuickSort<T>>();
    if (algorithm == "heap") return std::make_unique<HeapSort<T>>();
    if (algorithm == "intro") return std::make_unique<IntroSort<T>>();
    if (algorithm == "tim") return std::make_unique<TimSort<T>>();
    if constexpr (std::is_arithmetic_v<T>) {
        if (algorithm == "bucket") return std::make_unique<BucketSort<T>>();
    }
    if constexpr (std::is_integral_v<T>) {
        if (algorithm == "counting") return std::make_unique<CountingSort<T>>();
        if (algorithm == "radix") return std::make_unique<RadixSort<T>>();
    }
    throw InvalidArgument("unknown sorting algorithm: " + algorithm);
}

/// Names accepted by makeSortStrategy for the given element type.
template <typename T>
[[nodiscard]] std::vector<std::string> availableSortStrategies() {
    std::vector<std::string> names{"bubble", "insertion", "selection", "shell",
                                   "merge",  "quick",     "heap",      "intro",
                                   "tim"};
    if constexpr (std::is_arithmetic_v<T>) {
        names.push_back("bucket");
    }
    if constexpr (std::is_integral_v<T>) {
        names.push_back("counting");
        names.push_back("radix");
    }
    return names;
}

/// One-shot convenience: sort a copy with the named algorithm.
template <typename T>
[[nodiscard]] std::vector<T> sorted(std::vector<T> data, const std::string& algorithm = "intro") {
    makeSortStrategy<T>(algorithm)->sort(data);
    return data;
}

/// True when `data` is in non-decreasing order.
template <typename T>
[[nodiscard]] bool isSorted(const std::vector<T>& data) {
    for (std::size_t i = 1; i < data.size(); ++i) {
        if (data[i] < data[i - 1]) return false;
    }
    return true;
}

}  // namespace daedalus

#endif  // DAEDALUS_ALGORITHMS_SORTING_HPP
