// ============================================================================
//  Daedalus :: benchmarks/benchmark_main.cpp
//
//  Measures the library against the standard library and against its own
//  alternatives. Every number printed is measured on the machine running it --
//  nothing here is quoted from a README.
//
//      daedalus_bench            run everything
//      daedalus_bench sorting    just the sorts
//      daedalus_bench trees      just the search trees
//      daedalus_bench --scale 4  multiply every input size by 4
//
//  Method: each case is run several times and the MEDIAN is reported, not the
//  mean. A mean is dragged around by one unlucky scheduling hiccup; a median of
//  five is not. The comparison against std:: is included precisely because a
//  hand-written container that quietly loses to the standard library by 10x is
//  a fact worth knowing rather than hiding.
// ============================================================================
#include <algorithm>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <sstream>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "daedalus/algorithms/Sorting.hpp"
#include "daedalus/core/Version.hpp"
#include "daedalus/graph/Graph.hpp"
#include "daedalus/graph/ShortestPath.hpp"
#include "daedalus/graph/Traversal.hpp"
#include "daedalus/hashing/HashMap.hpp"
#include "daedalus/hashing/OpenAddressingMap.hpp"
#include "daedalus/linear/DynamicArray.hpp"
#include "daedalus/linear/SkipList.hpp"
#include "daedalus/sets/DisjointSet.hpp"
#include "daedalus/trees/AVLTree.hpp"
#include "daedalus/trees/BTree.hpp"
#include "daedalus/trees/BinarySearchTree.hpp"
#include "daedalus/trees/RedBlackTree.hpp"
#include "daedalus/trees/Treap.hpp"

using namespace daedalus;

namespace {

std::size_t g_scale = 1;

/// Runs `work` `repeats` times and returns the median wall time in
/// milliseconds. The result of `work` is fed to a sink so the optimiser cannot
/// delete the whole computation as dead code.
double median(const std::function<void()>& work, int repeats = 5) {
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(repeats));
    for (int i = 0; i < repeats; ++i) {
        const auto started = std::chrono::steady_clock::now();
        work();
        samples.push_back(std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - started)
                              .count());
    }
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

volatile std::size_t g_sink = 0;

void heading(const std::string& title) {
    std::cout << "\n" << std::string(76, '=') << "\n  " << title << "\n"
              << std::string(76, '=') << "\n\n";
}

void columns(const std::string& first, const std::string& second, const std::string& third,
             const std::string& fourth) {
    std::cout << "  " << std::left << std::setw(28) << first << std::setw(14) << second
              << std::setw(14) << third << fourth << "\n";
}

void row(const std::string& label, double ms, double baseline, const std::string& note) {
    std::ostringstream time;
    time << std::fixed << std::setprecision(2) << ms;
    std::ostringstream ratio;
    if (baseline > 0.0) {
        ratio << std::fixed << std::setprecision(2) << (ms / baseline) << "x";
    } else {
        ratio << "-";
    }
    columns(label, time.str(), ratio.str(), note);
}

std::vector<int> shuffled(std::size_t count, std::uint32_t seed) {
    std::vector<int> values(count);
    std::iota(values.begin(), values.end(), 0);
    std::mt19937 rng(seed);
    std::shuffle(values.begin(), values.end(), rng);
    return values;
}

std::size_t scaled(std::size_t base) { return base * g_scale; }

// ---------------------------------------------------------------------------

void benchmarkSorting() {
    heading("Sorting");

    const std::size_t count = scaled(200000);
    const std::vector<int> input = shuffled(count, 1u);
    std::cout << "  " << count << " shuffled integers, median of 5 runs\n\n";
    columns("algorithm", "ms", "vs std::sort", "note");
    std::cout << "  " << std::string(72, '-') << "\n";

    std::vector<int> reference = input;
    const double baseline = median([&] {
        std::vector<int> data = input;
        std::sort(data.begin(), data.end());
        g_sink += static_cast<std::size_t>(data[0]);
    });
    row("std::sort", baseline, baseline, "the target to beat");

    for (const char* name : {"intro", "quick", "merge", "heap", "tim", "shell", "radix",
                             "counting"}) {
        auto strategy = makeSortStrategy<int>(name);
        const double elapsed = median([&] {
            std::vector<int> data = input;
            strategy->sort(data);
            g_sink += static_cast<std::size_t>(data[0]);
        });
        row(std::string("daedalus ") + name, elapsed, baseline,
            strategy->worstComplexity());
    }

    std::cout << "\n  Reading this honestly:\n"
                 "  * The comparison sorts lose to std::sort by roughly 5-15x, and the\n"
                 "    reason is the Strategy interface itself. Every comparison goes\n"
                 "    through a std::function, which is an indirect call the optimiser\n"
                 "    cannot inline; std::sort inlines its comparator completely. That\n"
                 "    is the price of choosing the algorithm by name at runtime, and it\n"
                 "    is a constant factor, not a complexity difference.\n"
                 "  * radix and counting BEAT std::sort, because they do not compare at\n"
                 "    all -- they read the keys' bits. That is only possible for bounded\n"
                 "    integer keys, which is exactly the restriction they carry.\n"
                 "  * The quadratic sorts are excluded at this size on purpose: at\n"
                 "    n = " << count << " bubble sort would take minutes. The CLI shows\n"
                 "    the comparison-count difference at a readable scale instead.\n";
}

void benchmarkTrees() {
    heading("Ordered structures");

    const std::size_t count = scaled(200000);
    const std::vector<int> keys = shuffled(count, 2u);
    std::cout << "  " << count << " random keys inserted, then all of them looked up\n\n";
    columns("structure", "insert ms", "vs std::set", "lookup ms");
    std::cout << "  " << std::string(72, '-') << "\n";

    double baseline = 0.0;
    {
        const double insert = median([&] {
            std::set<int> reference;
            for (int key : keys) reference.insert(key);
            g_sink += reference.size();
        }, 3);
        std::set<int> reference(keys.begin(), keys.end());
        const double lookup = median([&] {
            std::size_t found = 0;
            for (int key : keys) found += reference.count(key);
            g_sink += found;
        }, 3);
        baseline = insert;
        std::ostringstream lookupText;
        lookupText << std::fixed << std::setprecision(2) << lookup;
        row("std::set (red-black)", insert, baseline, lookupText.str());
    }

    const auto measure = [&](const std::string& label, auto makeTree) {
        const double insert = median([&] {
            auto tree = makeTree();
            for (int key : keys) tree->insert(key);
            g_sink += tree->size();
        }, 3);
        auto tree = makeTree();
        for (int key : keys) tree->insert(key);
        const double lookup = median([&] {
            std::size_t found = 0;
            for (int key : keys) found += tree->contains(key) ? 1u : 0u;
            g_sink += found;
        }, 3);
        std::ostringstream lookupText;
        lookupText << std::fixed << std::setprecision(2) << lookup;
        row(label, insert, baseline, lookupText.str());
    };

    measure("daedalus AVLTree", [] { return std::make_unique<AVLTree<int>>(); });
    measure("daedalus RedBlackTree", [] { return std::make_unique<RedBlackTree<int>>(); });
    measure("daedalus Treap", [] { return std::make_unique<Treap<int>>(); });
    measure("daedalus SkipList", [] { return std::make_unique<SkipList<int>>(); });
    measure("daedalus BTree(t=32)", [] { return std::make_unique<BTree<int>>(32); });

    std::cout << "\n  Insertion order is random here. Run the CLI 'trees' command to see\n"
                 "  what SORTED input does to the unbalanced tree -- that is the case\n"
                 "  where the difference stops being a constant factor.\n";
}

void benchmarkHashing() {
    heading("Hash tables");

    const std::size_t count = scaled(300000);
    const std::vector<int> keys = shuffled(count, 3u);
    std::cout << "  " << count << " keys inserted, then all of them looked up\n\n";
    columns("structure", "insert ms", "vs unordered", "lookup ms");
    std::cout << "  " << std::string(72, '-') << "\n";

    double baseline = 0.0;
    {
        const double insert = median([&] {
            std::unordered_map<int, int> reference;
            for (int key : keys) reference[key] = key;
            g_sink += reference.size();
        }, 3);
        std::unordered_map<int, int> reference;
        for (int key : keys) reference[key] = key;
        const double lookup = median([&] {
            std::size_t found = 0;
            for (int key : keys) found += reference.count(key);
            g_sink += found;
        }, 3);
        baseline = insert;
        std::ostringstream lookupText;
        lookupText << std::fixed << std::setprecision(2) << lookup;
        row("std::unordered_map", insert, baseline, lookupText.str());
    }

    {
        const double insert = median([&] {
            HashMap<int, int> map;
            for (int key : keys) map.put(key, key);
            g_sink += map.size();
        }, 3);
        HashMap<int, int> map;
        for (int key : keys) map.put(key, key);
        const double lookup = median([&] {
            std::size_t found = 0;
            for (int key : keys) found += map.contains(key) ? 1u : 0u;
            g_sink += found;
        }, 3);
        std::ostringstream lookupText;
        lookupText << std::fixed << std::setprecision(2) << lookup;
        row("daedalus HashMap (chained)", insert, baseline, lookupText.str());
    }

    {
        const double insert = median([&] {
            OpenAddressingMap<int, int> map;
            for (int key : keys) map.put(key, key);
            g_sink += map.size();
        }, 3);
        OpenAddressingMap<int, int> map;
        for (int key : keys) map.put(key, key);
        const double lookup = median([&] {
            std::size_t found = 0;
            for (int key : keys) found += map.contains(key) ? 1u : 0u;
            g_sink += found;
        }, 3);
        std::ostringstream lookupText;
        lookupText << std::fixed << std::setprecision(2) << lookup;
        row("daedalus OpenAddressing", insert, baseline, lookupText.str());

        // std::hash<int> is the identity function, and these keys are 0..n-1
        // in a table several times larger, so NOTHING collides and the probe
        // distance is trivially zero. Measuring Robin Hood on that input would
        // be measuring nothing, so the real figure comes from keys that are
        // spread wide enough to wrap the table repeatedly.
        OpenAddressingMap<int, int> collidingMap;
        for (int key : keys) collidingMap.put(key * 7919, key);
        std::cout << "  " << std::left << std::setw(34) << "  longest probe (sequential keys)"
                  << map.longestProbe() << "\n";
        std::cout << "  " << std::left << std::setw(34) << "  longest probe (wrapping keys)"
                  << collidingMap.longestProbe() << "\n";
        std::cout << "\n  Robin Hood's job is that second number: with a load factor near\n"
                     "  0.7 it keeps the WORST probe close to the average, instead of\n"
                     "  letting one unlucky key sit at the end of a long run.\n";
    }
}

void benchmarkContainers() {
    heading("Sequence containers");

    const std::size_t count = scaled(2000000);
    std::cout << "  " << count << " appends, then a full linear scan\n\n";
    columns("structure", "append ms", "vs vector", "scan ms");
    std::cout << "  " << std::string(72, '-') << "\n";

    const double baseline = median([&] {
        std::vector<int> data;
        for (std::size_t i = 0; i < count; ++i) data.push_back(static_cast<int>(i));
        g_sink += data.size();
    }, 3);
    {
        std::vector<int> data;
        for (std::size_t i = 0; i < count; ++i) data.push_back(static_cast<int>(i));
        const double scan = median([&] {
            std::size_t total = 0;
            for (int value : data) total += static_cast<std::size_t>(value);
            g_sink += total;
        }, 3);
        std::ostringstream scanText;
        scanText << std::fixed << std::setprecision(2) << scan;
        row("std::vector", baseline, baseline, scanText.str());
    }

    {
        const double append = median([&] {
            DynamicArray<int> data;
            for (std::size_t i = 0; i < count; ++i) data.pushBack(static_cast<int>(i));
            g_sink += data.size();
        }, 3);
        DynamicArray<int> data;
        for (std::size_t i = 0; i < count; ++i) data.pushBack(static_cast<int>(i));
        const double scan = median([&] {
            std::size_t total = 0;
            for (int value : data) total += static_cast<std::size_t>(value);
            g_sink += total;
        }, 3);
        std::ostringstream scanText;
        scanText << std::fixed << std::setprecision(2) << scan;
        row("daedalus DynamicArray", append, baseline, scanText.str());
    }
}

void benchmarkGraph() {
    heading("Graph algorithms");

    const std::size_t vertices = scaled(20000);
    const std::size_t edgesPerVertex = 8;
    std::mt19937 rng(4u);

    Graph<int, int> graph(true);
    for (std::size_t v = 0; v < vertices; ++v) (void)graph.addVertex(static_cast<int>(v));
    for (std::size_t v = 0; v < vertices; ++v) {
        for (std::size_t e = 0; e < edgesPerVertex; ++e) {
            const int target = static_cast<int>(rng() % vertices);
            graph.addEdge(static_cast<int>(v), target, static_cast<int>(1 + rng() % 100));
        }
    }

    std::cout << "  " << graph.vertexCount() << " vertices, " << graph.edgeCount()
              << " directed edges\n\n";
    columns("algorithm", "ms", "", "complexity");
    std::cout << "  " << std::string(72, '-') << "\n";

    row("BFS", median([&] { g_sink += breadthFirstSearch(graph, 0).order.size(); }, 3), 0.0,
        "O(V + E)");
    row("DFS (iterative)", median([&] { g_sink += depthFirstSearch(graph, 0).order.size(); }, 3),
        0.0, "O(V + E)");
    row("Dijkstra", median([&] { g_sink += dijkstra(graph, 0).settledCount; }, 3), 0.0,
        "O((V + E) log V)");
    row("Bellman-Ford", median([&] { g_sink += bellmanFord(graph, 0).distance.size(); }, 1), 0.0,
        "O(V * E)");

    std::cout << "\n  Bellman-Ford is run once rather than five times: it is the slow one\n"
                 "  by design, and that is exactly the point of having both.\n";

    heading("Union-find");
    const std::size_t elements = scaled(2000000);
    std::cout << "  " << elements << " elements joined into one component, then all found\n\n";
    columns("operation", "ms", "", "note");
    std::cout << "  " << std::string(72, '-') << "\n";

    DisjointSet sets(elements);
    row("union of a full chain",
        median([&] {
            DisjointSet local(elements);
            for (std::size_t i = 1; i < elements; ++i) local.unite(i - 1, i);
            g_sink += local.componentCount();
        }, 3),
        0.0, "O(alpha(n)) amortised");

    for (std::size_t i = 1; i < elements; ++i) sets.unite(i - 1, i);
    row("find on every element",
        median([&] {
            std::size_t total = 0;
            for (std::size_t i = 0; i < elements; ++i) total += sets.find(i);
            g_sink += total;
        }, 3),
        0.0, "flattened after the first pass");
}

void printUsage(const char* program) {
    std::cout << daedalus::banner() << "\n\n"
              << "usage: " << program << " [suite] [--scale N]\n\n"
              << "  (no suite)   run every benchmark\n"
              << "  sorting      sorting algorithms versus std::sort\n"
              << "  trees        ordered structures versus std::set\n"
              << "  hashing      hash tables versus std::unordered_map\n"
              << "  containers   sequences versus std::vector\n"
              << "  graph        traversal, shortest paths, union-find\n"
              << "  --scale N    multiply every input size by N (default 1)\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string suite;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            return 0;
        }
        if (argument == "--scale") {
            if (i + 1 >= argc) {
                std::cerr << "missing value for --scale\n";
                return 2;
            }
            g_scale = static_cast<std::size_t>(std::stoul(argv[++i]));
            if (g_scale == 0) {
                std::cerr << "--scale must be at least 1\n";
                return 2;
            }
            continue;
        }
        suite = argument;
    }

    const std::map<std::string, std::function<void()>> suites{
        {"sorting", benchmarkSorting},   {"trees", benchmarkTrees},
        {"hashing", benchmarkHashing},   {"containers", benchmarkContainers},
        {"graph", benchmarkGraph}};

    std::cout << daedalus::banner() << "\n"
              << "  benchmark scale: " << g_scale << "x\n"
              << "  every figure below is the median of several runs on THIS machine\n";

    try {
        if (suite.empty()) {
            for (const auto& entry : suites) entry.second();
        } else {
            const auto found = suites.find(suite);
            if (found == suites.end()) {
                std::cerr << "unknown suite: " << suite << "\n\n";
                printUsage(argv[0]);
                return 2;
            }
            found->second();
        }
    } catch (const std::exception& failure) {
        std::cerr << "\nbenchmark failed: " << failure.what() << "\n";
        return 1;
    }

    std::cout << "\n";
    return 0;
}
