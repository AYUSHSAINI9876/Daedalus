// ============================================================================
//  Daedalus :: algorithms/DivideAndConquer.hpp
//
//    countInversions       O(n log n)   piggy-backs on a merge sort
//    closestPair           O(n log n)   the strip argument
//    karatsuba             O(n^1.585)   big-integer multiplication
//    quickselect           O(n) average the kth smallest without sorting
//    medianOfMedians       O(n) worst   quickselect with a guaranteed pivot
//    majorityElement       O(n)         Boyer-Moore voting
//    maximumSubarray       O(n log n)   the D&C framing of Kadane's problem
//
//  Each is here because it shows a different flavour of the same idea: solve
//  halves, then do work to combine them. The combine step is where the actual
//  algorithm lives -- merging while counting, scanning a strip, folding
//  partial products.
// ============================================================================
#ifndef DAEDALUS_ALGORITHMS_DIVIDE_AND_CONQUER_HPP
#define DAEDALUS_ALGORITHMS_DIVIDE_AND_CONQUER_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Exception.hpp"

namespace daedalus {

/// Number of pairs (i, j) with i < j and values[i] > values[j] -- a measure of
/// how far from sorted the input is. Counted during a merge sort: when an
/// element is taken from the right half, it is smaller than every element
/// remaining in the left half, so a whole block of inversions is counted at
/// once.
[[nodiscard]] inline long long countInversions(std::vector<long long> values) {
    if (values.size() < 2) return 0;
    std::vector<long long> scratch(values.size());

    struct Counter {
        std::vector<long long>& values;
        std::vector<long long>& scratch;

        long long sortAndCount(std::size_t first, std::size_t last) {
            if (last - first < 2) return 0;
            const std::size_t middle = first + (last - first) / 2;
            long long inversions = sortAndCount(first, middle) + sortAndCount(middle, last);

            std::size_t left = first;
            std::size_t right = middle;
            std::size_t out = first;
            while (left < middle && right < last) {
                if (values[right] < values[left]) {
                    // values[right] is smaller than the whole left remainder.
                    inversions += static_cast<long long>(middle - left);
                    scratch[out++] = values[right++];
                } else {
                    scratch[out++] = values[left++];
                }
            }
            while (left < middle) scratch[out++] = values[left++];
            while (right < last) scratch[out++] = values[right++];
            for (std::size_t i = first; i < last; ++i) values[i] = scratch[i];
            return inversions;
        }
    };

    return Counter{values, scratch}.sortAndCount(0, values.size());
}

// --- closest pair of points --------------------------------------------------

struct Point {
    double x{0.0};
    double y{0.0};
    bool operator==(const Point& other) const { return x == other.x && y == other.y; }
};

struct ClosestPairResult {
    double distance{0.0};
    Point first;
    Point second;
};

[[nodiscard]] inline double euclideanDistance(const Point& a, const Point& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

/// Closest pair in O(n log n). After solving both halves, only points within
/// the current best distance of the dividing line can beat it, and within that
/// strip -- sorted by y -- each point needs to be compared with at most the
/// next seven. That constant is what makes the combine step linear.
[[nodiscard]] inline ClosestPairResult closestPair(std::vector<Point> points) {
    require(points.size() >= 2, "closestPair needs at least two points");

    std::sort(points.begin(), points.end(),
              [](const Point& a, const Point& b) { return a.x < b.x; });

    struct Solver {
        std::vector<Point>& points;

        ClosestPairResult solve(std::size_t first, std::size_t last) {
            const std::size_t count = last - first;
            if (count <= 3) return bruteForce(first, last);

            const std::size_t middle = first + count / 2;
            const double dividingX = points[middle].x;

            ClosestPairResult best = solve(first, middle);
            const ClosestPairResult right = solve(middle, last);
            if (right.distance < best.distance) best = right;

            // Points close enough to the dividing line to matter.
            std::vector<Point> strip;
            for (std::size_t i = first; i < last; ++i) {
                if (std::abs(points[i].x - dividingX) < best.distance) strip.push_back(points[i]);
            }
            std::sort(strip.begin(), strip.end(),
                      [](const Point& a, const Point& b) { return a.y < b.y; });

            for (std::size_t i = 0; i < strip.size(); ++i) {
                for (std::size_t j = i + 1;
                     j < strip.size() && (strip[j].y - strip[i].y) < best.distance; ++j) {
                    const double candidate = euclideanDistance(strip[i], strip[j]);
                    if (candidate < best.distance) {
                        best = ClosestPairResult{candidate, strip[i], strip[j]};
                    }
                }
            }
            return best;
        }

        ClosestPairResult bruteForce(std::size_t first, std::size_t last) {
            ClosestPairResult best{std::numeric_limits<double>::max(), Point{}, Point{}};
            for (std::size_t i = first; i < last; ++i) {
                for (std::size_t j = i + 1; j < last; ++j) {
                    const double candidate = euclideanDistance(points[i], points[j]);
                    if (candidate < best.distance) {
                        best = ClosestPairResult{candidate, points[i], points[j]};
                    }
                }
            }
            return best;
        }
    };

    return Solver{points}.solve(0, points.size());
}

// --- Karatsuba ---------------------------------------------------------------

/// Multiplies two non-negative decimal strings. Schoolbook multiplication does
/// n^2 digit products; Karatsuba replaces four half-size products with three
/// (ac, bd, and (a+b)(c+d) - ac - bd), giving n^1.585.
[[nodiscard]] inline std::string karatsubaMultiply(const std::string& a, const std::string& b);

namespace detail {

[[nodiscard]] inline std::string stripLeadingZeros(const std::string& value) {
    const std::size_t firstDigit = value.find_first_not_of('0');
    return firstDigit == std::string::npos ? "0" : value.substr(firstDigit);
}

[[nodiscard]] inline std::string addDecimal(const std::string& a, const std::string& b) {
    std::string result;
    int carry = 0;
    std::size_t i = a.size();
    std::size_t j = b.size();
    while (i > 0 || j > 0 || carry != 0) {
        int sum = carry;
        if (i > 0) sum += a[--i] - '0';
        if (j > 0) sum += b[--j] - '0';
        result += static_cast<char>('0' + sum % 10);
        carry = sum / 10;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

/// a - b, where a >= b is assumed.
[[nodiscard]] inline std::string subtractDecimal(const std::string& a, const std::string& b) {
    std::string result;
    int borrow = 0;
    std::size_t i = a.size();
    std::size_t j = b.size();
    while (i > 0) {
        int difference = (a[--i] - '0') - borrow - (j > 0 ? b[--j] - '0' : 0);
        borrow = 0;
        if (difference < 0) {
            difference += 10;
            borrow = 1;
        }
        result += static_cast<char>('0' + difference);
    }
    std::reverse(result.begin(), result.end());
    return stripLeadingZeros(result);
}

}   // namespace detail

[[nodiscard]] inline std::string karatsubaMultiply(const std::string& a, const std::string& b) {
    for (char c : a + b) {
        if (c < '0' || c > '9') throw InvalidArgument("karatsuba expects decimal digits only");
    }
    const std::string left = detail::stripLeadingZeros(a);
    const std::string right = detail::stripLeadingZeros(b);
    if (left == "0" || right == "0") return "0";

    // Base case: schoolbook is faster below the crossover.
    if (left.size() <= 8 && right.size() <= 8) {
        const long long product = std::stoll(left) * std::stoll(right);
        return std::to_string(product);
    }

    const std::size_t width = std::max(left.size(), right.size());
    const std::size_t half = width / 2;
    const std::string paddedLeft = std::string(width - left.size(), '0') + left;
    const std::string paddedRight = std::string(width - right.size(), '0') + right;

    const std::size_t split = width - half;
    const std::string highLeft = paddedLeft.substr(0, split);
    const std::string lowLeft = paddedLeft.substr(split);
    const std::string highRight = paddedRight.substr(0, split);
    const std::string lowRight = paddedRight.substr(split);

    const std::string highProduct = karatsubaMultiply(highLeft, highRight);
    const std::string lowProduct = karatsubaMultiply(lowLeft, lowRight);
    const std::string crossProduct = karatsubaMultiply(detail::addDecimal(highLeft, lowLeft),
                                                       detail::addDecimal(highRight, lowRight));
    // The third product, minus the two we already have, is the middle term.
    const std::string middle =
        detail::subtractDecimal(detail::subtractDecimal(crossProduct, highProduct), lowProduct);

    const std::string shiftedHigh = highProduct + std::string(2 * half, '0');
    const std::string shiftedMiddle = middle + std::string(half, '0');
    return detail::stripLeadingZeros(
        detail::addDecimal(detail::addDecimal(shiftedHigh, shiftedMiddle), lowProduct));
}

// --- selection ---------------------------------------------------------------

/// kth smallest (0-based) without a full sort. Expected O(n): each partition
/// discards one side entirely instead of recursing into both.
template <typename T>
[[nodiscard]] T quickselect(std::vector<T> values, std::size_t k, std::uint32_t seed = 12345u) {
    require(k < values.size(), "quickselect index is out of range");
    std::mt19937 rng(seed);

    std::size_t low = 0;
    std::size_t high = values.size() - 1;
    for (;;) {
        if (low == high) return values[low];

        // A random pivot makes the adversarial input unlikely rather than
        // impossible; medianOfMedians below makes it impossible.
        const std::size_t pivotIndex = low + rng() % (high - low + 1);
        std::swap(values[pivotIndex], values[high]);
        const T pivot = values[high];

        std::size_t boundary = low;
        for (std::size_t i = low; i < high; ++i) {
            if (values[i] < pivot) std::swap(values[i], values[boundary++]);
        }
        std::swap(values[boundary], values[high]);

        if (k == boundary) return values[boundary];
        if (k < boundary) {
            high = boundary - 1;
        } else {
            low = boundary + 1;
        }
    }
}

/// kth smallest with a worst-case O(n) guarantee. The pivot is the median of
/// the medians of five-element groups, which is provably good enough to discard
/// at least 30% of the input every round.
template <typename T>
[[nodiscard]] T medianOfMedians(std::vector<T> values, std::size_t k) {
    require(k < values.size(), "selection index is out of range");

    struct Selector {
        std::vector<T>& values;

        T select(std::size_t low, std::size_t high, std::size_t k) {
            for (;;) {
                if (low == high) return values[low];
                const std::size_t pivotIndex = choosePivot(low, high);
                const std::size_t boundary = partition(low, high, pivotIndex);
                if (k == boundary) return values[boundary];
                if (k < boundary) {
                    high = boundary - 1;
                } else {
                    low = boundary + 1;
                }
            }
        }

        std::size_t choosePivot(std::size_t low, std::size_t high) {
            const std::size_t count = high - low + 1;
            if (count <= 5) return medianOfGroup(low, high);

            std::size_t writeAt = low;
            for (std::size_t start = low; start <= high; start += 5) {
                const std::size_t end = std::min(start + 4, high);
                const std::size_t median = medianOfGroup(start, end);
                std::swap(values[median], values[writeAt++]);
            }
            // Recurse on the gathered medians. select() leaves the chosen
            // element in its final position, so its index is the pivot.
            const std::size_t medianCount = writeAt - low;
            const std::size_t medianIndex = low + medianCount / 2;
            (void)select(low, low + medianCount - 1, medianIndex);
            return medianIndex;
        }

        std::size_t medianOfGroup(std::size_t low, std::size_t high) {
            std::sort(values.begin() + static_cast<std::ptrdiff_t>(low),
                      values.begin() + static_cast<std::ptrdiff_t>(high) + 1);
            return low + (high - low) / 2;
        }

        std::size_t partition(std::size_t low, std::size_t high, std::size_t pivotIndex) {
            const T pivot = values[pivotIndex];
            std::swap(values[pivotIndex], values[high]);
            std::size_t boundary = low;
            for (std::size_t i = low; i < high; ++i) {
                if (values[i] < pivot) std::swap(values[i], values[boundary++]);
            }
            std::swap(values[boundary], values[high]);
            return boundary;
        }
    };

    return Selector{values}.select(0, values.size() - 1, k);
}

/// Median, taking the lower of the two middles for an even count.
template <typename T>
[[nodiscard]] T median(std::vector<T> values) {
    require(!values.empty(), "median needs a non-empty input");
    // Compute the index BEFORE the move: argument evaluation order is
    // unspecified, so reading values.size() inside the call is a live bug.
    const std::size_t middle = (values.size() - 1) / 2;
    return quickselect(std::move(values), middle);
}

// --- voting ------------------------------------------------------------------

/// The element appearing more than n/2 times, if one exists. Boyer-Moore
/// voting: a candidate and a counter, in O(1) space. The second pass is
/// mandatory -- the first only finds a candidate, it does not verify one.
template <typename T>
[[nodiscard]] std::optional<T> majorityElement(const std::vector<T>& values) {
    if (values.empty()) return std::nullopt;

    T candidate = values[0];
    std::size_t votes = 0;
    for (const T& value : values) {
        if (votes == 0) {
            candidate = value;
            votes = 1;
        } else if (value == candidate) {
            ++votes;
        } else {
            --votes;
        }
    }

    std::size_t occurrences = 0;
    for (const T& value : values) {
        if (value == candidate) ++occurrences;
    }
    if (occurrences * 2 > values.size()) return candidate;
    return std::nullopt;
}

}   // namespace daedalus

#endif   // DAEDALUS_ALGORITHMS_DIVIDE_AND_CONQUER_HPP
