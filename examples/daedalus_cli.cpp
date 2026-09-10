// ============================================================================
//  Daedalus :: examples/daedalus_cli.cpp
//
//  A terminal tour of the library. Each subcommand demonstrates one area and
//  prints real measured numbers rather than assertions about them.
//
//      daedalus_cli demo        run everything, in order
//      daedalus_cli sort        sorting strategies, instrumented
//      daedalus_cli trees       the five search trees on the same keys
//      daedalus_cli graph       traversal, shortest paths, MST, SCC
//      daedalus_cli strings     matching, palindromes, edit distance
//      daedalus_cli hashing     hash maps, sets, bloom filter, caches
//      daedalus_cli algorithms  DP, greedy, backtracking, number theory
//      daedalus_cli patterns    the OOP design patterns, run live
//
//  Output is pure ASCII: a Windows console in the cp1252 code page throws on a
//  non-ASCII write, and a demo that crashes on the reviewer's machine is worse
//  than no demo.
// ============================================================================
#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <random>
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
#include "daedalus/core/Comparator.hpp"
#include "daedalus/core/Version.hpp"
#include "daedalus/graph/Connectivity.hpp"
#include "daedalus/graph/Graph.hpp"
#include "daedalus/graph/MinimumSpanningTree.hpp"
#include "daedalus/graph/NetworkFlow.hpp"
#include "daedalus/graph/ShortestPath.hpp"
#include "daedalus/graph/Traversal.hpp"
#include "daedalus/hashing/BloomFilter.hpp"
#include "daedalus/hashing/Cache.hpp"
#include "daedalus/hashing/HashMap.hpp"
#include "daedalus/hashing/HashSet.hpp"
#include "daedalus/linear/DoublyLinkedList.hpp"
#include "daedalus/linear/DynamicArray.hpp"
#include "daedalus/linear/SkipList.hpp"
#include "daedalus/linear/Stack.hpp"
#include "daedalus/sets/DisjointSet.hpp"
#include "daedalus/trees/AVLTree.hpp"
#include "daedalus/trees/BTree.hpp"
#include "daedalus/trees/BinarySearchTree.hpp"
#include "daedalus/trees/FenwickTree.hpp"
#include "daedalus/trees/RedBlackTree.hpp"
#include "daedalus/trees/SegmentTree.hpp"
#include "daedalus/trees/SplayTree.hpp"
#include "daedalus/trees/Treap.hpp"
#include "daedalus/trees/Trie.hpp"

using namespace daedalus;

namespace {

void heading(const std::string& title) {
    std::cout << "\n" << std::string(74, '=') << "\n  " << title << "\n"
              << std::string(74, '=') << "\n";
}

void section(const std::string& title) {
    const std::size_t padding = title.size() >= 68 ? 1 : 68 - title.size();
    std::cout << "\n-- " << title << " " << std::string(padding, '-') << "\n";
}

template <typename T>
void printRow(const std::string& label, const T& value) {
    std::cout << "  " << std::left << std::setw(28) << label << value << "\n";
}

std::string join(const std::vector<int>& values, std::size_t limit = 20) {
    std::string out;
    for (std::size_t i = 0; i < values.size() && i < limit; ++i) {
        if (i > 0) out += " ";
        out += std::to_string(values[i]);
    }
    if (values.size() > limit) out += " ... (" + std::to_string(values.size()) + " total)";
    return out;
}

std::vector<int> shuffledRange(int count, std::uint32_t seed) {
    std::vector<int> values(static_cast<std::size_t>(count));
    std::iota(values.begin(), values.end(), 1);
    std::mt19937 rng(seed);
    std::shuffle(values.begin(), values.end(), rng);
    return values;
}

double milliseconds(const std::function<void()>& work) {
    const auto started = std::chrono::steady_clock::now();
    work();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
        .count();
}

// ---------------------------------------------------------------------------

void demoSorting() {
    heading("Sorting: twelve strategies, one interface, real counters");

    const std::vector<int> input = shuffledRange(600, 2026u);
    std::cout << "\n  Input: 600 shuffled values. Every comparison and swap below was\n"
                 "  counted by an observer attached to the strategy at runtime.\n\n";

    std::cout << "  " << std::left << std::setw(12) << "algorithm" << std::setw(14) << "comparisons"
              << std::setw(12) << "swaps" << std::setw(10) << "ms" << std::setw(9) << "stable"
              << std::setw(10) << "in place" << "worst case\n";
    std::cout << "  " << std::string(70, '-') << "\n";

    for (const std::string& name : availableSortStrategies<int>()) {
        auto strategy = makeSortStrategy<int>(name);
        auto metrics = std::make_shared<MetricsObserver>();
        strategy->attach(metrics);

        std::vector<int> data = input;
        const double elapsed = milliseconds([&] { strategy->sort(data); });
        if (!isSorted(data)) {
            std::cout << "  " << name << ": FAILED TO SORT\n";
            continue;
        }

        std::cout << "  " << std::left << std::setw(12) << name << std::setw(14)
                  << metrics->comparisons() << std::setw(12) << metrics->swaps()
                  << std::setw(10) << std::fixed << std::setprecision(2) << elapsed
                  << std::setw(9) << (strategy->stable() ? "yes" : "no") << std::setw(10)
                  << (strategy->inPlace() ? "yes" : "no") << strategy->worstComplexity() << "\n";
    }

    section("the same algorithms on already-sorted input");
    std::vector<int> ascending(600);
    std::iota(ascending.begin(), ascending.end(), 1);
    for (const char* name : {"bubble", "insertion", "quick", "tim"}) {
        auto strategy = makeSortStrategy<int>(name);
        auto metrics = std::make_shared<MetricsObserver>();
        strategy->attach(metrics);
        std::vector<int> data = ascending;
        strategy->sort(data);
        printRow(std::string(name) + " comparisons", metrics->comparisons());
    }
    std::cout << "\n  Bubble and insertion drop to O(n) because they detect the order.\n"
                 "  Quicksort does NOT degrade, because of median-of-three pivoting --\n"
                 "  a naive implementation would go quadratic on exactly this input.\n";
}

void demoTrees() {
    heading("Search trees: the same keys, five different shapes");

    const int count = 1023;
    std::cout << "\n  Inserting 1..1023 in ASCENDING order -- the worst case for an\n"
                 "  unbalanced tree. A perfectly balanced tree would be 9 levels deep.\n\n";

    std::vector<std::unique_ptr<SortedSet<int>>> trees;
    trees.push_back(std::make_unique<BinarySearchTree<int>>());
    trees.push_back(std::make_unique<AVLTree<int>>());
    trees.push_back(std::make_unique<RedBlackTree<int>>());
    trees.push_back(std::make_unique<SplayTree<int>>());
    trees.push_back(std::make_unique<Treap<int>>());

    std::cout << "  " << std::left << std::setw(20) << "structure" << std::setw(10) << "height"
              << std::setw(12) << "build ms" << "note\n";
    std::cout << "  " << std::string(64, '-') << "\n";

    for (auto& tree : trees) {
        const double elapsed = milliseconds([&] {
            for (int i = 1; i <= count; ++i) tree->insert(i);
        });
        const std::string note = tree->height() > 100 ? "degenerate: this is a linked list"
                                                       : "stays logarithmic";
        std::cout << "  " << std::left << std::setw(20) << tree->name() << std::setw(10)
                  << tree->height() << std::setw(12) << std::fixed << std::setprecision(2)
                  << elapsed << note << "\n";
    }

    section("a small AVL tree, drawn");
    AVLTree<int> small;
    for (int value : {50, 30, 70, 20, 40, 60, 80, 35}) small.insert(value);
    std::cout << small.prettyPrint();
    printRow("in-order", join(small.inOrder()));
    printRow("level-order", join(small.levelOrder()));
    printRow("height", small.height());
    printRow("kth smallest (k=3)", small.kthSmallest(3).value());
    printRow("successor of 40", small.successor(40).value());
    printRow("LCA of 20 and 40", small.lowestCommonAncestor(20, 40).value());
    printRow("range [30, 60]", join(small.rangeQuery(30, 60)));

    section("B-tree: fanout keeps a million keys three levels deep");
    BTree<int> wide(64);
    for (int i = 0; i < 100000; ++i) wide.insert(i);
    printRow("keys", wide.size());
    printRow("minimum degree", wide.minimumDegree());
    printRow("height (edges)", wide.height());
    printRow("nodes allocated", wide.nodeCount());
    printRow("invariants hold", wide.verifyProperties() ? "yes" : "NO");

    section("indexed range structures");
    std::vector<int> values{5, 2, 8, 1, 9, 3, 7, 4};
    SegmentTree<int> sums(values);
    SegmentTree<int, MinPolicy<int>> minima(values);
    FenwickTree<long long> fenwick(std::vector<long long>(values.begin(), values.end()));
    printRow("array", join(values));
    printRow("sum of [2, 5]", sums.query(2, 5));
    printRow("min of [2, 5]", minima.query(2, 5));
    printRow("fenwick prefix to 5", fenwick.prefixSum(5));

    LazySegmentTree<int> lazy(values);
    lazy.rangeAdd(1, 4, 10);
    printRow("after +10 on [1,4]", lazy.query(0, 7));

    section("trie: the query a hash map cannot answer");
    Trie trie{"labyrinth", "labour", "label", "minotaur", "minos"};
    printRow("words stored", trie.size());
    printRow("starts with 'lab'", trie.countWithPrefix("lab"));
    std::cout << "  " << std::left << std::setw(32) << "completions of 'lab'";
    for (const std::string& word : trie.withPrefix("lab")) std::cout << word << " ";
    std::cout << "\n";
    printRow("longest prefix of 'minotaurs'", trie.longestPrefixOf("minotaurs"));
}

void demoGraph() {
    heading("Graphs: traversal, shortest paths, spanning trees, connectivity");

    Graph<std::string, int> roads(false);
    roads.addEdge("A", "B", 4);
    roads.addEdge("A", "H", 8);
    roads.addEdge("B", "C", 8);
    roads.addEdge("B", "H", 11);
    roads.addEdge("C", "D", 7);
    roads.addEdge("C", "I", 2);
    roads.addEdge("C", "F", 4);
    roads.addEdge("D", "E", 9);
    roads.addEdge("D", "F", 14);
    roads.addEdge("E", "F", 10);
    roads.addEdge("F", "G", 2);
    roads.addEdge("G", "H", 1);
    roads.addEdge("G", "I", 6);
    roads.addEdge("H", "I", 7);

    printRow("vertices", roads.vertexCount());
    printRow("edges", roads.edgeCount());

    section("traversal");
    const auto breadth = breadthFirstSearch(roads, std::string("A"));
    std::cout << "  " << std::left << std::setw(32) << "BFS from A";
    for (const std::string& vertex : breadth.order) std::cout << vertex << " ";
    std::cout << "\n";
    const auto depth = depthFirstSearch(roads, std::string("A"));
    std::cout << "  " << std::left << std::setw(32) << "DFS from A";
    for (const std::string& vertex : depth.order) std::cout << vertex << " ";
    std::cout << "\n";

    section("shortest paths from A");
    const auto shortest = dijkstra(roads, std::string("A"));
    for (std::size_t v = 0; v < roads.vertexCount(); ++v) {
        std::cout << "  " << std::left << std::setw(32)
                  << ("distance to " + roads.label(v))
                  << (shortest.reachable(v) ? std::to_string(shortest.distance[v])
                                            : std::string("unreachable"))
                  << "\n";
    }
    std::cout << "  " << std::left << std::setw(32) << "path A -> E";
    for (const std::string& vertex : reconstructPath(roads, shortest, std::string("E"))) {
        std::cout << vertex << " ";
    }
    std::cout << "\n";
    printRow("vertices settled", shortest.settledCount);

    section("minimum spanning tree");
    const auto viaKruskal = kruskal(roads);
    const auto viaPrim = prim(roads);
    printRow("kruskal total weight", viaKruskal.totalWeight);
    printRow("prim total weight", viaPrim.totalWeight);
    printRow("edges chosen", viaKruskal.edges.size());
    std::cout << "  " << std::left << std::setw(32) << "tree edges";
    for (const auto& edge : viaKruskal.edges) {
        std::cout << edge.from << "-" << edge.to << "(" << edge.weight << ") ";
    }
    std::cout << "\n  Both algorithms are greedy and both are optimal, so the totals\n"
                 "  must agree even though the edge sets can differ.\n";

    section("structure");
    printRow("connected", isConnected(roads) ? "yes" : "no");
    printRow("bridges", findBridges(roads).size());
    printRow("articulation points", findArticulationPoints(roads).size());
    printRow("bipartite", checkBipartite(roads).bipartite ? "yes" : "no");
    printRow("has a cycle", hasCycle(roads) ? "yes" : "no");

    section("directed graphs");
    Graph<std::string, int> build(true);
    build.addEdge("core", "linear");
    build.addEdge("core", "trees");
    build.addEdge("linear", "graph");
    build.addEdge("trees", "graph");
    build.addEdge("graph", "cli");
    const auto order = topologicalSort(build);
    std::cout << "  " << std::left << std::setw(32) << "build order";
    for (const std::string& target : order.value()) std::cout << target << " ";
    std::cout << "\n";

    Graph<std::string, int> cyclic(true);
    cyclic.addEdge("a", "b");
    cyclic.addEdge("b", "c");
    cyclic.addEdge("c", "a");
    cyclic.addEdge("c", "d");
    printRow("cyclic graph sorts", topologicalSort(cyclic).has_value() ? "yes" : "no (correct)");
    printRow("strongly connected parts", tarjanStronglyConnectedComponents(cyclic).size());

    section("network flow");
    Graph<std::string, int> network(true);
    network.addEdge("s", "v1", 16);
    network.addEdge("s", "v2", 13);
    network.addEdge("v1", "v3", 12);
    network.addEdge("v2", "v1", 4);
    network.addEdge("v2", "v4", 14);
    network.addEdge("v3", "v2", 9);
    network.addEdge("v3", "t", 20);
    network.addEdge("v4", "v3", 7);
    network.addEdge("v4", "t", 4);
    const auto flow = dinic(network, std::string("s"), std::string("t"));
    printRow("maximum flow", flow.maxFlow);
    printRow("minimum cut capacity", flow.minCutCapacity);
    std::cout << "  Equal, as the max-flow min-cut theorem requires.\n";
}

void demoStrings() {
    heading("Strings: matching, palindromes, distance, suffix arrays");

    const std::string text = "the labyrinth of daedalus, the labyrinth of minos";
    const std::string pattern = "labyrinth";
    printRow("text", text);
    printRow("pattern", pattern);

    section("every matcher agrees");
    for (const auto& entry : std::vector<std::pair<std::string, std::vector<std::size_t>>>{
             {"naive", naiveSearch(text, pattern)},
             {"knuth-morris-pratt", knuthMorrisPratt(text, pattern)},
             {"z algorithm", zSearch(text, pattern)},
             {"rabin-karp", rabinKarp(text, pattern)},
             {"boyer-moore-horspool", boyerMooreHorspool(text, pattern)}}) {
        std::cout << "  " << std::left << std::setw(32) << entry.first;
        for (std::size_t position : entry.second) std::cout << position << " ";
        std::cout << "\n";
    }

    section("analysis");
    printRow("longest palindrome", longestPalindrome("forgeeksskeegfor"));
    printRow("edit distance", editDistance("kitten", "sitting"));
    printRow("longest common subsequence", longestCommonSubsequence("ABCBDAB", "BDCABA"));
    printRow("longest common substring", longestCommonSubstring("ABABC", "BABCA"));
    printRow("longest repeated substring", longestRepeatedSubstring("banana"));
    printRow("anagrams?", areAnagrams("listen", "silent") ? "yes" : "no");
    printRow("rotation?", isRotationOf("waterbottle", "erbottlewat") ? "yes" : "no");
    printRow("longest unique run", longestUniqueSubstringLength("abcabcbb"));

    section("multi-pattern matching in one pass");
    AhoCorasick automaton({"he", "she", "his", "hers"});
    std::cout << "  patterns: he she his hers, text: \"ushers\"\n";
    for (const auto& match : automaton.search("ushers")) {
        std::cout << "    " << match.pattern << " at " << match.position << "\n";
    }
    printRow("trie nodes", automaton.nodeCount());
}

void demoHashing() {
    heading("Hashing: maps, sets, bloom filters and caches");

    section("hash map, and what a bad hash costs");
    struct Collapsing {
        std::size_t operator()(int key) const { return static_cast<std::size_t>(key % 8); }
    };
    HashMap<int, int> healthy;
    HashMap<int, int, Collapsing> clustered;
    for (int i = 0; i < 5000; ++i) {
        healthy.put(i, i);
        clustered.put(i, i);
    }
    const HashStatistics good = healthy.statistics();
    const HashStatistics bad = clustered.statistics();
    printRow("entries", healthy.size());
    printRow("healthy: buckets used", good.usedBuckets);
    printRow("healthy: longest chain", good.longestChain);
    printRow("clustered: buckets used", bad.usedBuckets);
    printRow("clustered: longest chain", bad.longestChain);
    std::cout << "  Both still answer correctly -- chaining degrades, it does not break.\n";

    section("set algebra");
    HashSet<int> left{1, 2, 3, 4, 5};
    HashSet<int> right{4, 5, 6, 7};
    printRow("union size", left.unionWith(right).size());
    printRow("intersection size", left.intersectionWith(right).size());
    printRow("difference size", left.differenceWith(right).size());
    printRow("disjoint?", left.isDisjointFrom(right) ? "yes" : "no");

    section("bloom filter: memory versus certainty");
    BloomFilter<std::string> filter(100000, 0.01);
    for (int i = 0; i < 100000; ++i) filter.add("key:" + std::to_string(i));
    std::size_t falsePositives = 0;
    for (int i = 0; i < 20000; ++i) {
        if (filter.mightContain("absent:" + std::to_string(i))) ++falsePositives;
    }
    printRow("items stored", filter.insertedCount());
    printRow("memory used (bytes)", filter.memoryBytes());
    printRow("hash functions", filter.hashCount());
    printRow("target error rate", filter.targetFalsePositiveRate());
    printRow("observed error rate",
             static_cast<double>(falsePositives) / 20000.0);
    std::cout << "  Never a false negative -- that is the guarantee it trades memory for.\n";

    section("cache policies on a scan-heavy workload");
    LRUCache<int, int> lru(10);
    LFUCache<int, int> lfu(10);

    // Warm the hot set first, then reset the counters. Without the warm-up
    // LFU has no frequency history to work with and behaves exactly like LRU,
    // which would make the comparison below meaningless.
    for (int warm = 0; warm < 50; ++warm) {
        for (int hot = 0; hot < 4; ++hot) {
            if (!lru.get(hot).has_value()) lru.put(hot, hot);
            if (!lfu.get(hot).has_value()) lfu.put(hot, hot);
        }
    }
    lru.resetStatistics();
    lfu.resetStatistics();

    for (int round = 0; round < 100; ++round) {
        for (int hot = 0; hot < 4; ++hot) {
            if (!lru.get(hot).has_value()) lru.put(hot, hot);
            if (!lfu.get(hot).has_value()) lfu.put(hot, hot);
        }
        for (int i = 0; i < 20; ++i) {   // a cold scan LONGER than the cache
            const int cold = 1000 + round * 20 + i;
            if (!lru.get(cold).has_value()) lru.put(cold, cold);
            if (!lfu.get(cold).has_value()) lfu.put(cold, cold);
        }
    }
    std::cout << std::setprecision(3);
    printRow("LRU hit rate", lru.statistics().hitRate());
    printRow("LFU hit rate", lfu.statistics().hitRate());
    std::cout << std::setprecision(2);
    std::cout << "  A cold scan longer than the cache flushes an LRU completely;\n"
                 "  LFU keeps the hot set because its frequency dwarfs the scan's.\n";

    section("union-find");
    DisjointSet sets(1000000);
    for (std::size_t i = 1; i < 1000000; ++i) sets.unite(i - 1, i);
    for (std::size_t i = 0; i < 1000000; ++i) (void)sets.find(i);
    printRow("elements", sets.size());
    printRow("components", sets.componentCount());
    printRow("deepest chain after find", sets.maximumDepth());
    std::cout << "  A chain union then a full pass of finds: path compression has\n"
                 "  flattened every element to point straight at the root.\n";

    section("skip list");
    SkipList<int> skip;
    for (int value : shuffledRange(65536, 7u)) skip.insert(value);
    printRow("keys", skip.size());
    printRow("levels in use", skip.height() + 1);
    printRow("lower bound of 5000", skip.lowerBound(5000).value());
}

void demoAlgorithms() {
    heading("Algorithms: dynamic programming, greedy, backtracking, number theory");

    section("dynamic programming");
    const auto best = maximumSubarray({-2, 1, -3, 4, -1, 2, 1, -5, 4});
    printRow("max subarray sum", best.sum);
    printRow("  at indices", std::to_string(best.first) + ".." + std::to_string(best.last));

    const auto pack = knapsack01({10, 20, 30}, {60, 100, 120}, 50);
    printRow("0/1 knapsack value", pack.value);
    std::cout << "  " << std::left << std::setw(32) << "items chosen";
    for (std::size_t index : pack.chosenItems) std::cout << index << " ";
    std::cout << "\n";
    printRow("unbounded knapsack", unboundedKnapsack({10, 20, 30}, {60, 100, 120}, 50));

    const auto change = coinChangeMinimum({1, 3, 4}, 6);
    printRow("coins for 6 from {1,3,4}", change.coinCount);
    const auto greedyChange = minimumCoinsGreedy({1, 3, 4}, 6);
    printRow("  greedy would use", greedyChange.size());
    std::cout << "  This is why coin change is a DP and not a greedy: the greedy\n"
                 "  takes 4+1+1 where 3+3 is optimal.\n";

    printRow("LIS length", longestIncreasingSubsequence({10, 9, 2, 5, 3, 7, 101, 18}).size());
    printRow("matrix chain cost", matrixChainOrder({40, 20, 30, 10, 30}).multiplications);
    printRow("  parenthesisation", matrixChainOrder({40, 20, 30, 10, 30}).parenthesisation);
    printRow("rod cutting (len 8)", rodCutting({1, 5, 8, 9, 10, 17, 17, 20}, 8));
    printRow("house robber", houseRobber({2, 7, 9, 3, 1}));
    printRow("equal partition possible", canPartitionEqually({1, 5, 11, 5}) ? "yes" : "no");

    section("greedy");
    const auto huffman = huffmanCoding("this is an example of a huffman tree");
    printRow("huffman encoded bits", huffman.encodedBits);
    printRow("fixed-width would need", huffman.fixedWidthBits);
    printRow("compression ratio", huffman.compressionRatio());
    printRow("platforms needed",
             minimumPlatforms({900, 940, 950, 1100, 1500, 1800},
                              {910, 1200, 1120, 1130, 1900, 2000}));

    section("backtracking");
    for (std::size_t n : {std::size_t{6}, std::size_t{8}, std::size_t{10}}) {
        const auto queens = solveNQueens(n, true);
        std::cout << "  " << std::left << std::setw(32)
                  << (std::to_string(n) + "-queens solutions") << queens.solutionCount
                  << "   (nodes explored: " << queens.nodesExplored << ")\n";
    }
    const auto eight = solveNQueens(8);
    std::cout << "\n  One of the 92 solutions for n=8:\n\n";
    for (const std::string& line : std::vector<std::string>{renderQueens(eight.solutions[0])}) {
        std::size_t start = 0;
        while (start < line.size()) {
            const std::size_t end = line.find('\n', start);
            if (end == std::string::npos) break;
            std::cout << "    " << line.substr(start, end - start) << "\n";
            start = end + 1;
        }
    }

    section("number theory");
    printRow("primes below 100000", sieveOfEratosthenes(100000).size());
    printRow("is 1000000007 prime", isPrime(1000000007ull) ? "yes" : "no");
    printRow("largest 64-bit prime",
             isPrime(18446744073709551557ull) ? "confirmed prime" : "NOT PRIME");
    printRow("gcd(48, 18)", greatestCommonDivisor(48, 18));
    printRow("3^-1 mod 11", modularInverse(3, 11).value());
    printRow("2^100 mod 1e9+7", modularPower(2, 100, 1000000007ull));
    printRow("C(50, 25) mod 1e9+7", binomialModulo(50, 25, 1000000007ull));
    printRow("fibonacci(90)", fibonacci(90));
    const auto crt = chineseRemainder({2, 3, 2}, {3, 5, 7});
    printRow("CRT solution", std::to_string(crt->first) + " mod " + std::to_string(crt->second));

    section("divide and conquer");
    printRow("inversions in [4,3,2,1]", countInversions({4, 3, 2, 1}));
    printRow("karatsuba 20-digit square",
             karatsubaMultiply("99999999999999999999", "99999999999999999999"));
    printRow("median of 100k values", quickselect(shuffledRange(100000, 3u), 49999));
}

void demoPatterns() {
    heading("Design patterns, running rather than described");

    section("Strategy: the sorting algorithm is an object");
    std::unique_ptr<SortStrategy<int>> strategy = makeSortStrategy<int>("merge");
    std::vector<int> data{5, 2, 9, 1};
    strategy->sort(data);
    printRow("chosen at runtime by name", strategy->name());
    printRow("result", join(data));

    section("Observer: instrumentation attached without touching the algorithm");
    auto metrics = std::make_shared<MetricsObserver>();
    auto trace = std::make_shared<TraceObserver>(5);
    strategy->attach(metrics);
    strategy->attach(trace);
    std::vector<int> again{5, 2, 9, 1, 7, 3};
    strategy->sort(again);
    printRow("events recorded", metrics->report());
    std::cout << "  first few events:\n" << trace->report();

    section("Visitor: traversal logic supplied by the caller");
    AVLTree<int> tree{50, 30, 70, 20, 40};
    CollectingVisitor<int> collector;
    tree.accept(collector, TraversalOrder::InOrder);
    printRow("values", join(collector.values));
    std::cout << "  " << std::left << std::setw(32) << "depths";
    for (int depth : collector.depths) std::cout << depth << " ";
    std::cout << "\n";

    int total = 0;
    FunctionVisitor<int> adder([&total](const int& value, int) { total += value; });
    tree.accept(adder, TraversalOrder::LevelOrder);
    printRow("sum via a lambda visitor", total);

    section("Polymorphism: one handle, five implementations");
    std::vector<std::unique_ptr<SortedSet<int>>> structures;
    structures.push_back(std::make_unique<BinarySearchTree<int>>());
    structures.push_back(std::make_unique<AVLTree<int>>());
    structures.push_back(std::make_unique<RedBlackTree<int>>());
    structures.push_back(std::make_unique<SplayTree<int>>());
    structures.push_back(std::make_unique<Treap<int>>());
    for (auto& structure : structures) {
        for (int value : {5, 3, 8, 1, 9}) structure->insert(value);
        std::cout << "  " << std::left << std::setw(22) << structure->name()
                  << structure->toString() << "\n";
    }

    section("Adapter: one Stack, two backing stores");
    Stack<int> onArray;
    Stack<int, DoublyLinkedList<int>> onList;
    for (int value : {1, 2, 3}) {
        onArray.push(value);
        onList.push(value);
    }
    printRow("array-backed pop", onArray.pop());
    printRow("list-backed pop", onList.pop());

    section("Comparator strategy: ordering chosen at runtime");
    auto ascendingOrder = ascending<int>();
    auto descendingOrder = descending<int>();
    std::vector<int> numbers{3, 1, 2};
    // std::cref, not a bare dereference: std::sort takes its comparator BY
    // VALUE, and Comparator<T> is abstract, so *ptr would try to slice-copy an
    // abstract base. A reference wrapper is both callable and copyable.
    std::sort(numbers.begin(), numbers.end(), std::cref(*ascendingOrder));
    printRow(ascendingOrder->name(), join(numbers));
    std::sort(numbers.begin(), numbers.end(), std::cref(*descendingOrder));
    printRow(descendingOrder->name(), join(numbers));

    section("Exception hierarchy: catch one fault or all of them");
    try {
        DynamicArray<int> array{1, 2, 3};
        (void)array.at(99);
    } catch (const IndexOutOfRange& precise) {
        printRow("caught precisely", precise.what());
    }
    try {
        DynamicArray<int> empty;
        (void)empty.popBack();
    } catch (const DaedalusError& broad) {
        printRow("caught by the root type", broad.what());
    }
}

void printUsage(const char* program) {
    std::cout << daedalus::banner() << "\n\n"
              << "usage: " << program << " <command>\n\n"
              << "  demo        run every section below, in order\n"
              << "  sort        sorting strategies with live comparison counts\n"
              << "  trees       BST, AVL, red-black, splay, treap, B-tree, trie\n"
              << "  graph       traversal, shortest paths, MST, SCC, flow\n"
              << "  strings     matching, palindromes, edit distance\n"
              << "  hashing     maps, sets, bloom filter, caches, union-find\n"
              << "  algorithms  DP, greedy, backtracking, number theory\n"
              << "  patterns    the OOP design patterns, executed\n"
              << "  version     print the library version\n";
}

}  // namespace

int main(int argc, char** argv) {
    const std::map<std::string, std::function<void()>> commands{
        {"sort", demoSorting},       {"trees", demoTrees},
        {"graph", demoGraph},        {"strings", demoStrings},
        {"hashing", demoHashing},    {"algorithms", demoAlgorithms},
        {"patterns", demoPatterns}};

    if (argc < 2) {
        printUsage(argv[0]);
        return 0;
    }

    const std::string command = argv[1];
    try {
        if (command == "--help" || command == "-h" || command == "help") {
            printUsage(argv[0]);
            return 0;
        }
        if (command == "version" || command == "--version") {
            std::cout << daedalus::banner() << "\n";
            return 0;
        }
        if (command == "demo") {
            std::cout << daedalus::banner() << "\n";
            demoSorting();
            demoTrees();
            demoGraph();
            demoStrings();
            demoHashing();
            demoAlgorithms();
            demoPatterns();
            std::cout << "\n" << std::string(74, '=') << "\n"
                      << "  Every number above was computed just now. Run the test suite\n"
                      << "  (ctest --test-dir build) to see the same behaviour asserted.\n"
                      << std::string(74, '=') << "\n";
            return 0;
        }

        const auto found = commands.find(command);
        if (found == commands.end()) {
            std::cerr << "unknown command: " << command << "\n\n";
            printUsage(argv[0]);
            return 2;
        }
        std::cout << daedalus::banner() << "\n";
        found->second();
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "\nfailed: " << failure.what() << "\n";
        return 1;
    }
}
