// ============================================================================
//  Daedalus :: algorithms/DynamicProgramming.hpp
//
//  The canonical DP problems, each solved with the space optimisation that
//  actually applies to it rather than a uniform 2-D table:
//
//    maximumSubarray        Kadane, O(n) / O(1)
//    knapsack01             O(n*W) time, O(W) space via the reversed inner loop
//    unboundedKnapsack      O(n*W), forward loop -- the single direction change
//                           that turns "use once" into "use many"
//    coinChangeMinimum      O(n*amount), with the choice reconstructed
//    coinChangeWays         counts combinations, not permutations
//    longestIncreasingSubsequence  O(n log n) via patience sorting
//    editDistance           see Strings.hpp
//    matrixChainOrder       O(n^3) interval DP, with the parenthesisation
//    subsetSum / partitionEqualSubsets
//    rodCutting
//    houseRobber / houseRobberCircular
//    minimumPathSum         grid DP
//    longestCommonSubsequenceLength
//
//  Each returns the reconstructed answer where one exists, not just its value:
//  a knapsack that says "optimal value 220" without saying which items is only
//  half an answer.
// ============================================================================
#ifndef DAEDALUS_ALGORITHMS_DYNAMIC_PROGRAMMING_HPP
#define DAEDALUS_ALGORITHMS_DYNAMIC_PROGRAMMING_HPP

#include <algorithm>
#include <cstddef>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Exception.hpp"

namespace daedalus {

// --- Kadane ------------------------------------------------------------------

struct SubarrayResult {
    long long sum{0};
    std::size_t first{0};
    std::size_t last{0};   ///< inclusive
};

/// Maximum-sum contiguous subarray. Kadane's insight: the best subarray ending
/// at i either extends the best one ending at i-1 or starts fresh at i, so one
/// pass suffices. Handles all-negative input by returning the least-bad single
/// element rather than an empty sum of zero.
[[nodiscard]] inline SubarrayResult maximumSubarray(const std::vector<long long>& values) {
    require(!values.empty(), "maximumSubarray needs a non-empty array");

    SubarrayResult best{values[0], 0, 0};
    long long runningSum = values[0];
    std::size_t runningStart = 0;

    for (std::size_t i = 1; i < values.size(); ++i) {
        if (runningSum < 0) {
            runningSum = values[i];
            runningStart = i;
        } else {
            runningSum += values[i];
        }
        if (runningSum > best.sum) {
            best.sum = runningSum;
            best.first = runningStart;
            best.last = i;
        }
    }
    return best;
}

// --- knapsack ----------------------------------------------------------------

struct KnapsackResult {
    long long value{0};
    std::vector<std::size_t> chosenItems;
    long long usedCapacity{0};
};

/// 0/1 knapsack: each item may be taken at most once. The 2-D table is kept so
/// the chosen set can be reconstructed; knapsack01Value() below is the O(W)
/// space version for when only the number matters.
[[nodiscard]] inline KnapsackResult knapsack01(const std::vector<long long>& weights,
                                               const std::vector<long long>& values,
                                               long long capacity) {
    require(weights.size() == values.size(), "knapsack needs one value per weight");
    require(capacity >= 0, "knapsack capacity must be non-negative");

    const std::size_t n = weights.size();
    const std::size_t width = static_cast<std::size_t>(capacity) + 1;
    std::vector<std::vector<long long>> table(n + 1, std::vector<long long>(width, 0));

    for (std::size_t i = 1; i <= n; ++i) {
        for (std::size_t w = 0; w < width; ++w) {
            table[i][w] = table[i - 1][w];   // skip item i
            const long long weight = weights[i - 1];
            if (weight <= static_cast<long long>(w)) {
                const long long taken =
                    table[i - 1][w - static_cast<std::size_t>(weight)] + values[i - 1];
                if (taken > table[i][w]) table[i][w] = taken;
            }
        }
    }

    KnapsackResult result;
    result.value = table[n][static_cast<std::size_t>(capacity)];
    std::size_t remaining = static_cast<std::size_t>(capacity);
    for (std::size_t i = n; i > 0; --i) {
        if (table[i][remaining] == table[i - 1][remaining]) continue;   // item skipped
        result.chosenItems.push_back(i - 1);
        result.usedCapacity += weights[i - 1];
        remaining -= static_cast<std::size_t>(weights[i - 1]);
    }
    std::reverse(result.chosenItems.begin(), result.chosenItems.end());
    return result;
}

/// O(capacity) space. The inner loop runs BACKWARDS, which is what stops an
/// item being used twice within a single row.
[[nodiscard]] inline long long knapsack01Value(const std::vector<long long>& weights,
                                               const std::vector<long long>& values,
                                               long long capacity) {
    require(weights.size() == values.size(), "knapsack needs one value per weight");
    std::vector<long long> best(static_cast<std::size_t>(capacity) + 1, 0);
    for (std::size_t i = 0; i < weights.size(); ++i) {
        for (long long w = capacity; w >= weights[i]; --w) {
            best[static_cast<std::size_t>(w)] =
                std::max(best[static_cast<std::size_t>(w)],
                         best[static_cast<std::size_t>(w - weights[i])] + values[i]);
        }
    }
    return best[static_cast<std::size_t>(capacity)];
}

/// Unbounded knapsack: unlimited copies of each item. Identical to the above
/// except the inner loop runs FORWARDS, so an item can build on itself.
[[nodiscard]] inline long long unboundedKnapsack(const std::vector<long long>& weights,
                                                 const std::vector<long long>& values,
                                                 long long capacity) {
    require(weights.size() == values.size(), "knapsack needs one value per weight");
    std::vector<long long> best(static_cast<std::size_t>(capacity) + 1, 0);
    for (long long w = 0; w <= capacity; ++w) {
        for (std::size_t i = 0; i < weights.size(); ++i) {
            if (weights[i] > w) continue;
            best[static_cast<std::size_t>(w)] =
                std::max(best[static_cast<std::size_t>(w)],
                         best[static_cast<std::size_t>(w - weights[i])] + values[i]);
        }
    }
    return best[static_cast<std::size_t>(capacity)];
}

// --- coin change -------------------------------------------------------------

struct CoinChangeResult {
    bool possible{false};
    std::size_t coinCount{0};
    std::vector<long long> coinsUsed;
};

/// Fewest coins summing to `amount`. Greedy is wrong for general coin systems
/// (1, 3, 4 and amount 6: greedy gives 4+1+1, optimal is 3+3), which is exactly
/// why this is a DP.
[[nodiscard]] inline CoinChangeResult coinChangeMinimum(const std::vector<long long>& coins,
                                                        long long amount) {
    require(amount >= 0, "coin change amount must be non-negative");
    const std::size_t target = static_cast<std::size_t>(amount);
    constexpr std::size_t kUnreachable = std::numeric_limits<std::size_t>::max();

    std::vector<std::size_t> best(target + 1, kUnreachable);
    std::vector<long long> pick(target + 1, 0);
    // target + 1 is at least 1, so this table always has a zero row -- but the
    // optimiser cannot prove target + 1 does not wrap and warns at -O3. State
    // the invariant where it is used rather than leaving it implicit.
    if (best.empty()) return CoinChangeResult{};
    best[0] = 0;

    for (std::size_t value = 1; value <= target; ++value) {
        for (long long coin : coins) {
            if (coin <= 0 || static_cast<std::size_t>(coin) > value) continue;
            const std::size_t previous = best[value - static_cast<std::size_t>(coin)];
            if (previous == kUnreachable || previous + 1 >= best[value]) continue;
            best[value] = previous + 1;
            pick[value] = coin;
        }
    }

    CoinChangeResult result;
    if (best[target] == kUnreachable) return result;
    result.possible = true;
    result.coinCount = best[target];
    for (std::size_t value = target; value > 0;) {
        result.coinsUsed.push_back(pick[value]);
        value -= static_cast<std::size_t>(pick[value]);
    }
    std::sort(result.coinsUsed.begin(), result.coinsUsed.end());
    return result;
}

/// Number of distinct COMBINATIONS summing to `amount`. The coin loop is
/// outermost -- swap the loops and you count permutations instead, which is a
/// different (and usually wrong) answer.
[[nodiscard]] inline long long coinChangeWays(const std::vector<long long>& coins,
                                              long long amount) {
    require(amount >= 0, "coin change amount must be non-negative");
    std::vector<long long> ways(static_cast<std::size_t>(amount) + 1, 0);
    ways[0] = 1;
    for (long long coin : coins) {
        if (coin <= 0) continue;
        for (long long value = coin; value <= amount; ++value) {
            ways[static_cast<std::size_t>(value)] += ways[static_cast<std::size_t>(value - coin)];
        }
    }
    return ways[static_cast<std::size_t>(amount)];
}

// --- subsequences ------------------------------------------------------------

/// Longest strictly increasing subsequence, reconstructed. O(n log n) by
/// patience sorting: `tails[k]` holds the smallest possible tail of an
/// increasing subsequence of length k+1, and that array is always sorted, so
/// each element is placed with a binary search.
[[nodiscard]] inline std::vector<long long> longestIncreasingSubsequence(
    const std::vector<long long>& values) {
    if (values.empty()) return {};

    std::vector<std::size_t> tailIndex;   // index into values, per length
    std::vector<std::size_t> predecessor(values.size(), values.size());

    for (std::size_t i = 0; i < values.size(); ++i) {
        // First position whose tail is >= values[i].
        std::size_t low = 0;
        std::size_t high = tailIndex.size();
        while (low < high) {
            const std::size_t middle = low + (high - low) / 2;
            if (values[tailIndex[middle]] < values[i]) {
                low = middle + 1;
            } else {
                high = middle;
            }
        }
        if (low > 0) predecessor[i] = tailIndex[low - 1];
        if (low == tailIndex.size()) {
            tailIndex.push_back(i);
        } else {
            tailIndex[low] = i;
        }
    }

    std::vector<long long> sequence;
    for (std::size_t at = tailIndex.back(); at != values.size(); at = predecessor[at]) {
        sequence.push_back(values[at]);
    }
    std::reverse(sequence.begin(), sequence.end());
    return sequence;
}

[[nodiscard]] inline std::size_t longestCommonSubsequenceLength(const std::vector<long long>& a,
                                                                const std::vector<long long>& b) {
    std::vector<std::size_t> previous(b.size() + 1, 0);
    std::vector<std::size_t> current(b.size() + 1, 0);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        for (std::size_t j = 1; j <= b.size(); ++j) {
            current[j] =
                a[i - 1] == b[j - 1] ? previous[j - 1] + 1 : std::max(previous[j], current[j - 1]);
        }
        previous.swap(current);
    }
    return previous[b.size()];
}

// --- subset sum and partitioning --------------------------------------------

/// Can any subset reach exactly `target`? O(n * target) with a bitset-style
/// boolean row.
[[nodiscard]] inline bool subsetSumExists(const std::vector<long long>& values, long long target) {
    if (target < 0) return false;
    std::vector<bool> reachable(static_cast<std::size_t>(target) + 1, false);
    reachable[0] = true;
    for (long long value : values) {
        if (value <= 0) continue;
        for (long long sum = target; sum >= value; --sum) {
            if (reachable[static_cast<std::size_t>(sum - value)]) {
                reachable[static_cast<std::size_t>(sum)] = true;
            }
        }
    }
    return reachable[static_cast<std::size_t>(target)];
}

/// Can the multiset be split into two halves of equal sum? Reduces to a subset
/// sum for half the total, which is only possible when the total is even.
[[nodiscard]] inline bool canPartitionEqually(const std::vector<long long>& values) {
    const long long total = std::accumulate(values.begin(), values.end(), 0LL);
    if (total % 2 != 0) return false;
    return subsetSumExists(values, total / 2);
}

// --- interval DP -------------------------------------------------------------

struct MatrixChainResult {
    long long multiplications{0};
    std::string parenthesisation;
};

/// Optimal matrix-chain multiplication order. `dimensions` has n+1 entries for
/// n matrices, matrix i being dimensions[i] x dimensions[i+1]. Multiplication is
/// associative but not equally cheap: the order can change the cost by orders
/// of magnitude, and this finds the cheapest.
[[nodiscard]] inline MatrixChainResult matrixChainOrder(const std::vector<long long>& dimensions) {
    require(dimensions.size() >= 2, "matrix chain needs at least one matrix");
    const std::size_t n = dimensions.size() - 1;

    std::vector<std::vector<long long>> cost(n, std::vector<long long>(n, 0));
    std::vector<std::vector<std::size_t>> split(n, std::vector<std::size_t>(n, 0));

    for (std::size_t length = 2; length <= n; ++length) {
        for (std::size_t i = 0; i + length - 1 < n; ++i) {
            const std::size_t j = i + length - 1;
            cost[i][j] = std::numeric_limits<long long>::max();
            for (std::size_t k = i; k < j; ++k) {
                const long long candidate = cost[i][k] + cost[k + 1][j] +
                                            dimensions[i] * dimensions[k + 1] * dimensions[j + 1];
                if (candidate < cost[i][j]) {
                    cost[i][j] = candidate;
                    split[i][j] = k;
                }
            }
        }
    }

    // Recursive rendering of the chosen split points.
    struct Renderer {
        const std::vector<std::vector<std::size_t>>& split;
        std::string render(std::size_t i, std::size_t j) const {
            if (i == j) return "M" + std::to_string(i + 1);
            return "(" + render(i, split[i][j]) + " x " + render(split[i][j] + 1, j) + ")";
        }
    };

    MatrixChainResult result;
    result.multiplications = cost[0][n - 1];
    result.parenthesisation = Renderer{split}.render(0, n - 1);
    return result;
}

// --- classic one-dimensional problems ---------------------------------------

/// Best revenue from cutting a rod of length n, given per-length prices. This
/// is unbounded knapsack in disguise, kept separate because the framing is what
/// makes the reduction visible.
[[nodiscard]] inline long long rodCutting(const std::vector<long long>& prices, long long length) {
    std::vector<long long> best(static_cast<std::size_t>(length) + 1, 0);
    for (long long total = 1; total <= length; ++total) {
        for (std::size_t cut = 0; cut < prices.size() && static_cast<long long>(cut) < total;
             ++cut) {
            best[static_cast<std::size_t>(total)] =
                std::max(best[static_cast<std::size_t>(total)],
                         prices[cut] + best[static_cast<std::size_t>(total) - cut - 1]);
        }
    }
    return best[static_cast<std::size_t>(length)];
}

/// Maximum sum of non-adjacent elements.
[[nodiscard]] inline long long houseRobber(const std::vector<long long>& houses) {
    long long skip = 0;   // best if the previous house was NOT robbed
    long long take = 0;   // best if it was
    for (long long value : houses) {
        const long long nextTake = skip + value;
        skip = std::max(skip, take);
        take = nextTake;
    }
    return std::max(skip, take);
}

/// Same, but the houses form a circle, so the first and last are adjacent.
/// Solved as the better of two linear runs that each exclude one endpoint.
[[nodiscard]] inline long long houseRobberCircular(const std::vector<long long>& houses) {
    if (houses.empty()) return 0;
    if (houses.size() == 1) return houses[0];
    const std::vector<long long> withoutLast(houses.begin(), houses.end() - 1);
    const std::vector<long long> withoutFirst(houses.begin() + 1, houses.end());
    return std::max(houseRobber(withoutLast), houseRobber(withoutFirst));
}

/// Cheapest path from the top-left to the bottom-right of a grid, moving only
/// right or down.
[[nodiscard]] inline long long minimumPathSum(const std::vector<std::vector<long long>>& grid) {
    require(!grid.empty() && !grid[0].empty(), "minimumPathSum needs a non-empty grid");
    const std::size_t rows = grid.size();
    const std::size_t columns = grid[0].size();

    std::vector<long long> best(columns, 0);
    // Guaranteed non-empty by the require() above, but that check lives in
    // another function and the optimiser will not carry it here.
    if (best.empty()) return 0;
    best[0] = grid[0][0];
    for (std::size_t c = 1; c < columns; ++c) best[c] = best[c - 1] + grid[0][c];

    for (std::size_t r = 1; r < rows; ++r) {
        best[0] += grid[r][0];
        for (std::size_t c = 1; c < columns; ++c) {
            best[c] = std::min(best[c], best[c - 1]) + grid[r][c];
        }
    }
    return best[columns - 1];
}

/// Number of distinct right/down paths across an r x c grid. Pure DP here; the
/// closed form is a binomial coefficient (see NumberTheory.hpp).
[[nodiscard]] inline long long gridPathCount(std::size_t rows, std::size_t columns) {
    if (rows == 0 || columns == 0) return 0;
    std::vector<long long> row(columns, 1);
    for (std::size_t r = 1; r < rows; ++r) {
        for (std::size_t c = 1; c < columns; ++c) row[c] += row[c - 1];
    }
    return row[columns - 1];
}

}   // namespace daedalus

#endif   // DAEDALUS_ALGORITHMS_DYNAMIC_PROGRAMMING_HPP
