// ============================================================================
//  Unit tests for heaps, range structures, tries, B-trees and general binary
//  trees.
// ============================================================================
#include <algorithm>
#include <numeric>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "daedalus/trees/BTree.hpp"
#include "daedalus/trees/BinaryHeap.hpp"
#include "daedalus/trees/BinaryTree.hpp"
#include "daedalus/trees/FenwickTree.hpp"
#include "daedalus/trees/SegmentTree.hpp"
#include "daedalus/trees/Trie.hpp"
#include "framework/TestFramework.hpp"

using namespace daedalus;

namespace {

std::vector<int> shuffledRange(int count, std::uint32_t seed) {
    std::vector<int> values(static_cast<std::size_t>(count));
    std::iota(values.begin(), values.end(), 0);
    std::mt19937 rng(seed);
    std::shuffle(values.begin(), values.end(), rng);
    return values;
}

}   // namespace

// ============================================================================
//  BinaryHeap
// ============================================================================

DAEDALUS_TEST(BinaryHeap, max_heap_by_default) {
    BinaryHeap<int> heap;
    for (int value : {3, 1, 4, 1, 5, 9, 2, 6}) heap.push(value);
    CHECK_EQ(heap.top(), 9);
    CHECK_EQ(heap.pop(), 9);
    CHECK_EQ(heap.pop(), 6);
    CHECK_EQ(heap.pop(), 5);
    CHECK_EQ(heap.size(), 5u);
    CHECK_TRUE(heap.isValidHeap());
}

DAEDALUS_TEST(BinaryHeap, min_heap_alias) {
    MinHeap<int> heap;
    for (int value : {3, 1, 4, 1, 5}) heap.push(value);
    CHECK_EQ(heap.pop(), 1);
    CHECK_EQ(heap.pop(), 1);
    CHECK_EQ(heap.pop(), 3);
    CHECK_TRUE(heap.isValidHeap());
}

DAEDALUS_TEST(BinaryHeap, floyd_build_is_a_valid_heap) {
    const std::vector<int> values = shuffledRange(1000, 5u);
    BinaryHeap<int> heap(values);
    CHECK_EQ(heap.size(), 1000u);
    CHECK_TRUE(heap.isValidHeap());
    CHECK_EQ(heap.top(), 999);

    std::vector<int> drained = heap.drainSorted();
    CHECK_EQ(drained.front(), 999);
    CHECK_EQ(drained.back(), 0);
    CHECK_TRUE(std::is_sorted(drained.rbegin(), drained.rend()));
    CHECK_EQ(heap.size(), 1000u);   // drainSorted must not consume the heap
}

DAEDALUS_TEST(BinaryHeap, pop_order_matches_a_full_sort) {
    BinaryHeap<int> heap(shuffledRange(500, 17u));
    std::vector<int> popped;
    while (!heap.empty()) popped.push_back(heap.pop());
    std::vector<int> expected(500);
    std::iota(expected.rbegin(), expected.rend(), 0);
    CHECK_EQ(popped, expected);
}

DAEDALUS_TEST(BinaryHeap, replace_top_and_erase) {
    BinaryHeap<int> heap{5, 3, 8, 1};
    CHECK_EQ(heap.replaceTop(0), 8);
    CHECK_EQ(heap.top(), 5);
    CHECK_TRUE(heap.isValidHeap());
    CHECK_TRUE(heap.erase(3));
    CHECK_FALSE(heap.erase(42));
    CHECK_TRUE(heap.isValidHeap());
    CHECK_EQ(heap.size(), 3u);
}

DAEDALUS_TEST(BinaryHeap, empty_operations_throw) {
    BinaryHeap<int> heap;
    CHECK_THROWS_AS(heap.top(), EmptyContainer);
    CHECK_THROWS_AS(heap.pop(), EmptyContainer);
    CHECK_THROWS_AS(heap.replaceTop(1), EmptyContainer);
}

DAEDALUS_TEST(PriorityQueue, lowest_priority_value_comes_out_first) {
    PriorityQueue<std::string, int> queue;
    queue.push("compact", 30);
    queue.push("flush", 10);
    queue.push("checkpoint", 20);
    CHECK_EQ(queue.peek(), std::string("flush"));
    CHECK_EQ(queue.peekPriority(), 10);
    CHECK_EQ(queue.pop(), std::string("flush"));
    CHECK_EQ(queue.pop(), std::string("checkpoint"));
    CHECK_EQ(queue.pop(), std::string("compact"));
    CHECK_TRUE(queue.empty());
}

DAEDALUS_TEST(PriorityQueue, ties_keep_insertion_order) {
    PriorityQueue<std::string, int> queue;
    queue.push("first", 5);
    queue.push("second", 5);
    queue.push("third", 5);
    CHECK_EQ(queue.pop(), std::string("first"));
    CHECK_EQ(queue.pop(), std::string("second"));
    CHECK_EQ(queue.pop(), std::string("third"));
}

// ============================================================================
//  SegmentTree
// ============================================================================

DAEDALUS_TEST(SegmentTree, sum_queries_and_point_updates) {
    SegmentTree<int> tree(std::vector<int>{1, 3, 5, 7, 9, 11});
    CHECK_EQ(tree.query(0, 5), 36);
    CHECK_EQ(tree.query(1, 3), 15);
    CHECK_EQ(tree.query(2, 2), 5);
    tree.update(2, 50);
    CHECK_EQ(tree.query(1, 3), 60);
    CHECK_EQ(tree.queryAll(), 81);
}

DAEDALUS_TEST(SegmentTree, min_and_max_policies) {
    const std::vector<int> values{5, 2, 8, 1, 9, 3};
    SegmentTree<int, MinPolicy<int>> minimum(values);
    SegmentTree<int, MaxPolicy<int>> maximum(values);
    CHECK_EQ(minimum.query(0, 5), 1);
    CHECK_EQ(minimum.query(0, 2), 2);
    CHECK_EQ(maximum.query(0, 5), 9);
    CHECK_EQ(maximum.query(0, 2), 8);
    minimum.update(3, 100);
    CHECK_EQ(minimum.query(0, 5), 2);
}

DAEDALUS_TEST(SegmentTree, matches_brute_force_under_random_load) {
    std::mt19937 rng(999u);
    std::uniform_int_distribution<int> values(-50, 50);
    std::vector<int> array(200);
    for (int& value : array) value = values(rng);

    SegmentTree<int> tree(array);
    std::uniform_int_distribution<std::size_t> index(0, array.size() - 1);
    for (int step = 0; step < 500; ++step) {
        if (step % 3 == 0) {
            const std::size_t position = index(rng);
            const int value = values(rng);
            array[position] = value;
            tree.update(position, value);
        }
        std::size_t left = index(rng);
        std::size_t right = index(rng);
        if (left > right) std::swap(left, right);
        const int expected =
            std::accumulate(array.begin() + static_cast<std::ptrdiff_t>(left),
                            array.begin() + static_cast<std::ptrdiff_t>(right) + 1, 0);
        CHECK_EQ(tree.query(left, right), expected);
    }
}

DAEDALUS_TEST(SegmentTree, rejects_bad_ranges) {
    SegmentTree<int> tree(std::vector<int>{1, 2, 3});
    CHECK_THROWS_AS(tree.query(2, 1), InvalidArgument);
    CHECK_THROWS_AS(tree.query(0, 3), IndexOutOfRange);
    CHECK_THROWS_AS(tree.update(3, 0), IndexOutOfRange);
    SegmentTree<int> empty;
    CHECK_EQ(empty.queryAll(), 0);
}

DAEDALUS_TEST(SegmentTree, lazy_range_add_then_query) {
    LazySegmentTree<int> tree(std::vector<int>{1, 2, 3, 4, 5});
    CHECK_EQ(tree.query(0, 4), 15);
    tree.rangeAdd(1, 3, 10);   // -> 1, 12, 13, 14, 5
    CHECK_EQ(tree.query(0, 4), 45);
    CHECK_EQ(tree.query(1, 3), 39);
    CHECK_EQ(tree.at(0), 1);
    CHECK_EQ(tree.at(2), 13);
    tree.rangeAdd(0, 4, -1);
    CHECK_EQ(tree.query(0, 4), 40);
}

DAEDALUS_TEST(SegmentTree, lazy_matches_brute_force_under_random_load) {
    std::mt19937 rng(2468u);
    std::vector<long long> array(120, 0);
    LazySegmentTree<long long> tree(array);
    std::uniform_int_distribution<std::size_t> index(0, array.size() - 1);
    std::uniform_int_distribution<int> deltas(-20, 20);

    for (int step = 0; step < 400; ++step) {
        std::size_t left = index(rng);
        std::size_t right = index(rng);
        if (left > right) std::swap(left, right);

        if (step % 2 == 0) {
            const long long delta = deltas(rng);
            for (std::size_t i = left; i <= right; ++i) array[i] += delta;
            tree.rangeAdd(left, right, delta);
        } else {
            const long long expected =
                std::accumulate(array.begin() + static_cast<std::ptrdiff_t>(left),
                                array.begin() + static_cast<std::ptrdiff_t>(right) + 1, 0LL);
            CHECK_EQ(tree.query(left, right), expected);
        }
    }
}

DAEDALUS_TEST(SegmentTree, lazy_min_policy) {
    LazySegmentTree<int, MinPolicy<int>> tree(std::vector<int>{5, 3, 8, 1, 9});
    CHECK_EQ(tree.query(0, 4), 1);
    tree.rangeAdd(3, 3, 100);
    CHECK_EQ(tree.query(0, 4), 3);
    CHECK_EQ(tree.query(2, 4), 8);
}

// ============================================================================
//  FenwickTree
// ============================================================================

DAEDALUS_TEST(FenwickTree, prefix_and_range_sums) {
    FenwickTree<long long> tree(std::vector<long long>{1, 2, 3, 4, 5});
    CHECK_EQ(tree.prefixSum(0), 1);
    CHECK_EQ(tree.prefixSum(4), 15);
    CHECK_EQ(tree.rangeSum(1, 3), 9);
    CHECK_EQ(tree.at(2), 3);
    tree.add(2, 10);
    CHECK_EQ(tree.rangeSum(1, 3), 19);
    tree.set(0, 100);
    CHECK_EQ(tree.at(0), 100);
    CHECK_EQ(tree.prefixSum(4), 124);
}

DAEDALUS_TEST(FenwickTree, matches_brute_force_under_random_load) {
    std::mt19937 rng(13579u);
    std::vector<long long> array(300, 0);
    FenwickTree<long long> tree(array.size());
    std::uniform_int_distribution<std::size_t> index(0, array.size() - 1);
    std::uniform_int_distribution<int> deltas(-100, 100);

    for (int step = 0; step < 1000; ++step) {
        if (step % 2 == 0) {
            const std::size_t position = index(rng);
            const long long delta = deltas(rng);
            array[position] += delta;
            tree.add(position, delta);
        } else {
            std::size_t left = index(rng);
            std::size_t right = index(rng);
            if (left > right) std::swap(left, right);
            const long long expected =
                std::accumulate(array.begin() + static_cast<std::ptrdiff_t>(left),
                                array.begin() + static_cast<std::ptrdiff_t>(right) + 1, 0LL);
            CHECK_EQ(tree.rangeSum(left, right), expected);
        }
    }
}

DAEDALUS_TEST(FenwickTree, lower_bound_finds_the_first_reaching_prefix) {
    // Frequencies 1..5 give prefix sums 1, 3, 6, 10, 15.
    FenwickTree<long long> tree(std::vector<long long>{1, 2, 3, 4, 5});
    CHECK_EQ(tree.lowerBound(1), 0u);
    CHECK_EQ(tree.lowerBound(2), 1u);
    CHECK_EQ(tree.lowerBound(3), 1u);
    CHECK_EQ(tree.lowerBound(4), 2u);
    CHECK_EQ(tree.lowerBound(15), 4u);
    CHECK_EQ(tree.lowerBound(16), 5u);   // no prefix reaches it
}

DAEDALUS_TEST(FenwickTree, rejects_bad_indices) {
    FenwickTree<long long> tree(3);
    CHECK_THROWS_AS(tree.add(3, 1), IndexOutOfRange);
    CHECK_THROWS_AS(tree.prefixSum(3), IndexOutOfRange);
    CHECK_THROWS_AS(tree.rangeSum(2, 1), InvalidArgument);
}

DAEDALUS_TEST(FenwickTree, range_update_range_query) {
    RangeFenwickTree<long long> tree(6);
    tree.rangeAdd(1, 3, 5);   // -> 0, 5, 5, 5, 0, 0
    CHECK_EQ(tree.at(0), 0);
    CHECK_EQ(tree.at(1), 5);
    CHECK_EQ(tree.at(3), 5);
    CHECK_EQ(tree.at(4), 0);
    CHECK_EQ(tree.rangeSum(0, 5), 15);
    tree.rangeAdd(0, 5, 2);   // -> 2, 7, 7, 7, 2, 2
    CHECK_EQ(tree.rangeSum(0, 5), 27);
    CHECK_EQ(tree.rangeSum(2, 4), 16);
}

DAEDALUS_TEST(FenwickTree, range_variant_matches_brute_force) {
    std::mt19937 rng(86420u);
    std::vector<long long> array(150, 0);
    RangeFenwickTree<long long> tree(array.size());
    std::uniform_int_distribution<std::size_t> index(0, array.size() - 1);
    std::uniform_int_distribution<int> deltas(-30, 30);

    for (int step = 0; step < 400; ++step) {
        std::size_t left = index(rng);
        std::size_t right = index(rng);
        if (left > right) std::swap(left, right);
        if (step % 2 == 0) {
            const long long delta = deltas(rng);
            for (std::size_t i = left; i <= right; ++i) array[i] += delta;
            tree.rangeAdd(left, right, delta);
        } else {
            const long long expected =
                std::accumulate(array.begin() + static_cast<std::ptrdiff_t>(left),
                                array.begin() + static_cast<std::ptrdiff_t>(right) + 1, 0LL);
            CHECK_EQ(tree.rangeSum(left, right), expected);
        }
    }
}

// ============================================================================
//  Trie
// ============================================================================

DAEDALUS_TEST(Trie, insert_contains_and_prefix) {
    Trie trie{"car", "card", "care", "dog"};
    CHECK_EQ(trie.size(), 4u);
    CHECK_TRUE(trie.contains("car"));
    CHECK_TRUE(trie.contains("card"));
    CHECK_FALSE(trie.contains("ca"));
    CHECK_TRUE(trie.startsWith("ca"));
    CHECK_FALSE(trie.startsWith("cat"));
    CHECK_EQ(trie.countWithPrefix("car"), 3u);
    CHECK_EQ(trie.countWithPrefix("d"), 1u);
    CHECK_EQ(trie.countWithPrefix("zzz"), 0u);
}

DAEDALUS_TEST(Trie, completions_are_sorted) {
    Trie trie{"care", "car", "card", "cat", "dog"};
    CHECK_EQ(trie.withPrefix("car"), (std::vector<std::string>{"car", "card", "care"}));
    CHECK_EQ(trie.withPrefix("ca"), (std::vector<std::string>{"car", "card", "care", "cat"}));
    CHECK_EQ(trie.words(), (std::vector<std::string>{"car", "card", "care", "cat", "dog"}));
    CHECK_EQ(trie.withPrefix("zebra"), (std::vector<std::string>{}));
}

DAEDALUS_TEST(Trie, erase_prunes_dead_branches) {
    Trie trie{"car", "card"};
    const std::size_t before = trie.nodeCount();
    CHECK_TRUE(trie.erase("card"));
    CHECK_LT(trie.nodeCount(), before);   // the 'd' node is gone
    CHECK_TRUE(trie.contains("car"));     // the shared prefix survives
    CHECK_FALSE(trie.contains("card"));
    CHECK_EQ(trie.countWithPrefix("car"), 1u);
    CHECK_FALSE(trie.erase("card"));
    CHECK_TRUE(trie.erase("car"));
    CHECK_TRUE(trie.empty());
    CHECK_EQ(trie.nodeCount(), 1u);   // only the root remains
}

DAEDALUS_TEST(Trie, duplicate_insert_is_a_no_op) {
    Trie trie;
    trie.insert("hello");
    trie.insert("hello");
    CHECK_EQ(trie.size(), 1u);
    CHECK_EQ(trie.countWithPrefix("hello"), 1u);
}

DAEDALUS_TEST(Trie, empty_string_is_a_valid_key) {
    Trie trie;
    trie.insert("");
    CHECK_TRUE(trie.contains(""));
    CHECK_EQ(trie.size(), 1u);
    CHECK_TRUE(trie.erase(""));
    CHECK_FALSE(trie.contains(""));
}

DAEDALUS_TEST(Trie, longest_common_prefix) {
    CHECK_EQ(Trie({"flower", "flow", "flight"}).longestCommonPrefix(), std::string("fl"));
    CHECK_EQ(Trie({"dog", "racecar"}).longestCommonPrefix(), std::string(""));
    CHECK_EQ(Trie({"interspecies"}).longestCommonPrefix(), std::string("interspecies"));
    CHECK_EQ(Trie().longestCommonPrefix(), std::string(""));
}

DAEDALUS_TEST(Trie, longest_prefix_of_text) {
    Trie trie{"a", "ab", "abcd"};
    CHECK_EQ(trie.longestPrefixOf("abcdef"), std::string("abcd"));
    CHECK_EQ(trie.longestPrefixOf("abz"), std::string("ab"));
    CHECK_EQ(trie.longestPrefixOf("zzz"), std::string(""));
}

DAEDALUS_TEST(Trie, copy_is_independent) {
    Trie original{"one", "two"};
    Trie copy = original;
    copy.insert("three");
    CHECK_EQ(original.size(), 2u);
    CHECK_EQ(copy.size(), 3u);
    CHECK_FALSE(original.contains("three"));
}

// ============================================================================
//  BTree
// ============================================================================

DAEDALUS_TEST(BTree, stays_valid_through_sorted_insertion) {
    BTree<int> tree(3);
    for (int i = 0; i < 1000; ++i) tree.insert(i);
    CHECK_EQ(tree.size(), 1000u);
    CHECK_TRUE(tree.verifyProperties());
    CHECK_EQ(tree.minimum().value(), 0);
    CHECK_EQ(tree.maximum().value(), 999);
    // Fanout keeps it flat: t = 3 gives at least 3 keys per level of branching.
    CHECK_LE(tree.height(), 8);
}

DAEDALUS_TEST(BTree, in_order_traversal_is_sorted) {
    BTree<int> tree(2);
    for (int key : shuffledRange(300, 42u)) tree.insert(key);
    std::vector<int> expected(300);
    std::iota(expected.begin(), expected.end(), 0);
    CHECK_EQ(tree.toVector(), expected);
    CHECK_TRUE(tree.verifyProperties());
}

DAEDALUS_TEST(BTree, deletion_covers_borrow_and_merge) {
    BTree<int> tree(2);   // minimal degree makes underflow happen constantly
    for (int i = 0; i < 200; ++i) tree.insert(i);
    for (int key : shuffledRange(200, 4242u)) {
        CHECK_TRUE(tree.erase(key));
        CHECK_TRUE(tree.verifyProperties());
    }
    CHECK_TRUE(tree.empty());
    CHECK_EQ(tree.height(), -1);
}

DAEDALUS_TEST(BTree, matches_std_set_under_random_load) {
    BTree<int> tree(4);
    std::set<int> reference;
    std::mt19937 rng(24680u);
    std::uniform_int_distribution<int> keys(0, 300);

    for (int step = 0; step < 3000; ++step) {
        const int key = keys(rng);
        if ((rng() & 3u) != 0u) {
            tree.insert(key);
            reference.insert(key);
        } else {
            CHECK_EQ(tree.erase(key), reference.erase(key) > 0);
        }
        if (step % 250 == 0) {
            CHECK_TRUE(tree.verifyProperties());
            CHECK_EQ(tree.size(), reference.size());
        }
    }
    CHECK_TRUE(tree.verifyProperties());
    CHECK_EQ(tree.toVector(), (std::vector<int>(reference.begin(), reference.end())));
}

DAEDALUS_TEST(BTree, duplicates_ignored_and_bad_degree_rejected) {
    BTree<int> tree(3);
    tree.insert(1);
    tree.insert(1);
    CHECK_EQ(tree.size(), 1u);
    CHECK_FALSE(tree.erase(99));
    CHECK_THROWS_AS(BTree<int>(1), InvalidArgument);
}

DAEDALUS_TEST(BTree, higher_degree_means_fewer_nodes) {
    BTree<int> narrow(2);
    BTree<int> wide(16);
    for (int i = 0; i < 2000; ++i) {
        narrow.insert(i);
        wide.insert(i);
    }
    CHECK_LT(wide.nodeCount(), narrow.nodeCount());
    CHECK_LT(wide.height(), narrow.height());
    CHECK_TRUE(wide.verifyProperties());
}

// ============================================================================
//  General BinaryTree
// ============================================================================

DAEDALUS_TEST(BinaryTree, builds_from_level_order_with_gaps) {
    // 1 -> (2, 3); 2 -> (nothing, 4)
    auto tree = BinaryTree<int>::fromLevelOrder({1, 2, 3, std::nullopt, 4});
    CHECK_EQ(tree.size(), 4u);
    CHECK_EQ(tree.preOrder(), (std::vector<int>{1, 2, 4, 3}));
    CHECK_EQ(tree.inOrder(), (std::vector<int>{2, 4, 1, 3}));
    CHECK_EQ(tree.postOrder(), (std::vector<int>{4, 2, 3, 1}));
    CHECK_EQ(tree.height(), 2);
}

DAEDALUS_TEST(BinaryTree, levels_zigzag_and_right_side_view) {
    auto tree = BinaryTree<int>::fromLevelOrder({1, 2, 3, 4, 5, 6, 7});
    CHECK_EQ(tree.levels().size(), 3u);
    CHECK_EQ(tree.levels()[2], (std::vector<int>{4, 5, 6, 7}));
    CHECK_EQ(tree.zigzagLevels()[1], (std::vector<int>{3, 2}));
    CHECK_EQ(tree.rightSideView(), (std::vector<int>{1, 3, 7}));
}

DAEDALUS_TEST(BinaryTree, invert_mirrors_the_tree) {
    auto tree = BinaryTree<int>::fromLevelOrder({4, 2, 7, 1, 3, 6, 9});
    tree.invert();
    CHECK_EQ(tree.levels()[1], (std::vector<int>{7, 2}));
    CHECK_EQ(tree.inOrder(), (std::vector<int>{9, 7, 6, 4, 3, 2, 1}));
}

DAEDALUS_TEST(BinaryTree, symmetry) {
    CHECK_TRUE(BinaryTree<int>::fromLevelOrder({1, 2, 2, 3, 4, 4, 3}).isSymmetric());
    CHECK_FALSE(
        BinaryTree<int>::fromLevelOrder({1, 2, 2, std::nullopt, 3, std::nullopt, 3}).isSymmetric());
    CHECK_TRUE(BinaryTree<int>().isSymmetric());
}

DAEDALUS_TEST(BinaryTree, path_algorithms) {
    /*        5
     *      /   \
     *     4     8
     *    /     / \
     *   11    13  4
     */
    auto tree = BinaryTree<int>::fromLevelOrder({5, 4, 8, 11, std::nullopt, 13, 4});
    const auto paths = tree.rootToLeafPaths();
    CHECK_EQ(paths.size(), 3u);
    CHECK_EQ(paths[0], (std::vector<int>{5, 4, 11}));
    CHECK_TRUE(tree.hasPathSum(20));   // 5 + 4 + 11
    CHECK_TRUE(tree.hasPathSum(26));   // 5 + 8 + 13
    CHECK_FALSE(tree.hasPathSum(99));
    CHECK_EQ(tree.maxRootToLeafSum().value(), 26);
    CHECK_FALSE(BinaryTree<int>().maxRootToLeafSum().has_value());
}

DAEDALUS_TEST(BinaryTree, general_lowest_common_ancestor) {
    auto tree = BinaryTree<int>::fromLevelOrder({3, 5, 1, 6, 2, 0, 8});
    CHECK_EQ(tree.lowestCommonAncestor(5, 1).value(), 3);
    CHECK_EQ(tree.lowestCommonAncestor(6, 2).value(), 5);
    CHECK_EQ(tree.lowestCommonAncestor(5, 6).value(), 5);   // an ancestor of itself
    CHECK_FALSE(tree.lowestCommonAncestor(5, 99).has_value());
}

DAEDALUS_TEST(BinaryTree, depth_and_shape_queries) {
    auto tree = BinaryTree<int>::fromLevelOrder({1, 2, 3, 4});
    CHECK_EQ(tree.depthOf(1).value(), 0);
    CHECK_EQ(tree.depthOf(4).value(), 2);
    CHECK_FALSE(tree.depthOf(99).has_value());
    CHECK_EQ(tree.countLeaves(), 2u);
    CHECK_TRUE(tree.isBalanced());
    CHECK_TRUE(tree.contains(3));
}

DAEDALUS_TEST(BinaryTree, reconstruction_from_two_traversals) {
    const std::vector<int> preorder{3, 9, 20, 15, 7};
    const std::vector<int> inorder{9, 3, 15, 20, 7};
    auto tree = BinaryTree<int>::fromPreorderAndInorder(preorder, inorder);
    CHECK_EQ(tree.preOrder(), preorder);
    CHECK_EQ(tree.inOrder(), inorder);
    CHECK_EQ(tree.levels()[1], (std::vector<int>{9, 20}));
    CHECK_THROWS_AS(BinaryTree<int>::fromPreorderAndInorder({1, 2}, {1}), InvalidArgument);
}

DAEDALUS_TEST(BinaryTree, serialisation_captures_shape) {
    auto skewedLeft = BinaryTree<int>::fromLevelOrder({1, 2, std::nullopt});
    auto skewedRight = BinaryTree<int>::fromLevelOrder({1, std::nullopt, 2});
    CHECK_NE(skewedLeft.serialize(), skewedRight.serialize());
    CHECK_EQ(skewedLeft.serialize(), std::string("1,2,#,#,#"));
    CHECK_EQ(BinaryTree<int>().serialize(), std::string("#"));
}

DAEDALUS_TEST(BinaryTree, identity_and_subtree_search) {
    auto tree = BinaryTree<int>::fromLevelOrder({3, 4, 5, 1, 2});
    auto part = BinaryTree<int>::fromLevelOrder({4, 1, 2});
    auto other = BinaryTree<int>::fromLevelOrder({3, 4, 5, 1, 2});
    CHECK_TRUE(tree.isSameAs(other));
    CHECK_TRUE(tree.containsSubtree(part));
    CHECK_FALSE(part.containsSubtree(tree));
    CHECK_FALSE(tree.isSameAs(part));
}

DAEDALUS_TEST(BinaryTree, copy_is_deep) {
    auto original = BinaryTree<int>::fromLevelOrder({1, 2, 3});
    BinaryTree<int> copy = original;
    copy.invert();
    CHECK_EQ(original.levels()[1], (std::vector<int>{2, 3}));
    CHECK_EQ(copy.levels()[1], (std::vector<int>{3, 2}));
}
