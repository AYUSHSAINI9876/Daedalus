// ============================================================================
//  Unit tests for the algorithms layer.
// ============================================================================
#include <algorithm>
#include <memory>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "daedalus/algorithms/Backtracking.hpp"
#include "daedalus/algorithms/DivideAndConquer.hpp"
#include "daedalus/algorithms/DynamicProgramming.hpp"
#include "daedalus/algorithms/Greedy.hpp"
#include "daedalus/algorithms/NumberTheory.hpp"
#include "daedalus/algorithms/Searching.hpp"
#include "daedalus/algorithms/Sorting.hpp"
#include "daedalus/algorithms/Strings.hpp"
#include "framework/TestFramework.hpp"

using namespace daedalus;

namespace {

std::vector<int> randomInts(std::size_t count, std::uint32_t seed, int low = -1000,
                            int high = 1000) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> values(low, high);
    std::vector<int> data(count);
    for (int& value : data) value = values(rng);
    return data;
}

std::vector<int> ascending(std::size_t count) {
    std::vector<int> data(count);
    std::iota(data.begin(), data.end(), 0);
    return data;
}

}  // namespace

// ============================================================================
//  Sorting
// ============================================================================

DAEDALUS_TEST(Sorting, every_algorithm_sorts_random_input) {
    const std::vector<int> input = randomInts(500, 11u);
    std::vector<int> expected = input;
    std::sort(expected.begin(), expected.end());

    for (const std::string& algorithm : availableSortStrategies<int>()) {
        std::vector<int> data = input;
        makeSortStrategy<int>(algorithm)->sort(data);
        CHECK_EQ(data, expected);
    }
}

DAEDALUS_TEST(Sorting, every_algorithm_handles_the_edge_cases) {
    for (const std::string& algorithm : availableSortStrategies<int>()) {
        auto strategy = makeSortStrategy<int>(algorithm);

        std::vector<int> empty;
        CHECK_NO_THROW(strategy->sort(empty));
        CHECK_EQ(empty.size(), 0u);

        std::vector<int> single{42};
        strategy->sort(single);
        CHECK_EQ(single, (std::vector<int>{42}));

        std::vector<int> pair{2, 1};
        strategy->sort(pair);
        CHECK_EQ(pair, (std::vector<int>{1, 2}));

        std::vector<int> identical(50, 7);
        strategy->sort(identical);
        CHECK_EQ(identical, std::vector<int>(50, 7));

        std::vector<int> alreadySorted = ascending(100);
        strategy->sort(alreadySorted);
        CHECK_EQ(alreadySorted, ascending(100));

        std::vector<int> reversed = ascending(100);
        std::reverse(reversed.begin(), reversed.end());
        strategy->sort(reversed);
        CHECK_EQ(reversed, ascending(100));
    }
}

DAEDALUS_TEST(Sorting, negative_values_survive_the_non_comparison_sorts) {
    const std::vector<int> input = randomInts(300, 77u, -5000, 5000);
    std::vector<int> expected = input;
    std::sort(expected.begin(), expected.end());

    for (const char* algorithm : {"counting", "radix", "bucket"}) {
        std::vector<int> data = input;
        makeSortStrategy<int>(algorithm)->sort(data);
        CHECK_EQ(data, expected);
    }
}

DAEDALUS_TEST(Sorting, stable_algorithms_preserve_equal_element_order) {
    // Pairs sorted on the first component only; the second must keep its order
    // for a stable algorithm.
    struct Item {
        int key;
        int sequence;
        bool operator==(const Item& other) const {
            return key == other.key && sequence == other.sequence;
        }
        bool operator<(const Item& other) const { return key < other.key; }
    };

    std::vector<Item> input;
    for (int i = 0; i < 60; ++i) input.push_back(Item{i % 5, i});

    for (const char* algorithm :
         {"bubble", "insertion", "merge", "tim", "selection", "quick", "heap", "shell"}) {
        auto strategy = makeSortStrategy<Item>(algorithm);
        std::vector<Item> data = input;
        strategy->sort(data);

        for (std::size_t i = 1; i < data.size(); ++i) CHECK_LE(data[i - 1].key, data[i].key);
        if (!strategy->stable()) continue;
        for (std::size_t i = 1; i < data.size(); ++i) {
            if (data[i - 1].key == data[i].key) CHECK_LT(data[i - 1].sequence, data[i].sequence);
        }
    }
}

DAEDALUS_TEST(Sorting, custom_comparator_sorts_descending) {
    std::vector<int> data{3, 1, 4, 1, 5};
    makeSortStrategy<int>("merge")->sort(data, [](int a, int b) { return a > b; });
    CHECK_EQ(data, (std::vector<int>{5, 4, 3, 1, 1}));
}

DAEDALUS_TEST(Sorting, observer_counts_show_the_complexity_gap) {
    const std::vector<int> input = randomInts(400, 5u);

    auto bubble = makeSortStrategy<int>("bubble");
    auto merge = makeSortStrategy<int>("merge");
    auto bubbleMetrics = std::make_shared<MetricsObserver>();
    auto mergeMetrics = std::make_shared<MetricsObserver>();
    bubble->attach(bubbleMetrics);
    merge->attach(mergeMetrics);

    std::vector<int> a = input;
    std::vector<int> b = input;
    bubble->sort(a);
    merge->sort(b);

    CHECK_EQ(a, b);
    CHECK_LT(0u, mergeMetrics->comparisons());
    // n^2 versus n log n on n = 400: about 80,000 versus about 3,500.
    CHECK_LT(mergeMetrics->comparisons() * 10, bubbleMetrics->comparisons());
    CHECK_TRUE(bubbleMetrics->report().find("comparison") != std::string::npos);
}

DAEDALUS_TEST(Sorting, observers_can_be_detached_and_reset) {
    auto strategy = makeSortStrategy<int>("quick");
    auto metrics = std::make_shared<MetricsObserver>();
    strategy->attach(metrics);
    CHECK_TRUE(strategy->hasObservers());

    std::vector<int> data = randomInts(100, 3u);
    strategy->sort(data);
    const std::size_t counted = metrics->comparisons();
    CHECK_LT(0u, counted);

    metrics->reset();
    CHECK_EQ(metrics->comparisons(), 0u);
    strategy->detach(metrics);
    CHECK_FALSE(strategy->hasObservers());

    std::vector<int> more = randomInts(100, 4u);
    strategy->sort(more);
    CHECK_EQ(metrics->comparisons(), 0u);   // detached: nothing recorded
}

DAEDALUS_TEST(Sorting, trace_observer_records_the_first_events) {
    auto strategy = makeSortStrategy<int>("insertion");
    auto trace = std::make_shared<TraceObserver>(8);
    strategy->attach(trace);
    std::vector<int> data{5, 4, 3, 2, 1};
    strategy->sort(data);
    CHECK_EQ(data, (std::vector<int>{1, 2, 3, 4, 5}));
    CHECK_EQ(trace->lines().size(), 8u);
    CHECK_LT(8u, trace->seen());
}

DAEDALUS_TEST(Sorting, insertion_sort_is_linear_on_sorted_input) {
    auto strategy = makeSortStrategy<int>("insertion");
    auto metrics = std::make_shared<MetricsObserver>();
    strategy->attach(metrics);
    std::vector<int> data = ascending(1000);
    strategy->sort(data);
    // One comparison per element, not one per pair.
    CHECK_LT(metrics->comparisons(), 2000u);
}

DAEDALUS_TEST(Sorting, quicksort_survives_the_all_duplicates_worst_case) {
    // A naive quicksort goes quadratic here; three-way partitioning does not.
    std::vector<int> data(20000, 7);
    auto strategy = makeSortStrategy<int>("quick");
    auto metrics = std::make_shared<MetricsObserver>();
    strategy->attach(metrics);
    CHECK_NO_THROW(strategy->sort(data));
    CHECK_TRUE(isSorted(data));
    CHECK_LT(metrics->comparisons(), 20000u * 40u);
}

DAEDALUS_TEST(Sorting, quicksort_survives_sorted_input) {
    std::vector<int> data = ascending(20000);
    CHECK_NO_THROW(makeSortStrategy<int>("quick")->sort(data));
    CHECK_TRUE(isSorted(data));
}

DAEDALUS_TEST(Sorting, timsort_detects_existing_runs) {
    TimSort<int> tim;
    std::vector<int> sortedInput = ascending(1000);
    tim.sort(sortedInput);
    CHECK_EQ(tim.runCount(), 1u);   // the whole array is already one run

    std::vector<int> shuffled = randomInts(1000, 9u);
    tim.sort(shuffled);
    CHECK_TRUE(isSorted(shuffled));
    CHECK_LT(1u, tim.runCount());
}

DAEDALUS_TEST(Sorting, strategies_report_their_own_properties) {
    CHECK_TRUE(makeSortStrategy<int>("merge")->stable());
    CHECK_FALSE(makeSortStrategy<int>("quick")->stable());
    CHECK_TRUE(makeSortStrategy<int>("heap")->inPlace());
    CHECK_FALSE(makeSortStrategy<int>("merge")->inPlace());
    CHECK_EQ(makeSortStrategy<int>("heap")->worstComplexity(), std::string("O(n log n)"));
    CHECK_EQ(makeSortStrategy<int>("quick")->worstComplexity(), std::string("O(n^2)"));
    CHECK_EQ(makeSortStrategy<int>("intro")->worstComplexity(), std::string("O(n log n)"));
    CHECK_THROWS_AS(makeSortStrategy<int>("nonsense"), InvalidArgument);
}

DAEDALUS_TEST(Sorting, counting_sort_refuses_an_unbounded_range) {
    std::vector<int> data{0, 2000000000};
    CHECK_THROWS_AS(makeSortStrategy<int>("counting")->sort(data), InvalidArgument);
}

DAEDALUS_TEST(Sorting, sorted_helper_and_is_sorted) {
    CHECK_EQ(sorted(std::vector<int>{3, 1, 2}), (std::vector<int>{1, 2, 3}));
    CHECK_EQ(sorted(std::vector<int>{3, 1, 2}, "radix"), (std::vector<int>{1, 2, 3}));
    CHECK_TRUE(isSorted(std::vector<int>{1, 1, 2}));
    CHECK_FALSE(isSorted(std::vector<int>{2, 1}));
}

DAEDALUS_TEST(Sorting, sorts_strings) {
    std::vector<std::string> data{"pear", "apple", "fig"};
    makeSortStrategy<std::string>("merge")->sort(data);
    CHECK_EQ(data, (std::vector<std::string>{"apple", "fig", "pear"}));
}

// ============================================================================
//  Searching
// ============================================================================

DAEDALUS_TEST(Searching, every_algorithm_finds_present_and_absent_keys) {
    const std::vector<int> data = ascending(1000);
    for (int target : {0, 1, 499, 999}) {
        CHECK_EQ(linearSearch(data, target).value(), static_cast<std::size_t>(target));
        CHECK_EQ(binarySearch(data, target).value(), static_cast<std::size_t>(target));
        CHECK_EQ(binarySearchRecursive(data, target).value(),
                 static_cast<std::size_t>(target));
        CHECK_EQ(exponentialSearch(data, target).value(), static_cast<std::size_t>(target));
        CHECK_EQ(jumpSearch(data, target).value(), static_cast<std::size_t>(target));
        CHECK_EQ(interpolationSearch(data, target).value(), static_cast<std::size_t>(target));
    }
    for (int absent : {-1, 1000, 5000}) {
        CHECK_FALSE(linearSearch(data, absent).has_value());
        CHECK_FALSE(binarySearch(data, absent).has_value());
        CHECK_FALSE(exponentialSearch(data, absent).has_value());
        CHECK_FALSE(jumpSearch(data, absent).has_value());
        CHECK_FALSE(interpolationSearch(data, absent).has_value());
    }
}

DAEDALUS_TEST(Searching, empty_input_is_handled_everywhere) {
    const std::vector<int> empty;
    CHECK_FALSE(linearSearch(empty, 1).has_value());
    CHECK_FALSE(binarySearch(empty, 1).has_value());
    CHECK_FALSE(exponentialSearch(empty, 1).has_value());
    CHECK_FALSE(jumpSearch(empty, 1).has_value());
    CHECK_FALSE(interpolationSearch(empty, 1).has_value());
    CHECK_FALSE(findPeak(empty).has_value());
    CHECK_EQ(lowerBound(empty, 1), 0u);
}

DAEDALUS_TEST(Searching, bounds_and_occurrence_counting) {
    const std::vector<int> data{1, 2, 2, 2, 3, 5};
    CHECK_EQ(lowerBound(data, 2), 1u);
    CHECK_EQ(upperBound(data, 2), 4u);
    CHECK_EQ(countOccurrences(data, 2), 3u);
    CHECK_EQ(countOccurrences(data, 4), 0u);
    CHECK_EQ(firstOccurrence(data, 2).value(), 1u);
    CHECK_EQ(lastOccurrence(data, 2).value(), 3u);
    CHECK_FALSE(firstOccurrence(data, 4).has_value());
    CHECK_FALSE(lastOccurrence(data, 0).has_value());
    CHECK_EQ(lowerBound(data, 0), 0u);      // insertion point before everything
    CHECK_EQ(lowerBound(data, 9), 6u);      // and after everything
}

DAEDALUS_TEST(Searching, matches_std_lower_bound_under_random_load) {
    std::mt19937 rng(2024u);
    for (int trial = 0; trial < 200; ++trial) {
        std::vector<int> data = randomInts(50, static_cast<std::uint32_t>(trial), -20, 20);
        std::sort(data.begin(), data.end());
        const int target = static_cast<int>(rng() % 45) - 22;
        const std::size_t expected = static_cast<std::size_t>(
            std::lower_bound(data.begin(), data.end(), target) - data.begin());
        CHECK_EQ(lowerBound(data, target), expected);
        CHECK_EQ(upperBound(data, target),
                 static_cast<std::size_t>(
                     std::upper_bound(data.begin(), data.end(), target) - data.begin()));
    }
}

DAEDALUS_TEST(Searching, ternary_search_on_a_unimodal_sequence) {
    const std::vector<int> data{1, 3, 8, 12, 4, 2};
    CHECK_EQ(ternarySearchMaximum(data).value(), 3u);
    CHECK_EQ(ternarySearchMaximum(std::vector<int>{5}).value(), 0u);
    CHECK_FALSE(ternarySearchMaximum(std::vector<int>{}).has_value());
}

DAEDALUS_TEST(Searching, rotated_array) {
    const std::vector<int> data{4, 5, 6, 7, 0, 1, 2};
    CHECK_EQ(searchRotated(data, 0).value(), 4u);
    CHECK_EQ(searchRotated(data, 4).value(), 0u);
    CHECK_EQ(searchRotated(data, 2).value(), 6u);
    CHECK_FALSE(searchRotated(data, 3).has_value());
    CHECK_EQ(rotationPivot(data), 4u);
    CHECK_EQ(rotationPivot(ascending(10)), 0u);   // not rotated
}

DAEDALUS_TEST(Searching, rotated_search_matches_linear_scan) {
    for (std::size_t rotation = 0; rotation < 20; ++rotation) {
        std::vector<int> data = ascending(20);
        std::rotate(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(rotation),
                    data.end());
        for (int target = -2; target < 22; ++target) {
            const auto found = searchRotated(data, target);
            const auto expected = linearSearch(data, target);
            CHECK_EQ(found.has_value(), expected.has_value());
            if (found.has_value()) CHECK_EQ(data[*found], target);
        }
    }
}

DAEDALUS_TEST(Searching, peak_finding) {
    CHECK_EQ(findPeak(std::vector<int>{1, 2, 3, 1}).value(), 2u);
    CHECK_EQ(findPeak(std::vector<int>{5, 4, 3}).value(), 0u);
    CHECK_EQ(findPeak(std::vector<int>{1, 2, 3}).value(), 2u);
}

DAEDALUS_TEST(Searching, binary_search_on_the_answer) {
    // Smallest x with x*x >= 1000.
    const long long root = binarySearchAnswer(
        0, 1000, [](long long x) { return x * x >= 1000; });
    CHECK_EQ(root, 32);
    CHECK_THROWS_AS(binarySearchAnswer(10, 1, [](long long) { return true; }), InvalidArgument);
}

DAEDALUS_TEST(Searching, integer_square_root) {
    CHECK_EQ(integerSquareRoot(0), 0);
    CHECK_EQ(integerSquareRoot(1), 1);
    CHECK_EQ(integerSquareRoot(15), 3);
    CHECK_EQ(integerSquareRoot(16), 4);
    CHECK_EQ(integerSquareRoot(1000000), 1000);
    // Would overflow if the check multiplied instead of dividing.
    CHECK_EQ(integerSquareRoot(4611686014132420609LL), 2147483647LL);
    CHECK_THROWS_AS(integerSquareRoot(-1), InvalidArgument);
}

// ============================================================================
//  Strings
// ============================================================================

DAEDALUS_TEST(Strings, all_matchers_agree_with_the_naive_one) {
    const std::vector<std::pair<std::string, std::string>> cases{
        {"abababcabababcabc", "ababc"},
        {"aaaaaaaaaa", "aaa"},          // heavy overlap
        {"abcdefghij", "j"},            // match at the very end
        {"abcdefghij", "a"},            // and at the start
        {"abcdefghij", "xyz"},          // no match
        {"", "abc"},                    // empty text
        {"abc", ""},                    // empty pattern
        {"mississippi", "issi"},        // overlapping occurrences
        {"aaa", "aaaa"},                // pattern longer than text
    };

    for (const auto& testCase : cases) {
        const std::string& text = testCase.first;
        const std::string& pattern = testCase.second;
        const std::vector<std::size_t> expected = naiveSearch(text, pattern);
        CHECK_EQ(knuthMorrisPratt(text, pattern), expected);
        CHECK_EQ(zSearch(text, pattern), expected);
        CHECK_EQ(rabinKarp(text, pattern), expected);
        CHECK_EQ(boyerMooreHorspool(text, pattern), expected);
    }
}

DAEDALUS_TEST(Strings, matchers_agree_on_random_binary_text) {
    std::mt19937 rng(4321u);
    for (int trial = 0; trial < 60; ++trial) {
        std::string text;
        for (int i = 0; i < 200; ++i) text += static_cast<char>('a' + (rng() % 3));
        std::string pattern;
        const std::size_t length = 1 + rng() % 5;
        for (std::size_t i = 0; i < length; ++i) pattern += static_cast<char>('a' + (rng() % 3));

        const std::vector<std::size_t> expected = naiveSearch(text, pattern);
        CHECK_EQ(knuthMorrisPratt(text, pattern), expected);
        CHECK_EQ(zSearch(text, pattern), expected);
        CHECK_EQ(rabinKarp(text, pattern), expected);
        CHECK_EQ(boyerMooreHorspool(text, pattern), expected);
    }
}

DAEDALUS_TEST(Strings, prefix_function_and_z_array) {
    CHECK_EQ(prefixFunction("aabaaab"),
             (std::vector<std::size_t>{0, 1, 0, 1, 2, 2, 3}));
    CHECK_EQ(zArray("aabxaab"), (std::vector<std::size_t>{7, 1, 0, 0, 3, 1, 0}));
    CHECK_EQ(zArray("").size(), 0u);
}

DAEDALUS_TEST(Strings, palindromes) {
    CHECK_EQ(longestPalindrome("babad"), std::string("bab"));
    CHECK_EQ(longestPalindrome("cbbd"), std::string("bb"));
    CHECK_EQ(longestPalindrome("a"), std::string("a"));
    CHECK_EQ(longestPalindrome(""), std::string(""));
    CHECK_EQ(longestPalindrome("forgeeksskeegfor"), std::string("geeksskeeg"));
    CHECK_TRUE(isPalindrome("racecar"));
    CHECK_TRUE(isPalindrome(""));
    CHECK_FALSE(isPalindrome("abca"));
}

DAEDALUS_TEST(Strings, manacher_matches_brute_force) {
    std::mt19937 rng(555u);
    for (int trial = 0; trial < 100; ++trial) {
        std::string text;
        for (int i = 0; i < 40; ++i) text += static_cast<char>('a' + (rng() % 3));

        // Brute-force longest palindromic substring.
        std::string best;
        for (std::size_t i = 0; i < text.size(); ++i) {
            for (std::size_t length = 1; i + length <= text.size(); ++length) {
                const std::string candidate = text.substr(i, length);
                if (candidate.size() > best.size() && isPalindrome(candidate)) best = candidate;
            }
        }
        CHECK_EQ(longestPalindrome(text).size(), best.size());
        CHECK_TRUE(isPalindrome(longestPalindrome(text)));
    }
}

DAEDALUS_TEST(Strings, suffix_array_and_lcp) {
    const std::string text = "banana";
    const std::vector<std::size_t> suffixes = suffixArray(text);
    // Suffixes sorted: a(5), ana(3), anana(1), banana(0), na(4), nana(2)
    CHECK_EQ(suffixes, (std::vector<std::size_t>{5, 3, 1, 0, 4, 2}));

    const std::vector<std::size_t> lcp = longestCommonPrefixArray(text, suffixes);
    CHECK_EQ(lcp, (std::vector<std::size_t>{0, 1, 3, 0, 0, 2}));
    CHECK_EQ(longestRepeatedSubstring(text), std::string("ana"));
    CHECK_EQ(longestRepeatedSubstring("abc"), std::string(""));
}

DAEDALUS_TEST(Strings, suffix_array_matches_a_direct_sort) {
    std::mt19937 rng(6789u);
    for (int trial = 0; trial < 40; ++trial) {
        std::string text;
        for (int i = 0; i < 30; ++i) text += static_cast<char>('a' + (rng() % 4));

        std::vector<std::size_t> expected(text.size());
        std::iota(expected.begin(), expected.end(), std::size_t{0});
        std::sort(expected.begin(), expected.end(), [&](std::size_t a, std::size_t b) {
            return text.compare(a, std::string::npos, text, b, std::string::npos) < 0;
        });
        CHECK_EQ(suffixArray(text), expected);
    }
}

DAEDALUS_TEST(Strings, aho_corasick_finds_every_pattern_in_one_pass) {
    AhoCorasick automaton({"he", "she", "his", "hers"});
    const auto matches = automaton.search("ushers");

    std::set<std::pair<std::size_t, std::string>> found;
    for (const auto& match : matches) found.insert({match.position, match.pattern});

    // "ushers" contains she@1, he@2, hers@2.
    CHECK_TRUE(found.count({1, "she"}) == 1);
    CHECK_TRUE(found.count({2, "he"}) == 1);
    CHECK_TRUE(found.count({2, "hers"}) == 1);
    CHECK_EQ(found.size(), 3u);
    CHECK_EQ(automaton.patternCount(), 4u);
}

DAEDALUS_TEST(Strings, aho_corasick_agrees_with_running_kmp_per_pattern) {
    const std::vector<std::string> patterns{"ab", "bc", "abc", "c"};
    const std::string text = "abcabcabx";

    AhoCorasick automaton(patterns);
    std::set<std::pair<std::size_t, std::string>> viaAutomaton;
    for (const auto& match : automaton.search(text)) {
        viaAutomaton.insert({match.position, match.pattern});
    }

    std::set<std::pair<std::size_t, std::string>> viaKmp;
    for (const std::string& pattern : patterns) {
        for (std::size_t position : knuthMorrisPratt(text, pattern)) {
            viaKmp.insert({position, pattern});
        }
    }
    CHECK_EQ(viaAutomaton.size(), viaKmp.size());
    CHECK_TRUE(viaAutomaton == viaKmp);
}

DAEDALUS_TEST(Strings, edit_distance) {
    CHECK_EQ(editDistance("kitten", "sitting"), 3u);
    CHECK_EQ(editDistance("", "abc"), 3u);
    CHECK_EQ(editDistance("abc", ""), 3u);
    CHECK_EQ(editDistance("same", "same"), 0u);
    CHECK_EQ(editDistance("flaw", "lawn"), 2u);
}

DAEDALUS_TEST(Strings, common_subsequence_and_substring) {
    CHECK_EQ(longestCommonSubsequence("ABCBDAB", "BDCABA"), std::string("BCBA"));
    CHECK_EQ(longestCommonSubsequence("abc", "xyz"), std::string(""));
    CHECK_EQ(longestCommonSubstring("ABABC", "BABCA"), std::string("BABC"));
    CHECK_EQ(longestCommonSubstring("abc", "def"), std::string(""));
    CHECK_EQ(longestCommonSubstring("", "abc"), std::string(""));
}

DAEDALUS_TEST(Strings, small_utilities) {
    CHECK_TRUE(areAnagrams("listen", "silent"));
    CHECK_FALSE(areAnagrams("listen", "silentt"));
    CHECK_FALSE(areAnagrams("abc", "abd"));
    CHECK_TRUE(isRotationOf("waterbottle", "erbottlewat"));
    CHECK_FALSE(isRotationOf("waterbottle", "bottlewatre"));
    CHECK_TRUE(isRotationOf("", ""));
    CHECK_EQ(longestUniqueSubstringLength("abcabcbb"), 3u);
    CHECK_EQ(longestUniqueSubstringLength("bbbbb"), 1u);
    CHECK_EQ(longestUniqueSubstringLength("pwwkew"), 3u);
    CHECK_EQ(longestUniqueSubstringLength(""), 0u);
}

// ============================================================================
//  Dynamic programming
// ============================================================================

DAEDALUS_TEST(DynamicProgramming, kadane_maximum_subarray) {
    const auto result = maximumSubarray({-2, 1, -3, 4, -1, 2, 1, -5, 4});
    CHECK_EQ(result.sum, 6);
    CHECK_EQ(result.first, 3u);
    CHECK_EQ(result.last, 6u);

    const auto allNegative = maximumSubarray({-5, -2, -8});
    CHECK_EQ(allNegative.sum, -2);   // the least-bad element, not zero
    CHECK_EQ(allNegative.first, 1u);
    CHECK_THROWS_AS(maximumSubarray({}), InvalidArgument);
}

DAEDALUS_TEST(DynamicProgramming, knapsack_01) {
    const std::vector<long long> weights{10, 20, 30};
    const std::vector<long long> values{60, 100, 120};
    const auto result = knapsack01(weights, values, 50);
    CHECK_EQ(result.value, 220);
    CHECK_EQ(result.chosenItems, (std::vector<std::size_t>{1, 2}));
    CHECK_EQ(result.usedCapacity, 50);
    CHECK_EQ(knapsack01Value(weights, values, 50), 220);
    CHECK_EQ(knapsack01Value(weights, values, 0), 0);
}

DAEDALUS_TEST(DynamicProgramming, knapsack_variants_differ_as_expected) {
    const std::vector<long long> weights{2, 3};
    const std::vector<long long> values{3, 4};
    CHECK_EQ(knapsack01Value(weights, values, 6), 7);      // one of each
    CHECK_EQ(unboundedKnapsack(weights, values, 6), 9);    // three of the first
    CHECK_THROWS_AS(knapsack01({1}, {1, 2}, 5), InvalidArgument);
}

DAEDALUS_TEST(DynamicProgramming, knapsack_matches_brute_force) {
    std::mt19937 rng(31u);
    for (int trial = 0; trial < 40; ++trial) {
        const std::size_t n = 10;
        std::vector<long long> weights(n);
        std::vector<long long> values(n);
        for (std::size_t i = 0; i < n; ++i) {
            weights[i] = 1 + static_cast<long long>(rng() % 15);
            values[i] = 1 + static_cast<long long>(rng() % 50);
        }
        const long long capacity = 30;

        long long best = 0;
        for (unsigned mask = 0; mask < (1u << n); ++mask) {
            long long weight = 0;
            long long value = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if ((mask >> i) & 1u) {
                    weight += weights[i];
                    value += values[i];
                }
            }
            if (weight <= capacity) best = std::max(best, value);
        }
        CHECK_EQ(knapsack01Value(weights, values, capacity), best);
        CHECK_EQ(knapsack01(weights, values, capacity).value, best);
    }
}

DAEDALUS_TEST(DynamicProgramming, coin_change) {
    const auto result = coinChangeMinimum({1, 3, 4}, 6);
    CHECK_TRUE(result.possible);
    CHECK_EQ(result.coinCount, 2u);                       // 3 + 3, not greedy's 4+1+1
    CHECK_EQ(result.coinsUsed, (std::vector<long long>{3, 3}));

    CHECK_FALSE(coinChangeMinimum({5, 10}, 3).possible);
    CHECK_TRUE(coinChangeMinimum({1, 2}, 0).possible);
    CHECK_EQ(coinChangeMinimum({1, 2}, 0).coinCount, 0u);
    CHECK_EQ(coinChangeWays({1, 2, 5}, 5), 4);            // 5, 2+2+1, 2+1+1+1, 1x5
    CHECK_EQ(coinChangeWays({2}, 3), 0);
}

DAEDALUS_TEST(DynamicProgramming, longest_increasing_subsequence) {
    const auto sequence = longestIncreasingSubsequence({10, 9, 2, 5, 3, 7, 101, 18});
    CHECK_EQ(sequence.size(), 4u);
    for (std::size_t i = 1; i < sequence.size(); ++i) CHECK_LT(sequence[i - 1], sequence[i]);

    CHECK_EQ(longestIncreasingSubsequence({}).size(), 0u);
    CHECK_EQ(longestIncreasingSubsequence({5}).size(), 1u);
    CHECK_EQ(longestIncreasingSubsequence({3, 2, 1}).size(), 1u);
    CHECK_EQ(longestIncreasingSubsequence({1, 2, 3}).size(), 3u);
}

DAEDALUS_TEST(DynamicProgramming, lis_matches_quadratic_reference) {
    std::mt19937 rng(808u);
    for (int trial = 0; trial < 40; ++trial) {
        std::vector<long long> values(40);
        for (long long& value : values) value = static_cast<long long>(rng() % 100);

        // O(n^2) reference.
        std::vector<std::size_t> best(values.size(), 1);
        std::size_t longest = values.empty() ? 0 : 1;
        for (std::size_t i = 1; i < values.size(); ++i) {
            for (std::size_t j = 0; j < i; ++j) {
                if (values[j] < values[i]) best[i] = std::max(best[i], best[j] + 1);
            }
            longest = std::max(longest, best[i]);
        }
        CHECK_EQ(longestIncreasingSubsequence(values).size(), longest);
    }
}

DAEDALUS_TEST(DynamicProgramming, subset_sum_and_partition) {
    CHECK_TRUE(subsetSumExists({3, 34, 4, 12, 5, 2}, 9));
    CHECK_FALSE(subsetSumExists({3, 34, 4, 12, 5, 2}, 30));
    CHECK_TRUE(subsetSumExists({1, 2}, 0));
    CHECK_FALSE(subsetSumExists({1, 2}, -1));
    CHECK_TRUE(canPartitionEqually({1, 5, 11, 5}));
    CHECK_FALSE(canPartitionEqually({1, 2, 3, 5}));
}

DAEDALUS_TEST(DynamicProgramming, matrix_chain_order) {
    // Matrices 40x20, 20x30, 30x10, 10x30.
    const auto result = matrixChainOrder({40, 20, 30, 10, 30});
    CHECK_EQ(result.multiplications, 26000);
    CHECK_FALSE(result.parenthesisation.empty());
    CHECK_TRUE(result.parenthesisation.find("M1") != std::string::npos);
    CHECK_EQ(matrixChainOrder({10, 20}).multiplications, 0);   // one matrix, no work
    CHECK_THROWS_AS(matrixChainOrder({5}), InvalidArgument);
}

DAEDALUS_TEST(DynamicProgramming, classic_one_dimensional_problems) {
    CHECK_EQ(rodCutting({1, 5, 8, 9, 10, 17, 17, 20}, 8), 22);
    CHECK_EQ(houseRobber({2, 7, 9, 3, 1}), 12);
    CHECK_EQ(houseRobber({}), 0);
    CHECK_EQ(houseRobberCircular({2, 3, 2}), 3);      // cannot take both 2s
    CHECK_EQ(houseRobberCircular({1, 2, 3, 1}), 4);
    CHECK_EQ(houseRobberCircular({5}), 5);
    CHECK_EQ(minimumPathSum({{1, 3, 1}, {1, 5, 1}, {4, 2, 1}}), 7);
    CHECK_EQ(gridPathCount(3, 7), 28);
    CHECK_EQ(gridPathCount(0, 5), 0);
}

DAEDALUS_TEST(DynamicProgramming, lcs_length_on_numeric_sequences) {
    CHECK_EQ(longestCommonSubsequenceLength({1, 2, 3, 4, 1}, {3, 4, 1, 2, 1, 3}), 3u);
    CHECK_EQ(longestCommonSubsequenceLength({}, {1, 2}), 0u);
}

// ============================================================================
//  Backtracking
// ============================================================================

DAEDALUS_TEST(Backtracking, n_queens_counts_match_the_known_sequence) {
    const std::vector<std::size_t> expected{1, 0, 0, 2, 10, 4, 40, 92, 352};
    for (std::size_t n = 1; n <= 9; ++n) {
        CHECK_EQ(solveNQueens(n, true).solutionCount, expected[n - 1]);
    }
    CHECK_EQ(solveNQueens(0).solutionCount, 0u);
}

DAEDALUS_TEST(Backtracking, n_queens_solutions_are_actually_valid) {
    const auto result = solveNQueens(8);
    CHECK_EQ(result.solutions.size(), 92u);
    CHECK_LT(0u, result.nodesExplored);

    for (const QueenPlacement& placement : result.solutions) {
        CHECK_EQ(placement.size(), 8u);
        for (std::size_t a = 0; a < placement.size(); ++a) {
            for (std::size_t b = a + 1; b < placement.size(); ++b) {
                CHECK_NE(placement[a], placement[b]);                 // same row
                const std::size_t rowGap = placement[a] > placement[b]
                                               ? placement[a] - placement[b]
                                               : placement[b] - placement[a];
                CHECK_NE(rowGap, b - a);                              // same diagonal
            }
        }
    }
    CHECK_EQ(renderQueens(result.solutions[0]).size(), 72u);   // 8 rows of 8 plus newlines
}

DAEDALUS_TEST(Backtracking, sudoku_solver) {
    SudokuGrid grid{
        {5, 3, 0, 0, 7, 0, 0, 0, 0}, {6, 0, 0, 1, 9, 5, 0, 0, 0},
        {0, 9, 8, 0, 0, 0, 0, 6, 0}, {8, 0, 0, 0, 6, 0, 0, 0, 3},
        {4, 0, 0, 8, 0, 3, 0, 0, 1}, {7, 0, 0, 0, 2, 0, 0, 0, 6},
        {0, 6, 0, 0, 0, 0, 2, 8, 0}, {0, 0, 0, 4, 1, 9, 0, 0, 5},
        {0, 0, 0, 0, 8, 0, 0, 7, 9}};

    CHECK_TRUE(solveSudoku(grid));
    CHECK_TRUE(isValidSudoku(grid));
    CHECK_EQ(grid[0][0], 5);   // the givens must be untouched
    CHECK_EQ(grid[8][8], 9);
}

DAEDALUS_TEST(Backtracking, sudoku_rejects_an_unsolvable_grid) {
    SudokuGrid grid(9, std::vector<int>(9, 0));
    grid[0][0] = 1;
    grid[0][1] = 1;   // two 1s in the same row: no solution
    CHECK_FALSE(solveSudoku(grid));

    SudokuGrid wrongShape(8, std::vector<int>(9, 0));
    CHECK_THROWS_AS(solveSudoku(wrongShape), InvalidArgument);
}

DAEDALUS_TEST(Backtracking, permutations_and_subsets) {
    const auto perms = permutations(std::vector<int>{1, 2, 3});
    CHECK_EQ(perms.size(), 6u);
    std::set<std::vector<int>> distinct(perms.begin(), perms.end());
    CHECK_EQ(distinct.size(), 6u);

    const auto duplicated = uniquePermutations(std::vector<int>{1, 1, 2});
    CHECK_EQ(duplicated.size(), 3u);   // not 6

    const auto powerSet = subsets(std::vector<int>{1, 2, 3});
    CHECK_EQ(powerSet.size(), 8u);
    CHECK_EQ(subsets(std::vector<int>{}).size(), 1u);   // just the empty set
}

DAEDALUS_TEST(Backtracking, combinations_and_target_subsets) {
    const auto pairs = combinations(4, 2);
    CHECK_EQ(pairs.size(), 6u);
    CHECK_EQ(pairs.front(), (std::vector<std::size_t>{0, 1}));
    CHECK_EQ(pairs.back(), (std::vector<std::size_t>{2, 3}));
    CHECK_EQ(combinations(3, 5).size(), 0u);
    CHECK_EQ(combinations(3, 0).size(), 1u);

    const auto summing = subsetsSummingTo({2, 3, 5, 7}, 10);
    CHECK_EQ(summing.size(), 2u);   // 3+7 and 2+3+5
}

DAEDALUS_TEST(Backtracking, maze_solving) {
    const std::vector<std::vector<int>> open{{1, 1, 0}, {0, 1, 0}, {0, 1, 1}};
    const std::string path = solveMaze(open);
    CHECK_FALSE(path.empty());

    // Replay the path and confirm it lands on the exit through open cells.
    std::size_t row = 0;
    std::size_t column = 0;
    for (char move : path) {
        if (move == 'D') ++row;
        if (move == 'U') --row;
        if (move == 'R') ++column;
        if (move == 'L') --column;
        CHECK_LT(row, 3u);
        CHECK_LT(column, 3u);
        CHECK_EQ(open[row][column], 1);
    }
    CHECK_EQ(row, 2u);
    CHECK_EQ(column, 2u);

    const std::vector<std::vector<int>> blocked{{1, 0}, {0, 1}};
    CHECK_EQ(solveMaze(blocked), std::string(""));
    CHECK_EQ(solveMaze({{0, 1}, {1, 1}}), std::string(""));   // blocked entrance
}

DAEDALUS_TEST(Backtracking, word_search) {
    const std::vector<std::string> board{"ABCE", "SFCS", "ADEE"};
    CHECK_TRUE(wordSearch(board, "ABCCED"));
    CHECK_TRUE(wordSearch(board, "SEE"));
    CHECK_FALSE(wordSearch(board, "ABCB"));   // would reuse the B
    CHECK_TRUE(wordSearch(board, ""));
}

DAEDALUS_TEST(Backtracking, graph_colouring) {
    // A 4-cycle is 2-colourable; a triangle is not.
    std::vector<std::vector<bool>> cycle(4, std::vector<bool>(4, false));
    for (std::size_t i = 0; i < 4; ++i) {
        cycle[i][(i + 1) % 4] = true;
        cycle[(i + 1) % 4][i] = true;
    }
    const auto twoColouring = graphColouring(cycle, 2);
    CHECK_EQ(twoColouring.size(), 4u);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK_NE(twoColouring[i], twoColouring[(i + 1) % 4]);
    }

    std::vector<std::vector<bool>> triangle(3, std::vector<bool>(3, true));
    for (std::size_t i = 0; i < 3; ++i) triangle[i][i] = false;
    CHECK_EQ(graphColouring(triangle, 2).size(), 0u);
    CHECK_EQ(graphColouring(triangle, 3).size(), 3u);
}

// ============================================================================
//  Greedy
// ============================================================================

DAEDALUS_TEST(Greedy, activity_selection) {
    const std::vector<Activity> activities{
        {1, 4, "a"}, {3, 5, "b"}, {0, 6, "c"}, {5, 7, "d"},
        {3, 9, "e"}, {5, 9, "f"}, {6, 10, "g"}, {8, 11, "h"},
        {8, 12, "i"}, {2, 14, "j"}, {12, 16, "k"}};

    const auto chosen = activitySelection(activities);
    CHECK_EQ(chosen.size(), 4u);
    for (std::size_t i = 1; i < chosen.size(); ++i) {
        CHECK_LE(chosen[i - 1].finish, chosen[i].start);   // no overlaps
    }
}

DAEDALUS_TEST(Greedy, fractional_knapsack_beats_the_integral_one) {
    const std::vector<double> weights{10, 20, 30};
    const std::vector<double> values{60, 100, 120};
    const auto result = fractionalKnapsack(weights, values, 50);
    CHECK_NEAR(result.value, 240.0, 1e-9);   // 220 is the best 0/1 answer

    // Greedy by density is WRONG for 0/1: here it would take item 0 then be
    // unable to improve, while the DP finds the better pair.
    CHECK_EQ(knapsack01Value({10, 20, 30}, {60, 100, 120}, 50), 220);
    CHECK_LT(220.0, result.value);
    CHECK_THROWS_AS(fractionalKnapsack({1}, {1, 2}, 5), InvalidArgument);
}

DAEDALUS_TEST(Greedy, huffman_round_trips_and_compresses) {
    const std::string text = "this is an example of a huffman tree";
    const auto result = huffmanCoding(text);

    const std::string bits = huffmanEncode(text, result.codes);
    CHECK_EQ(huffmanDecode(bits, result.codes), text);
    CHECK_EQ(bits.size(), result.encodedBits);
    CHECK_LT(result.encodedBits, result.fixedWidthBits);
    CHECK_LT(result.compressionRatio(), 1.0);

    // A more frequent symbol never gets a longer code than a rarer one.
    CHECK_LE(result.codes.at(' ').size(), result.codes.at('x').size());
}

DAEDALUS_TEST(Greedy, huffman_edge_cases) {
    CHECK_EQ(huffmanCoding("").codes.size(), 0u);
    const auto single = huffmanCoding("aaaa");
    CHECK_EQ(single.codes.size(), 1u);
    CHECK_EQ(single.codes.at('a'), std::string("0"));
    CHECK_EQ(huffmanDecode(huffmanEncode("aaaa", single.codes), single.codes),
             std::string("aaaa"));
    CHECK_THROWS_AS(huffmanEncode("z", single.codes), InvalidArgument);
    CHECK_THROWS_AS(huffmanDecode("0000001", huffmanCoding("abc").codes), InvalidArgument);
}

DAEDALUS_TEST(Greedy, job_sequencing) {
    const std::vector<Job> jobs{
        {"a", 2, 100}, {"b", 1, 19}, {"c", 2, 27}, {"d", 1, 25}, {"e", 3, 15}};
    const auto schedule = jobSequencing(jobs);
    CHECK_EQ(schedule.totalProfit, 142);   // c, a, e
    CHECK_EQ(schedule.scheduledCount, 3u);
    CHECK_EQ(schedule.sequence[1], std::string("a"));
}

DAEDALUS_TEST(Greedy, minimum_platforms) {
    CHECK_EQ(minimumPlatforms({900, 940, 950, 1100, 1500, 1800},
                              {910, 1200, 1120, 1130, 1900, 2000}),
             3u);
    CHECK_EQ(minimumPlatforms({100}, {200}), 1u);
    CHECK_THROWS_AS(minimumPlatforms({1, 2}, {3}), InvalidArgument);
}

DAEDALUS_TEST(Greedy, greedy_coin_change_is_wrong_on_a_non_canonical_system) {
    // Canonical system: greedy is optimal.
    CHECK_EQ(minimumCoinsGreedy({1, 5, 10, 25}, 30).size(), 2u);
    CHECK_EQ(coinChangeMinimum({1, 5, 10, 25}, 30).coinCount, 2u);

    // Non-canonical: greedy takes 4+1+1, the DP finds 3+3.
    const auto greedy = minimumCoinsGreedy({1, 3, 4}, 6);
    const auto optimal = coinChangeMinimum({1, 3, 4}, 6);
    CHECK_EQ(greedy.size(), 3u);
    CHECK_EQ(optimal.coinCount, 2u);
    CHECK_LT(optimal.coinCount, greedy.size());

    CHECK_EQ(minimumCoinsGreedy({5}, 3).size(), 0u);   // impossible
}

// ============================================================================
//  Number theory
// ============================================================================

DAEDALUS_TEST(NumberTheory, primality) {
    CHECK_FALSE(isPrime(0));
    CHECK_FALSE(isPrime(1));
    CHECK_TRUE(isPrime(2));
    CHECK_TRUE(isPrime(97));
    CHECK_FALSE(isPrime(561));                  // a Carmichael number
    CHECK_TRUE(isPrime(1000000007ull));
    CHECK_TRUE(isPrime(18446744073709551557ull));   // largest 64-bit prime
    CHECK_FALSE(isPrime(1000000009ull * 3));
}

DAEDALUS_TEST(NumberTheory, sieves_agree_with_each_other_and_with_isPrime) {
    const auto sieved = sieveOfEratosthenes(10000);
    const auto linear = linearSieve(10000);
    CHECK_EQ(sieved, linear.primes);
    CHECK_EQ(sieved.size(), 1229u);             // pi(10000)
    CHECK_EQ(sieved.front(), 2u);
    CHECK_EQ(sieved.back(), 9973u);

    for (std::uint64_t prime : sieved) CHECK_TRUE(isPrime(prime));
    CHECK_EQ(sieveOfEratosthenes(2).size(), 0u);
    CHECK_EQ(linear.smallestPrimeFactor[91], 7u);
}

DAEDALUS_TEST(NumberTheory, factorisation_and_divisors) {
    CHECK_EQ(primeFactors(360).size(), 3u);     // 2^3 * 3^2 * 5
    CHECK_EQ(primeFactors(360)[0].second, 3u);
    CHECK_EQ(primeFactors(97).size(), 1u);
    CHECK_EQ(divisors(28), (std::vector<std::uint64_t>{1, 2, 4, 7, 14, 28}));
    CHECK_EQ(divisors(1), (std::vector<std::uint64_t>{1}));
    CHECK_EQ(eulerTotient(9), 6u);
    CHECK_EQ(eulerTotient(1), 1u);
}

DAEDALUS_TEST(NumberTheory, gcd_lcm_and_bezout) {
    CHECK_EQ(greatestCommonDivisor(48, 18), 6);
    CHECK_EQ(greatestCommonDivisor(-48, 18), 6);
    CHECK_EQ(greatestCommonDivisor(0, 5), 5);
    CHECK_EQ(leastCommonMultiple(4, 6), 12);
    CHECK_EQ(leastCommonMultiple(0, 6), 0);

    const auto identity = extendedGcd(240, 46);
    CHECK_EQ(identity.gcd, 2);
    CHECK_EQ(240 * identity.x + 46 * identity.y, identity.gcd);
}

DAEDALUS_TEST(NumberTheory, modular_arithmetic) {
    CHECK_EQ(modularPower(2, 10, 1000), 24u);
    CHECK_EQ(modularPower(2, 0, 7), 1u);
    CHECK_EQ(modularPower(5, 3, 1), 0u);
    // Would overflow a 64-bit multiply without the 128-bit path.
    CHECK_EQ(modularMultiply(18446744073709551557ull - 1, 2, 18446744073709551557ull),
             18446744073709551555ull);

    CHECK_EQ(modularInverse(3, 11).value(), 4);       // 3 * 4 = 12 = 1 mod 11
    CHECK_FALSE(modularInverse(4, 8).has_value());    // not coprime
    CHECK_THROWS_AS(modularPower(2, 3, 0), InvalidArgument);
}

DAEDALUS_TEST(NumberTheory, chinese_remainder_theorem) {
    // x = 2 mod 3, x = 3 mod 5, x = 2 mod 7  ->  x = 23 mod 105
    const auto solution = chineseRemainder({2, 3, 2}, {3, 5, 7}).value();
    CHECK_EQ(solution.first, 23);
    CHECK_EQ(solution.second, 105);

    // Inconsistent: x = 1 mod 2 and x = 0 mod 4 cannot both hold.
    CHECK_FALSE(chineseRemainder({1, 0}, {2, 4}).has_value());
    CHECK_THROWS_AS(chineseRemainder({1}, {2, 3}), InvalidArgument);
}

DAEDALUS_TEST(NumberTheory, combinatorics_and_fibonacci) {
    CHECK_EQ(binomial(5, 2), 10);
    CHECK_EQ(binomial(10, 0), 1);
    CHECK_EQ(binomial(3, 5), 0);
    CHECK_EQ(binomial(20, 10), 184756);
    CHECK_EQ(binomialModulo(20, 10, 1000000007ull), 184756u);

    CHECK_EQ(fibonacci(0), 0u);
    CHECK_EQ(fibonacci(1), 1u);
    CHECK_EQ(fibonacci(10), 55u);
    CHECK_EQ(fibonacci(90), 2880067194370816120ull);
}

// ============================================================================
//  Divide and conquer
// ============================================================================

DAEDALUS_TEST(DivideAndConquer, inversion_counting) {
    CHECK_EQ(countInversions({1, 2, 3, 4}), 0);
    CHECK_EQ(countInversions({4, 3, 2, 1}), 6);          // n(n-1)/2
    CHECK_EQ(countInversions({2, 4, 1, 3, 5}), 3);
    CHECK_EQ(countInversions({}), 0);
    CHECK_EQ(countInversions({1}), 0);
}

DAEDALUS_TEST(DivideAndConquer, inversion_counting_matches_brute_force) {
    std::mt19937 rng(246u);
    for (int trial = 0; trial < 50; ++trial) {
        std::vector<long long> values(60);
        for (long long& value : values) value = static_cast<long long>(rng() % 30);

        long long expected = 0;
        for (std::size_t i = 0; i < values.size(); ++i) {
            for (std::size_t j = i + 1; j < values.size(); ++j) {
                if (values[i] > values[j]) ++expected;
            }
        }
        CHECK_EQ(countInversions(values), expected);
    }
}

DAEDALUS_TEST(DivideAndConquer, closest_pair) {
    const std::vector<Point> points{{2, 3}, {12, 30}, {40, 50}, {5, 1}, {12, 10}, {3, 4}};
    const auto result = closestPair(points);
    CHECK_NEAR(result.distance, std::sqrt(2.0), 1e-9);   // (2,3) and (3,4)
    CHECK_THROWS_AS(closestPair({{0, 0}}), InvalidArgument);
}

DAEDALUS_TEST(DivideAndConquer, closest_pair_matches_brute_force) {
    std::mt19937 rng(1357u);
    std::uniform_real_distribution<double> coordinate(-500.0, 500.0);

    for (int trial = 0; trial < 30; ++trial) {
        std::vector<Point> points(60);
        for (Point& point : points) point = Point{coordinate(rng), coordinate(rng)};

        double expected = std::numeric_limits<double>::max();
        for (std::size_t i = 0; i < points.size(); ++i) {
            for (std::size_t j = i + 1; j < points.size(); ++j) {
                expected = std::min(expected, euclideanDistance(points[i], points[j]));
            }
        }
        CHECK_NEAR(closestPair(points).distance, expected, 1e-9);
    }
}

DAEDALUS_TEST(DivideAndConquer, karatsuba_multiplication) {
    CHECK_EQ(karatsubaMultiply("0", "12345"), std::string("0"));
    CHECK_EQ(karatsubaMultiply("12", "12"), std::string("144"));
    CHECK_EQ(karatsubaMultiply("1234", "5678"), std::string("7006652"));
    CHECK_EQ(karatsubaMultiply("00123", "010"), std::string("1230"));
    CHECK_EQ(karatsubaMultiply("99999999999999999999", "99999999999999999999"),
             std::string("9999999999999999999800000000000000000001"));
    CHECK_THROWS_AS(karatsubaMultiply("12a", "3"), InvalidArgument);
}

DAEDALUS_TEST(DivideAndConquer, karatsuba_matches_long_multiplication) {
    std::mt19937 rng(97531u);
    for (int trial = 0; trial < 40; ++trial) {
        const long long a = static_cast<long long>(rng() % 100000);
        const long long b = static_cast<long long>(rng() % 100000);
        CHECK_EQ(karatsubaMultiply(std::to_string(a), std::to_string(b)),
                 std::to_string(a * b));
    }
}

DAEDALUS_TEST(DivideAndConquer, selection_algorithms) {
    const std::vector<int> data{7, 10, 4, 3, 20, 15};
    for (std::size_t k = 0; k < data.size(); ++k) {
        std::vector<int> expected = data;
        std::sort(expected.begin(), expected.end());
        CHECK_EQ(quickselect(data, k), expected[k]);
        CHECK_EQ(medianOfMedians(data, k), expected[k]);
    }
    CHECK_EQ(median(data), 7);
    CHECK_THROWS_AS(quickselect(data, 6), InvalidArgument);
    CHECK_THROWS_AS(median(std::vector<int>{}), InvalidArgument);
}

DAEDALUS_TEST(DivideAndConquer, selection_matches_a_full_sort) {
    std::mt19937 rng(8642u);
    for (int trial = 0; trial < 40; ++trial) {
        std::vector<int> data = randomInts(80, static_cast<std::uint32_t>(trial), -50, 50);
        std::vector<int> expected = data;
        std::sort(expected.begin(), expected.end());
        const std::size_t k = rng() % data.size();
        CHECK_EQ(quickselect(data, k), expected[k]);
        CHECK_EQ(medianOfMedians(data, k), expected[k]);
    }
}

DAEDALUS_TEST(DivideAndConquer, majority_element) {
    CHECK_EQ(majorityElement(std::vector<int>{3, 2, 3}).value(), 3);
    CHECK_EQ(majorityElement(std::vector<int>{2, 2, 1, 1, 1, 2, 2}).value(), 2);
    CHECK_FALSE(majorityElement(std::vector<int>{1, 2, 3}).has_value());
    CHECK_FALSE(majorityElement(std::vector<int>{1, 2}).has_value());   // exactly half
    CHECK_FALSE(majorityElement(std::vector<int>{}).has_value());
    CHECK_EQ(majorityElement(std::vector<std::string>{"a", "a", "b"}).value(),
             std::string("a"));
}
