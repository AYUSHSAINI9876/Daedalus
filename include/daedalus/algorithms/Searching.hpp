// ============================================================================
//  Daedalus :: algorithms/Searching.hpp
//
//  Search over sorted (and nearly-sorted) data.
//
//    linearSearch          O(n)          works on anything
//    binarySearch          O(log n)      the baseline
//    lowerBound/upperBound O(log n)      the boundary variants that actually
//                                        get used in practice
//    exponentialSearch     O(log i)      unbounded or unknown-length input
//    jumpSearch            O(sqrt n)     when a backward step is expensive
//    interpolationSearch   O(log log n)  uniformly distributed keys, O(n) worst
//    ternarySearch         O(log n)      the maximum of a unimodal function
//    searchRotated         O(log n)      a sorted array rotated at an unknown
//                                        pivot
//    binarySearchAnswer    O(log range)  the "binary search on the answer"
//                                        pattern, over a monotone predicate
//
//  Every midpoint is computed as low + (high - low) / 2, never (low + high) / 2.
//  The latter overflows on large indices -- famously a bug that sat in the JDK
//  for nine years.
// ============================================================================
#ifndef DAEDALUS_ALGORITHMS_SEARCHING_HPP
#define DAEDALUS_ALGORITHMS_SEARCHING_HPP

#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/core/Exception.hpp"

namespace daedalus {

/// Index of the first element equal to `target`, or nullopt.
template <typename T>
[[nodiscard]] std::optional<std::size_t> linearSearch(const std::vector<T>& data, const T& target) {
    for (std::size_t i = 0; i < data.size(); ++i) {
        if (data[i] == target) return i;
    }
    return std::nullopt;
}

/// Classic binary search over ascending data. Returns SOME matching index when
/// duplicates exist -- use firstOccurrence/lastOccurrence when that matters.
template <typename T>
    requires LessThanComparable<T>
[[nodiscard]] std::optional<std::size_t> binarySearch(const std::vector<T>& data, const T& target) {
    std::size_t low = 0;
    std::size_t high = data.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (data[middle] < target) {
            low = middle + 1;
        } else if (target < data[middle]) {
            high = middle;
        } else {
            return middle;
        }
    }
    return std::nullopt;
}

/// The recursive twin, kept for comparison. Identical results, O(log n) stack.
template <typename T>
    requires LessThanComparable<T>
[[nodiscard]] std::optional<std::size_t> binarySearchRecursive(const std::vector<T>& data,
                                                               const T& target, std::size_t low = 0,
                                                               std::size_t high = 0,
                                                               bool initialised = false) {
    if (!initialised) return binarySearchRecursive(data, target, 0, data.size(), true);
    if (low >= high) return std::nullopt;
    const std::size_t middle = low + (high - low) / 2;
    if (data[middle] < target) return binarySearchRecursive(data, target, middle + 1, high, true);
    if (target < data[middle]) return binarySearchRecursive(data, target, low, middle, true);
    return middle;
}

/// First index whose element is >= target (i.e. the insertion point).
template <typename T>
    requires LessThanComparable<T>
[[nodiscard]] std::size_t lowerBound(const std::vector<T>& data, const T& target) {
    std::size_t low = 0;
    std::size_t high = data.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (data[middle] < target) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

/// First index whose element is > target.
template <typename T>
    requires LessThanComparable<T>
[[nodiscard]] std::size_t upperBound(const std::vector<T>& data, const T& target) {
    std::size_t low = 0;
    std::size_t high = data.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (target < data[middle]) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    return low;
}

template <typename T>
    requires LessThanComparable<T>
[[nodiscard]] std::optional<std::size_t> firstOccurrence(const std::vector<T>& data,
                                                         const T& target) {
    const std::size_t index = lowerBound(data, target);
    if (index < data.size() && !(target < data[index]) && !(data[index] < target)) return index;
    return std::nullopt;
}

template <typename T>
    requires LessThanComparable<T>
[[nodiscard]] std::optional<std::size_t> lastOccurrence(const std::vector<T>& data,
                                                        const T& target) {
    const std::size_t index = upperBound(data, target);
    if (index == 0) return std::nullopt;
    if (!(target < data[index - 1]) && !(data[index - 1] < target)) return index - 1;
    return std::nullopt;
}

/// How many times `target` appears, in O(log n) rather than O(n).
template <typename T>
    requires LessThanComparable<T>
[[nodiscard]] std::size_t countOccurrences(const std::vector<T>& data, const T& target) {
    return upperBound(data, target) - lowerBound(data, target);
}

/// Exponential search: double a bound until it overshoots, then binary search
/// the last doubling window. Costs O(log i) where i is the answer's index, so
/// it beats plain binary search when the target is near the front -- and it is
/// the only option when the length is not known up front.
template <typename T>
    requires LessThanComparable<T>
[[nodiscard]] std::optional<std::size_t> exponentialSearch(const std::vector<T>& data,
                                                           const T& target) {
    if (data.empty()) return std::nullopt;
    if (!(data[0] < target) && !(target < data[0])) return 0;

    std::size_t bound = 1;
    while (bound < data.size() && data[bound] < target) bound *= 2;

    const std::size_t low = bound / 2;
    const std::size_t high = bound < data.size() ? bound + 1 : data.size();
    const std::vector<T> window(data.begin() + static_cast<std::ptrdiff_t>(low),
                                data.begin() + static_cast<std::ptrdiff_t>(high));
    const auto found = binarySearch(window, target);
    if (!found.has_value()) return std::nullopt;
    return low + *found;
}

/// Jump search: step forward in blocks of sqrt(n), then scan back through one
/// block. Fewer backward steps than binary search, which matters on media where
/// seeking backwards costs more than seeking forwards.
template <typename T>
    requires LessThanComparable<T>
[[nodiscard]] std::optional<std::size_t> jumpSearch(const std::vector<T>& data, const T& target) {
    const std::size_t n = data.size();
    if (n == 0) return std::nullopt;

    const std::size_t step = static_cast<std::size_t>(std::sqrt(static_cast<double>(n))) + 1;
    std::size_t previous = 0;
    std::size_t current = 0;
    while (current < n && data[current] < target) {
        previous = current;
        current += step;
    }
    const std::size_t last = current < n ? current + 1 : n;
    for (std::size_t i = previous; i < last; ++i) {
        if (!(data[i] < target) && !(target < data[i])) return i;
    }
    return std::nullopt;
}

/// Interpolation search: guess where the key should be by linear interpolation
/// rather than always halving. O(log log n) on uniformly distributed keys, but
/// O(n) on a skewed distribution -- the guess can be consistently terrible.
template <typename T>
    requires std::is_arithmetic_v<T>
[[nodiscard]] std::optional<std::size_t> interpolationSearch(const std::vector<T>& data,
                                                             const T& target) {
    if (data.empty()) return std::nullopt;
    std::size_t low = 0;
    std::size_t high = data.size() - 1;

    while (low <= high && target >= data[low] && target <= data[high]) {
        if (data[high] == data[low]) {
            return data[low] == target ? std::optional<std::size_t>(low) : std::nullopt;
        }
        const double span = static_cast<double>(data[high]) - static_cast<double>(data[low]);
        const double offset = static_cast<double>(target) - static_cast<double>(data[low]);
        const double fraction = offset / span;
        const std::size_t guess =
            low + static_cast<std::size_t>(fraction * static_cast<double>(high - low));

        if (guess > high) return std::nullopt;
        if (data[guess] == target) return guess;
        if (data[guess] < target) {
            low = guess + 1;
        } else {
            if (guess == 0) return std::nullopt;
            high = guess - 1;
        }
    }
    return std::nullopt;
}

/// Index of the maximum of a unimodal (strictly increasing then decreasing)
/// sequence. Binary search does not apply -- the data is not sorted -- but
/// comparing two interior points still discards a third of the range each step.
template <typename T>
    requires LessThanComparable<T>
[[nodiscard]] std::optional<std::size_t> ternarySearchMaximum(const std::vector<T>& data) {
    if (data.empty()) return std::nullopt;
    std::size_t low = 0;
    std::size_t high = data.size() - 1;
    while (high - low > 2) {
        const std::size_t third = (high - low) / 3;
        const std::size_t left = low + third;
        const std::size_t right = high - third;
        if (data[left] < data[right]) {
            low = left + 1;
        } else {
            high = right - 1;
        }
    }
    std::size_t best = low;
    for (std::size_t i = low; i <= high; ++i) {
        if (data[best] < data[i]) best = i;
    }
    return best;
}

/// Search a sorted array that has been rotated at an unknown pivot. At every
/// step at least one half is still properly sorted; identifying which one is
/// the whole trick.
template <typename T>
    requires LessThanComparable<T>
[[nodiscard]] std::optional<std::size_t> searchRotated(const std::vector<T>& data,
                                                       const T& target) {
    if (data.empty()) return std::nullopt;
    std::size_t low = 0;
    std::size_t high = data.size() - 1;

    while (low <= high) {
        const std::size_t middle = low + (high - low) / 2;
        if (!(data[middle] < target) && !(target < data[middle])) return middle;

        if (!(data[high] < data[middle])) {
            // The right half is sorted.
            if (data[middle] < target && !(data[high] < target)) {
                low = middle + 1;
            } else {
                if (middle == 0) return std::nullopt;
                high = middle - 1;
            }
        } else {
            // The left half is sorted.
            if (!(target < data[low]) && target < data[middle]) {
                if (middle == 0) return std::nullopt;
                high = middle - 1;
            } else {
                low = middle + 1;
            }
        }
    }
    return std::nullopt;
}

/// Index of the rotation pivot, i.e. the position of the smallest element.
/// Zero when the array is not rotated at all.
template <typename T>
    requires LessThanComparable<T>
[[nodiscard]] std::size_t rotationPivot(const std::vector<T>& data) {
    if (data.empty()) return 0;
    std::size_t low = 0;
    std::size_t high = data.size() - 1;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (data[high] < data[middle]) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

/// Any index that is greater than both its neighbours (edges count as
/// negative infinity). O(log n): whichever side slopes up must contain a peak.
template <typename T>
    requires LessThanComparable<T>
[[nodiscard]] std::optional<std::size_t> findPeak(const std::vector<T>& data) {
    if (data.empty()) return std::nullopt;
    std::size_t low = 0;
    std::size_t high = data.size() - 1;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        if (data[middle] < data[middle + 1]) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

/// "Binary search on the answer": given a predicate that is false up to some
/// threshold and true from then on, find the smallest value where it holds.
/// This is the pattern behind capacity-planning and minimax problems.
[[nodiscard]] inline long long binarySearchAnswer(long long low, long long high,
                                                  const std::function<bool(long long)>& holds) {
    require(low <= high, "binarySearchAnswer needs a non-empty range");
    while (low < high) {
        const long long middle = low + (high - low) / 2;
        if (holds(middle)) {
            high = middle;
        } else {
            low = middle + 1;
        }
    }
    return low;
}

/// Integer square root by binary search -- a worked example of the pattern
/// above, and overflow-safe because it divides rather than multiplies.
[[nodiscard]] inline long long integerSquareRoot(long long value) {
    require(value >= 0, "integerSquareRoot needs a non-negative value");
    long long low = 0;
    long long high = value;
    long long best = 0;
    while (low <= high) {
        const long long middle = low + (high - low) / 2;
        if (middle == 0 || middle <= value / middle) {
            best = middle;
            low = middle + 1;
        } else {
            high = middle - 1;
        }
    }
    return best;
}

}   // namespace daedalus

#endif   // DAEDALUS_ALGORITHMS_SEARCHING_HPP
