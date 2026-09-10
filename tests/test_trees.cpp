// ============================================================================
//  Unit tests for the search-tree family: BST, AVL, red-black, splay, treap.
//
//  Every balanced tree is checked the same way -- a randomised insert/erase
//  workload, with the structure's own invariant asserted after each batch and
//  the contents compared against std::set as the reference model.
// ============================================================================
#include <algorithm>
#include <memory>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "daedalus/trees/AVLTree.hpp"
#include "daedalus/trees/BinarySearchTree.hpp"
#include "daedalus/trees/RedBlackTree.hpp"
#include "daedalus/trees/SplayTree.hpp"
#include "daedalus/trees/Treap.hpp"
#include "framework/TestFramework.hpp"

using namespace daedalus;

namespace {

std::vector<int> shuffled(int count, std::uint32_t seed) {
    std::vector<int> values(static_cast<std::size_t>(count));
    std::iota(values.begin(), values.end(), 0);
    std::mt19937 rng(seed);
    std::shuffle(values.begin(), values.end(), rng);
    return values;
}

/// Insert/erase workload shared by every tree, comparing against std::set.
template <typename Tree, typename Invariant>
void exerciseAgainstStdSet(Tree& tree, Invariant invariant, std::uint32_t seed) {
    std::set<int> reference;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> keys(0, 400);

    for (int step = 0; step < 3000; ++step) {
        const int key = keys(rng);
        if ((rng() & 3u) != 0u) {
            tree.insert(key);
            reference.insert(key);
        } else {
            const bool removedFromTree = tree.erase(key);
            const bool removedFromSet = reference.erase(key) > 0;
            CHECK_EQ(removedFromTree, removedFromSet);
        }

        if (step % 200 == 0) {
            CHECK_TRUE(tree.isValidBST());
            CHECK_TRUE(invariant(tree));
            CHECK_EQ(tree.size(), reference.size());
        }
    }

    CHECK_TRUE(tree.isValidBST());
    CHECK_TRUE(invariant(tree));
    CHECK_EQ(tree.toVector(), (std::vector<int>(reference.begin(), reference.end())));
}

}  // namespace

// ============================================================================
//  BinarySearchTree
// ============================================================================

DAEDALUS_TEST(BinarySearchTree, insert_keeps_in_order_traversal_sorted) {
    BinarySearchTree<int> tree{50, 30, 70, 20, 40, 60, 80};
    CHECK_EQ(tree.toVector(), (std::vector<int>{20, 30, 40, 50, 60, 70, 80}));
    CHECK_EQ(tree.size(), 7u);
    CHECK_TRUE(tree.isValidBST());
}

DAEDALUS_TEST(BinarySearchTree, duplicates_are_ignored) {
    BinarySearchTree<int> tree{5, 5, 5, 3};
    CHECK_EQ(tree.size(), 2u);
    CHECK_EQ(tree.toVector(), (std::vector<int>{3, 5}));
}

DAEDALUS_TEST(BinarySearchTree, erase_covers_all_three_cases) {
    BinarySearchTree<int> tree{50, 30, 70, 20, 40, 60, 80, 75};
    CHECK_TRUE(tree.erase(20));                       // leaf
    CHECK_EQ(tree.toVector(), (std::vector<int>{30, 40, 50, 60, 70, 75, 80}));
    CHECK_TRUE(tree.erase(30));                       // one child
    CHECK_EQ(tree.toVector(), (std::vector<int>{40, 50, 60, 70, 75, 80}));
    CHECK_TRUE(tree.erase(70));                       // two children
    CHECK_EQ(tree.toVector(), (std::vector<int>{40, 50, 60, 75, 80}));
    CHECK_TRUE(tree.erase(50));                       // the root
    CHECK_EQ(tree.toVector(), (std::vector<int>{40, 60, 75, 80}));
    CHECK_FALSE(tree.erase(999));
    CHECK_TRUE(tree.isValidBST());
}

DAEDALUS_TEST(BinarySearchTree, all_four_traversals) {
    /*          4
     *        /   \
     *       2     6
     *      / \   / \
     *     1   3 5   7
     */
    BinarySearchTree<int> tree{4, 2, 6, 1, 3, 5, 7};
    CHECK_EQ(tree.preOrder(), (std::vector<int>{4, 2, 1, 3, 6, 5, 7}));
    CHECK_EQ(tree.inOrder(), (std::vector<int>{1, 2, 3, 4, 5, 6, 7}));
    CHECK_EQ(tree.postOrder(), (std::vector<int>{1, 3, 2, 5, 7, 6, 4}));
    CHECK_EQ(tree.levelOrder(), (std::vector<int>{4, 2, 6, 1, 3, 5, 7}));
    CHECK_EQ(tree.traversal(TraversalOrder::PreOrder), tree.preOrder());
}

DAEDALUS_TEST(BinarySearchTree, morris_traversal_matches_and_restores_the_tree) {
    BinarySearchTree<int> tree{4, 2, 6, 1, 3, 5, 7};
    CHECK_EQ(tree.morrisInOrder(), tree.inOrder());
    // The threading must be undone: a second walk has to agree with the first.
    CHECK_EQ(tree.morrisInOrder(), (std::vector<int>{1, 2, 3, 4, 5, 6, 7}));
    CHECK_TRUE(tree.isValidBST());
}

DAEDALUS_TEST(BinarySearchTree, levels_are_grouped_by_depth) {
    BinarySearchTree<int> tree{4, 2, 6, 1, 3, 5, 7};
    const auto levels = tree.levels();
    CHECK_EQ(levels.size(), 3u);
    CHECK_EQ(levels[0], (std::vector<int>{4}));
    CHECK_EQ(levels[1], (std::vector<int>{2, 6}));
    CHECK_EQ(levels[2], (std::vector<int>{1, 3, 5, 7}));
}

DAEDALUS_TEST(BinarySearchTree, visitor_receives_values_with_depth) {
    BinarySearchTree<int> tree{4, 2, 6};
    CollectingVisitor<int> visitor;
    tree.accept(visitor, TraversalOrder::InOrder);
    CHECK_EQ(visitor.values, (std::vector<int>{2, 4, 6}));
    CHECK_EQ(visitor.depths, (std::vector<int>{1, 0, 1}));

    int sum = 0;
    FunctionVisitor<int> adder([&sum](const int& value, int) { sum += value; });
    tree.accept(adder, TraversalOrder::LevelOrder);
    CHECK_EQ(sum, 12);
}

DAEDALUS_TEST(BinarySearchTree, degenerates_on_sorted_input) {
    BinarySearchTree<int> tree;
    for (int i = 0; i < 64; ++i) tree.insert(i);
    CHECK_EQ(tree.height(), 63);        // a linked list, not a tree
    CHECK_FALSE(tree.isBalanced());

    tree.rebalance();
    CHECK_EQ(tree.height(), 6);         // ceil(log2(64)) - 1
    CHECK_TRUE(tree.isBalanced());
    CHECK_TRUE(tree.isValidBST());
    CHECK_EQ(tree.size(), 64u);
}

DAEDALUS_TEST(BinarySearchTree, build_from_sorted_input) {
    BinarySearchTree<int> tree;
    tree.buildFromSorted({1, 2, 3, 4, 5, 6, 7});
    CHECK_EQ(tree.size(), 7u);
    CHECK_EQ(tree.height(), 2);
    CHECK_EQ(tree.levelOrder(), (std::vector<int>{4, 2, 6, 1, 3, 5, 7}));
}

DAEDALUS_TEST(BinarySearchTree, order_statistics_and_neighbours) {
    BinarySearchTree<int> tree{50, 30, 70, 20, 40, 60, 80};
    CHECK_EQ(tree.minimum().value(), 20);
    CHECK_EQ(tree.maximum().value(), 80);
    CHECK_EQ(tree.kthSmallest(1).value(), 20);
    CHECK_EQ(tree.kthSmallest(4).value(), 50);
    CHECK_EQ(tree.kthLargest(1).value(), 80);
    CHECK_FALSE(tree.kthSmallest(0).has_value());
    CHECK_FALSE(tree.kthSmallest(8).has_value());

    CHECK_EQ(tree.successor(50).value(), 60);
    CHECK_EQ(tree.predecessor(50).value(), 40);
    CHECK_EQ(tree.successor(35).value(), 40);      // absent keys work too
    CHECK_FALSE(tree.successor(80).has_value());
    CHECK_FALSE(tree.predecessor(20).has_value());
}

DAEDALUS_TEST(BinarySearchTree, lowest_common_ancestor) {
    BinarySearchTree<int> tree{50, 30, 70, 20, 40, 60, 80};
    CHECK_EQ(tree.lowestCommonAncestor(20, 40).value(), 30);
    CHECK_EQ(tree.lowestCommonAncestor(20, 80).value(), 50);
    CHECK_EQ(tree.lowestCommonAncestor(30, 30).value(), 30);
    CHECK_EQ(tree.lowestCommonAncestor(60, 80).value(), 70);
    CHECK_FALSE(tree.lowestCommonAncestor(20, 999).has_value());
}

DAEDALUS_TEST(BinarySearchTree, range_query) {
    BinarySearchTree<int> tree{50, 30, 70, 20, 40, 60, 80};
    CHECK_EQ(tree.rangeQuery(30, 60), (std::vector<int>{30, 40, 50, 60}));
    CHECK_EQ(tree.rangeQuery(0, 10), (std::vector<int>{}));
    CHECK_EQ(tree.rangeQuery(0, 100).size(), 7u);
}

DAEDALUS_TEST(BinarySearchTree, shape_metrics) {
    BinarySearchTree<int> tree{4, 2, 6, 1, 3, 5, 7};
    CHECK_EQ(tree.countLeaves(), 4u);
    CHECK_EQ(tree.countInternalNodes(), 3u);
    CHECK_EQ(tree.diameter(), 4);          // 1 -> 2 -> 4 -> 6 -> 5, four edges
    CHECK_TRUE(tree.isBalanced());
}

DAEDALUS_TEST(BinarySearchTree, pretty_print_renders_every_key) {
    BinarySearchTree<int> tree{2, 1, 3};
    const std::string rendered = tree.prettyPrint();
    CHECK_TRUE(rendered.find("1") != std::string::npos);
    CHECK_TRUE(rendered.find("2") != std::string::npos);
    CHECK_TRUE(rendered.find("3") != std::string::npos);
    CHECK_EQ(BinarySearchTree<int>{}.prettyPrint(), std::string("(empty)\n"));
}

DAEDALUS_TEST(BinarySearchTree, empty_tree_answers_safely) {
    BinarySearchTree<int> tree;
    CHECK_TRUE(tree.empty());
    CHECK_EQ(tree.height(), -1);
    CHECK_FALSE(tree.minimum().has_value());
    CHECK_FALSE(tree.maximum().has_value());
    CHECK_EQ(tree.inOrder(), (std::vector<int>{}));
    CHECK_TRUE(tree.isValidBST());
    CHECK_TRUE(tree.isBalanced());
    CHECK_EQ(tree.diameter(), 0);
}

DAEDALUS_TEST(BinarySearchTree, clear_then_reuse) {
    BinarySearchTree<int> tree{1, 2, 3};
    tree.clear();
    CHECK_EQ(tree.size(), 0u);
    tree.insert(9);
    CHECK_EQ(tree.toVector(), (std::vector<int>{9}));
}

DAEDALUS_TEST(BinarySearchTree, matches_std_set_under_random_load) {
    BinarySearchTree<int> tree;
    exerciseAgainstStdSet(tree, [](const BinarySearchTree<int>& t) { return t.isValidBST(); },
                          20260910u);
}

DAEDALUS_TEST(BinarySearchTree, works_with_strings) {
    BinarySearchTree<std::string> tree{"pear", "apple", "fig"};
    CHECK_EQ(tree.toVector(), (std::vector<std::string>{"apple", "fig", "pear"}));
    CHECK_TRUE(tree.contains("fig"));
    CHECK_FALSE(tree.contains("plum"));
}

// ============================================================================
//  AVLTree
// ============================================================================

DAEDALUS_TEST(AVLTree, sorted_input_stays_balanced) {
    AVLTree<int> tree;
    for (int i = 0; i < 1000; ++i) tree.insert(i);
    CHECK_EQ(tree.size(), 1000u);
    CHECK_TRUE(tree.isAVLBalanced());
    CHECK_TRUE(tree.isValidBST());
    // AVL guarantees h < 1.44 * log2(n+2); log2(1002) is about 10.
    CHECK_LE(tree.height(), 14);
}

DAEDALUS_TEST(AVLTree, each_rotation_case) {
    AVLTree<int> ll{30, 20, 10};   // left-left
    CHECK_EQ(ll.levelOrder(), (std::vector<int>{20, 10, 30}));

    AVLTree<int> rr{10, 20, 30};   // right-right
    CHECK_EQ(rr.levelOrder(), (std::vector<int>{20, 10, 30}));

    AVLTree<int> lr{30, 10, 20};   // left-right
    CHECK_EQ(lr.levelOrder(), (std::vector<int>{20, 10, 30}));

    AVLTree<int> rl{10, 30, 20};   // right-left
    CHECK_EQ(rl.levelOrder(), (std::vector<int>{20, 10, 30}));
}

DAEDALUS_TEST(AVLTree, height_is_O1_and_correct) {
    AVLTree<int> tree;
    CHECK_EQ(tree.height(), -1);
    tree.insert(1);
    CHECK_EQ(tree.height(), 0);
    tree.insert(2);
    tree.insert(3);
    CHECK_EQ(tree.height(), 1);
    CHECK_EQ(tree.balanceFactor(), 0);
}

DAEDALUS_TEST(AVLTree, erase_rebalances) {
    AVLTree<int> tree;
    for (int i = 0; i < 200; ++i) tree.insert(i);
    for (int i = 0; i < 150; ++i) {
        CHECK_TRUE(tree.erase(i));
        CHECK_TRUE(tree.isAVLBalanced());
    }
    CHECK_EQ(tree.size(), 50u);
    CHECK_TRUE(tree.isValidBST());
    CHECK_EQ(tree.minimum().value(), 150);
}

DAEDALUS_TEST(AVLTree, matches_std_set_under_random_load) {
    AVLTree<int> tree;
    exerciseAgainstStdSet(tree, [](const AVLTree<int>& t) { return t.isAVLBalanced(); },
                          424242u);
}

// ============================================================================
//  RedBlackTree
// ============================================================================

DAEDALUS_TEST(RedBlackTree, properties_hold_after_sorted_insertion) {
    RedBlackTree<int> tree;
    for (int i = 0; i < 1000; ++i) {
        tree.insert(i);
        if (i % 50 == 0) CHECK_TRUE(tree.verifyProperties());
    }
    CHECK_TRUE(tree.verifyProperties());
    CHECK_EQ(tree.size(), 1000u);
    // Red-black guarantees h <= 2*log2(n+1); log2(1001) is about 10.
    CHECK_LE(tree.height(), 20);
}

DAEDALUS_TEST(RedBlackTree, properties_hold_after_every_single_erase) {
    RedBlackTree<int> tree;
    const std::vector<int> keys = shuffled(300, 7u);
    for (int key : keys) tree.insert(key);
    CHECK_TRUE(tree.verifyProperties());

    for (int key : shuffled(300, 99u)) {
        CHECK_TRUE(tree.erase(key));
        CHECK_TRUE(tree.verifyProperties());
    }
    CHECK_TRUE(tree.empty());
    CHECK_EQ(tree.blackHeight(), 1);
}

DAEDALUS_TEST(RedBlackTree, black_height_is_uniform) {
    RedBlackTree<int> tree;
    for (int i : shuffled(100, 3u)) tree.insert(i);
    CHECK_LT(0, tree.blackHeight());
    CHECK_TRUE(tree.verifyProperties());
}

DAEDALUS_TEST(RedBlackTree, matches_std_set_under_random_load) {
    RedBlackTree<int> tree;
    exerciseAgainstStdSet(tree, [](const RedBlackTree<int>& t) { return t.verifyProperties(); },
                          20261231u);
}

DAEDALUS_TEST(RedBlackTree, erase_missing_key_is_a_no_op) {
    RedBlackTree<int> tree{1, 2, 3};
    CHECK_FALSE(tree.erase(99));
    CHECK_EQ(tree.size(), 3u);
    CHECK_TRUE(tree.verifyProperties());
}

// ============================================================================
//  SplayTree
// ============================================================================

DAEDALUS_TEST(SplayTree, accessed_key_moves_to_the_root) {
    SplayTree<int> tree;
    for (int i = 1; i <= 10; ++i) tree.insert(i);
    CHECK_TRUE(tree.contains(4));
    CHECK_EQ(tree.mostRecentlyAccessed().value(), 4);
    CHECK_TRUE(tree.contains(9));
    CHECK_EQ(tree.mostRecentlyAccessed().value(), 9);
    CHECK_TRUE(tree.isValidBST());
}

DAEDALUS_TEST(SplayTree, missing_key_lookup_keeps_the_tree_valid) {
    SplayTree<int> tree{10, 20, 30};
    CHECK_FALSE(tree.contains(25));
    CHECK_TRUE(tree.isValidBST());
    CHECK_EQ(tree.toVector(), (std::vector<int>{10, 20, 30}));
}

DAEDALUS_TEST(SplayTree, insert_erase_and_contents) {
    SplayTree<int> tree;
    for (int i : shuffled(200, 11u)) tree.insert(i);
    CHECK_EQ(tree.size(), 200u);
    CHECK_TRUE(tree.isValidBST());

    for (int i = 0; i < 100; ++i) CHECK_TRUE(tree.erase(i));
    CHECK_FALSE(tree.erase(0));
    CHECK_EQ(tree.size(), 100u);
    CHECK_EQ(tree.minimum().value(), 100);
    CHECK_TRUE(tree.isValidBST());
}

DAEDALUS_TEST(SplayTree, repeated_access_is_cheap) {
    SplayTree<int> tree;
    for (int i = 0; i < 500; ++i) tree.insert(i);
    // Touching one key 100 times must leave it at the root every time.
    for (int round = 0; round < 100; ++round) {
        CHECK_TRUE(tree.contains(250));
        CHECK_EQ(tree.mostRecentlyAccessed().value(), 250);
    }
    CHECK_TRUE(tree.isValidBST());
}

DAEDALUS_TEST(SplayTree, matches_std_set_under_random_load) {
    SplayTree<int> tree;
    exerciseAgainstStdSet(tree, [](const SplayTree<int>& t) { return t.isValidBST(); },
                          5150u);
}

// ============================================================================
//  Treap
// ============================================================================

DAEDALUS_TEST(Treap, keeps_bst_and_heap_invariants) {
    Treap<int> treap;
    for (int i : shuffled(500, 21u)) treap.insert(i);
    CHECK_EQ(treap.size(), 500u);
    CHECK_TRUE(treap.isValidBST());
    CHECK_TRUE(treap.verifyHeapProperty());
}

DAEDALUS_TEST(Treap, random_priorities_keep_it_shallow) {
    Treap<int> treap;
    for (int i = 0; i < 4096; ++i) treap.insert(i);   // worst case for a plain BST
    CHECK_TRUE(treap.verifyHeapProperty());
    // Expected height is about 3*log2(n) = 36; a degenerate tree would be 4095.
    CHECK_LE(treap.height(), 60);
}

DAEDALUS_TEST(Treap, erase_maintains_both_invariants) {
    Treap<int> treap;
    for (int i : shuffled(300, 31u)) treap.insert(i);
    for (int i : shuffled(300, 41u)) {
        CHECK_TRUE(treap.erase(i));
        if (i % 25 == 0) {
            CHECK_TRUE(treap.isValidBST());
            CHECK_TRUE(treap.verifyHeapProperty());
        }
    }
    CHECK_TRUE(treap.empty());
}

DAEDALUS_TEST(Treap, seeding_is_reproducible) {
    Treap<int> a(12345u);
    Treap<int> b(12345u);
    for (int i : shuffled(100, 5u)) {
        a.insert(i);
        b.insert(i);
    }
    CHECK_EQ(a.levelOrder(), b.levelOrder());   // identical shape, not just contents
}

DAEDALUS_TEST(Treap, matches_std_set_under_random_load) {
    Treap<int> treap;
    exerciseAgainstStdSet(treap, [](const Treap<int>& t) { return t.verifyHeapProperty(); },
                          31337u);
}

// ============================================================================
//  Runtime polymorphism across the whole family
// ============================================================================

DAEDALUS_TEST(Polymorphism, every_tree_is_interchangeable_through_SortedSet) {
    std::vector<std::unique_ptr<SortedSet<int>>> trees;
    trees.push_back(std::make_unique<BinarySearchTree<int>>());
    trees.push_back(std::make_unique<AVLTree<int>>());
    trees.push_back(std::make_unique<RedBlackTree<int>>());
    trees.push_back(std::make_unique<SplayTree<int>>());
    trees.push_back(std::make_unique<Treap<int>>());

    const std::vector<int> keys = shuffled(200, 77u);
    for (auto& tree : trees) {
        for (int key : keys) tree->insert(key);

        CHECK_EQ(tree->size(), 200u);
        CHECK_EQ(tree->minimum().value(), 0);
        CHECK_EQ(tree->maximum().value(), 199);
        CHECK_TRUE(tree->contains(100));
        CHECK_FALSE(tree->contains(999));
        CHECK_TRUE(tree->erase(100));
        CHECK_FALSE(tree->erase(100));
        CHECK_EQ(tree->size(), 199u);

        std::vector<int> expected;
        for (int i = 0; i < 200; ++i) {
            if (i != 100) expected.push_back(i);
        }
        CHECK_EQ(tree->toVector(), expected);
        CHECK_FALSE(tree->name().empty());
        CHECK_TRUE(tree->toString().rfind(tree->name(), 0) == 0);
    }
}

DAEDALUS_TEST(Polymorphism, balanced_trees_beat_the_naive_one_on_sorted_input) {
    BinarySearchTree<int> plain;
    AVLTree<int> avl;
    RedBlackTree<int> redBlack;
    for (int i = 0; i < 512; ++i) {
        plain.insert(i);
        avl.insert(i);
        redBlack.insert(i);
    }
    CHECK_EQ(plain.height(), 511);
    CHECK_LT(avl.height(), 15);
    CHECK_LT(redBlack.height(), 20);
    CHECK_LT(avl.height(), plain.height());
}
