// ============================================================================
//  Daedalus :: algorithms/Greedy.hpp
//
//  Greedy algorithms that are provably optimal, and one that is deliberately
//  not.
//
//    activitySelection     sort by finish time -- exchange argument proves it
//    fractionalKnapsack    sort by value density -- optimal ONLY because items
//                          can be split; the 0/1 version needs DP (see
//                          DynamicProgramming.hpp), and a test here shows
//                          greedy failing on it
//    huffmanCoding         merge the two rarest symbols -- optimal prefix code
//    jobSequencing         deadline scheduling for maximum profit
//    minimumPlatforms      the classic sweep over arrivals and departures
//    minimumCoinsGreedy    optimal for canonical coin systems only, and the
//                          test proves it wrong on {1, 3, 4}
//
//  Being explicit about where greedy breaks is the point: "use a greedy
//  algorithm" is only a valid answer when the exchange argument holds.
// ============================================================================
#ifndef DAEDALUS_ALGORITHMS_GREEDY_HPP
#define DAEDALUS_ALGORITHMS_GREEDY_HPP

#include <algorithm>
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Exception.hpp"

namespace daedalus {

// --- activity selection ------------------------------------------------------

struct Activity {
    long long start{0};
    long long finish{0};
    std::string label;
};

/// The largest set of mutually non-overlapping activities. Sorting by FINISH
/// time is what makes greedy correct: taking the earliest-finishing compatible
/// activity always leaves at least as much room as any alternative.
[[nodiscard]] inline std::vector<Activity> activitySelection(std::vector<Activity> activities) {
    std::sort(activities.begin(), activities.end(),
              [](const Activity& a, const Activity& b) { return a.finish < b.finish; });

    std::vector<Activity> chosen;
    long long lastFinish = std::numeric_limits<long long>::min();
    for (const Activity& activity : activities) {
        if (activity.start >= lastFinish) {
            chosen.push_back(activity);
            lastFinish = activity.finish;
        }
    }
    return chosen;
}

// --- fractional knapsack -----------------------------------------------------

struct FractionalKnapsackResult {
    double value{0.0};
    std::vector<std::pair<std::size_t, double>> fractions;   ///< (item index, 0..1)
};

/// Maximises value under a weight budget when items are divisible. Sorting by
/// value/weight is optimal here and NOT optimal for 0/1 knapsack -- the
/// difference is entirely that a fraction of the best-density item can fill
/// whatever space is left over.
[[nodiscard]] inline FractionalKnapsackResult fractionalKnapsack(
    const std::vector<double>& weights, const std::vector<double>& values, double capacity) {
    require(weights.size() == values.size(), "knapsack needs one value per weight");
    require(capacity >= 0.0, "knapsack capacity must be non-negative");

    std::vector<std::size_t> order(weights.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return values[a] / weights[a] > values[b] / weights[b];
    });

    FractionalKnapsackResult result;
    double remaining = capacity;
    for (std::size_t index : order) {
        if (remaining <= 0.0) break;
        if (weights[index] <= remaining) {
            result.value += values[index];
            result.fractions.push_back({index, 1.0});
            remaining -= weights[index];
        } else {
            const double fraction = remaining / weights[index];
            result.value += values[index] * fraction;
            result.fractions.push_back({index, fraction});
            remaining = 0.0;
        }
    }
    return result;
}

// --- Huffman coding ----------------------------------------------------------

struct HuffmanResult {
    std::map<char, std::string> codes;
    std::size_t encodedBits{0};
    std::size_t fixedWidthBits{0};   ///< what a naive fixed-width code would cost
    [[nodiscard]] double compressionRatio() const {
        return fixedWidthBits == 0 ? 1.0
                                   : static_cast<double>(encodedBits) /
                                         static_cast<double>(fixedWidthBits);
    }
};

/// Builds an optimal prefix code by repeatedly merging the two least frequent
/// symbols. Optimality follows from the exchange argument: in any optimal tree
/// the two rarest symbols can be moved to the deepest sibling pair without
/// increasing the cost.
[[nodiscard]] inline HuffmanResult huffmanCoding(const std::string& text) {
    HuffmanResult result;
    if (text.empty()) return result;

    std::map<char, std::size_t> frequency;
    for (char c : text) ++frequency[c];

    struct Node {
        std::size_t weight{0};
        char symbol{0};
        bool leaf{true};
        std::size_t order{0};   ///< insertion order, to break ties deterministically
        std::shared_ptr<Node> left;
        std::shared_ptr<Node> right;
    };

    struct Rarest {
        bool operator()(const std::shared_ptr<Node>& a, const std::shared_ptr<Node>& b) const {
            if (a->weight != b->weight) return a->weight > b->weight;   // min-heap
            return a->order > b->order;
        }
    };

    std::priority_queue<std::shared_ptr<Node>, std::vector<std::shared_ptr<Node>>, Rarest> heap;
    std::size_t nextOrder = 0;
    for (const auto& entry : frequency) {
        auto leaf = std::make_shared<Node>();
        leaf->weight = entry.second;
        leaf->symbol = entry.first;
        leaf->order = nextOrder++;
        heap.push(leaf);
    }

    // A single distinct symbol still needs one bit per occurrence.
    if (heap.size() == 1) {
        const auto only = heap.top();
        result.codes[only->symbol] = "0";
        result.encodedBits = text.size();
        result.fixedWidthBits = text.size();
        return result;
    }

    while (heap.size() > 1) {
        const auto left = heap.top();
        heap.pop();
        const auto right = heap.top();
        heap.pop();

        auto parent = std::make_shared<Node>();
        parent->weight = left->weight + right->weight;
        parent->leaf = false;
        parent->order = nextOrder++;
        parent->left = left;
        parent->right = right;
        heap.push(parent);
    }

    struct Walker {
        std::map<char, std::string>& codes;
        void walk(const std::shared_ptr<Node>& node, const std::string& prefix) {
            if (node->leaf) {
                codes[node->symbol] = prefix;
                return;
            }
            walk(node->left, prefix + "0");
            walk(node->right, prefix + "1");
        }
    };

    Walker{result.codes}.walk(heap.top(), "");

    for (const auto& entry : frequency) {
        result.encodedBits += entry.second * result.codes[entry.first].size();
    }
    std::size_t bitsPerSymbol = 1;
    while ((std::size_t{1} << bitsPerSymbol) < frequency.size()) ++bitsPerSymbol;
    result.fixedWidthBits = text.size() * bitsPerSymbol;
    return result;
}

/// Encodes with a code table produced above.
[[nodiscard]] inline std::string huffmanEncode(const std::string& text,
                                               const std::map<char, std::string>& codes) {
    std::string bits;
    for (char c : text) {
        const auto found = codes.find(c);
        if (found == codes.end()) throw InvalidArgument("no Huffman code for a character");
        bits += found->second;
    }
    return bits;
}

/// Decodes a bit string. Prefix-freeness is what makes this unambiguous.
[[nodiscard]] inline std::string huffmanDecode(const std::string& bits,
                                               const std::map<char, std::string>& codes) {
    std::map<std::string, char> reverse;
    for (const auto& entry : codes) reverse[entry.second] = entry.first;

    std::string text;
    std::string current;
    for (char bit : bits) {
        current += bit;
        const auto found = reverse.find(current);
        if (found != reverse.end()) {
            text += found->second;
            current.clear();
        }
    }
    if (!current.empty()) throw InvalidArgument("Huffman bit string is truncated");
    return text;
}

// --- scheduling --------------------------------------------------------------

struct Job {
    std::string id;
    std::size_t deadline{0};   ///< latest time slot (1-based) the job may occupy
    long long profit{0};
};

struct JobScheduleResult {
    std::vector<std::string> sequence;   ///< slot order, empty strings are idle
    long long totalProfit{0};
    std::size_t scheduledCount{0};
};

/// Unit-length jobs with deadlines and profits, one machine. Take the most
/// profitable job first and place it in the LATEST free slot before its
/// deadline, so earlier slots stay open for tighter jobs.
[[nodiscard]] inline JobScheduleResult jobSequencing(std::vector<Job> jobs) {
    std::sort(jobs.begin(), jobs.end(),
              [](const Job& a, const Job& b) { return a.profit > b.profit; });

    std::size_t horizon = 0;
    for (const Job& job : jobs) horizon = std::max(horizon, job.deadline);

    JobScheduleResult result;
    result.sequence.assign(horizon, "");
    for (const Job& job : jobs) {
        if (job.deadline == 0) continue;
        for (std::size_t slot = std::min(horizon, job.deadline); slot > 0; --slot) {
            if (!result.sequence[slot - 1].empty()) continue;
            result.sequence[slot - 1] = job.id;
            result.totalProfit += job.profit;
            ++result.scheduledCount;
            break;
        }
    }
    return result;
}

/// Fewest platforms a station needs so no train waits. Sorting arrivals and
/// departures separately and sweeping is the trick -- the trains are never
/// matched to each other, only counted.
[[nodiscard]] inline std::size_t minimumPlatforms(std::vector<long long> arrivals,
                                                  std::vector<long long> departures) {
    require(arrivals.size() == departures.size(), "each train needs an arrival and a departure");
    std::sort(arrivals.begin(), arrivals.end());
    std::sort(departures.begin(), departures.end());

    std::size_t platforms = 0;
    std::size_t peak = 0;
    std::size_t nextArrival = 0;
    std::size_t nextDeparture = 0;

    while (nextArrival < arrivals.size()) {
        if (arrivals[nextArrival] <= departures[nextDeparture]) {
            ++platforms;
            ++nextArrival;
            peak = std::max(peak, platforms);
        } else {
            --platforms;
            ++nextDeparture;
        }
    }
    return peak;
}

/// Greedy coin change: always take the largest coin that fits. Correct for
/// canonical systems (like most real currencies) and WRONG in general -- with
/// coins {1, 3, 4} and amount 6 it returns 4+1+1 where 3+3 is optimal. Use
/// coinChangeMinimum() when the system is not known to be canonical.
[[nodiscard]] inline std::vector<long long> minimumCoinsGreedy(std::vector<long long> coins,
                                                               long long amount) {
    std::sort(coins.begin(), coins.end(), std::greater<long long>());
    std::vector<long long> used;
    for (long long coin : coins) {
        while (coin > 0 && amount >= coin) {
            used.push_back(coin);
            amount -= coin;
        }
    }
    if (amount != 0) return {};   // this coin system cannot make the amount
    return used;
}

}  // namespace daedalus

#endif  // DAEDALUS_ALGORITHMS_GREEDY_HPP
