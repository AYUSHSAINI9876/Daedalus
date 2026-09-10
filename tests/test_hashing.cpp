// ============================================================================
//  Unit tests for the hashing layer and union-find.
// ============================================================================
#include <algorithm>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "daedalus/hashing/BloomFilter.hpp"
#include "daedalus/hashing/Cache.hpp"
#include "daedalus/hashing/HashMap.hpp"
#include "daedalus/hashing/HashSet.hpp"
#include "daedalus/hashing/OpenAddressingMap.hpp"
#include "daedalus/sets/DisjointSet.hpp"
#include "framework/TestFramework.hpp"

using namespace daedalus;

namespace {

/// A deliberately terrible hash: only eight distinct hash values exist, so no
/// choice of table size can spread the keys out.
///
/// Note that the obvious "bad" hash -- multiplying by 8 -- is NOT bad here,
/// because 8 is coprime with the prime table sizes and the modulus undoes it
/// perfectly. Collapsing the range is what actually defeats the table.
struct ClusteringHash {
    std::size_t operator()(int key) const { return static_cast<std::size_t>(key % 8); }
};

std::vector<int> sortedCopy(std::vector<int> values) {
    std::sort(values.begin(), values.end());
    return values;
}

}  // namespace

// ============================================================================
//  HashMap (separate chaining)
// ============================================================================

DAEDALUS_TEST(HashMap, put_get_and_overwrite) {
    HashMap<std::string, int> map;
    map.put("one", 1);
    map.put("two", 2);
    CHECK_EQ(map.size(), 2u);
    CHECK_EQ(map.get("one").value(), 1);
    CHECK_TRUE(map.contains("two"));
    CHECK_FALSE(map.get("three").has_value());

    map.put("one", 100);
    CHECK_EQ(map.size(), 2u);           // an overwrite must not grow the map
    CHECK_EQ(map.get("one").value(), 100);
}

DAEDALUS_TEST(HashMap, at_throws_for_missing_keys) {
    HashMap<std::string, int> map{{"a", 1}};
    CHECK_EQ(map.at("a"), 1);
    map.at("a") = 5;
    CHECK_EQ(map.get("a").value(), 5);
    CHECK_THROWS_AS(map.at("missing"), KeyNotFound);
    CHECK_THROWS_AS(map.at("missing"), LookupError);
}

DAEDALUS_TEST(HashMap, subscript_default_constructs) {
    HashMap<std::string, int> counts;
    ++counts["apple"];
    ++counts["apple"];
    ++counts["pear"];
    CHECK_EQ(counts["apple"], 2);
    CHECK_EQ(counts["pear"], 1);
    CHECK_EQ(counts.size(), 2u);
}

DAEDALUS_TEST(HashMap, erase_and_clear) {
    HashMap<int, int> map;
    for (int i = 0; i < 50; ++i) map.put(i, i * i);
    CHECK_TRUE(map.erase(25));
    CHECK_FALSE(map.erase(25));
    CHECK_EQ(map.size(), 49u);
    CHECK_FALSE(map.contains(25));
    map.clear();
    CHECK_TRUE(map.empty());
    CHECK_FALSE(map.contains(1));
}

DAEDALUS_TEST(HashMap, grows_and_rehashes_without_losing_entries) {
    HashMap<int, int> map;
    const std::size_t initialBuckets = map.bucketCount();
    for (int i = 0; i < 5000; ++i) map.put(i, i + 7);
    CHECK_EQ(map.size(), 5000u);
    CHECK_LT(initialBuckets, map.bucketCount());
    CHECK_LT(map.loadFactor(), map.maxLoadFactor());
    for (int i = 0; i < 5000; ++i) CHECK_EQ(map.get(i).value(), i + 7);
}

DAEDALUS_TEST(HashMap, matches_std_unordered_map_under_random_load) {
    HashMap<int, int> map;
    std::unordered_map<int, int> reference;
    std::mt19937 rng(1234u);
    std::uniform_int_distribution<int> keys(0, 500);

    for (int step = 0; step < 10000; ++step) {
        const int key = keys(rng);
        const int value = step;
        if ((rng() & 3u) != 0u) {
            map.put(key, value);
            reference[key] = value;
        } else {
            CHECK_EQ(map.erase(key), reference.erase(key) > 0);
        }
    }
    CHECK_EQ(map.size(), reference.size());
    for (const auto& entry : reference) CHECK_EQ(map.get(entry.first).value(), entry.second);
}

DAEDALUS_TEST(HashMap, enumerates_keys_values_and_entries) {
    HashMap<int, int> map{{1, 10}, {2, 20}, {3, 30}};
    CHECK_EQ(sortedCopy(map.keys()), (std::vector<int>{1, 2, 3}));
    CHECK_EQ(sortedCopy(map.values()), (std::vector<int>{10, 20, 30}));
    CHECK_EQ(map.entries().size(), 3u);
    CHECK_TRUE(map.toString().rfind("HashMap", 0) == 0);
}

DAEDALUS_TEST(HashMap, a_bad_hash_shows_up_in_the_statistics) {
    HashMap<int, int, ClusteringHash> clustered;
    HashMap<int, int> healthy;
    for (int i = 0; i < 400; ++i) {
        clustered.put(i, i);
        healthy.put(i, i);
    }
    const HashStatistics bad = clustered.statistics();
    const HashStatistics good = healthy.statistics();

    // Both still answer correctly -- chaining degrades, it does not break.
    CHECK_EQ(clustered.get(399).value(), 399);
    CHECK_EQ(clustered.size(), 400u);
    // But the clustered table piles keys into far fewer buckets.
    CHECK_LT(good.longestChain, bad.longestChain);
    CHECK_LT(bad.usedBuckets, good.usedBuckets);
}

DAEDALUS_TEST(HashMap, reserve_avoids_rehashing) {
    HashMap<int, int> map;
    map.reserve(1000);
    const std::size_t buckets = map.bucketCount();
    for (int i = 0; i < 700; ++i) map.put(i, i);
    CHECK_EQ(map.bucketCount(), buckets);
    CHECK_THROWS_AS(map.setMaxLoadFactor(0.0), InvalidArgument);
}

// ============================================================================
//  OpenAddressingMap (Robin Hood)
// ============================================================================

DAEDALUS_TEST(HashMap, open_addressing_basic_operations) {
    OpenAddressingMap<std::string, int> map;
    map.put("alpha", 1);
    map.put("beta", 2);
    map.put("gamma", 3);
    CHECK_EQ(map.size(), 3u);
    CHECK_EQ(map.get("beta").value(), 2);
    map.put("beta", 20);
    CHECK_EQ(map.size(), 3u);
    CHECK_EQ(map.at("beta"), 20);
    CHECK_TRUE(map.erase("beta"));
    CHECK_FALSE(map.contains("beta"));
    CHECK_FALSE(map.erase("beta"));
    CHECK_EQ(map.size(), 2u);
    CHECK_THROWS_AS(map.at("beta"), KeyNotFound);
}

DAEDALUS_TEST(HashMap, open_addressing_backward_shift_keeps_lookups_working) {
    // Every key hashes into the same run, so deleting from the middle is the
    // case that tombstone-free deletion has to get right.
    OpenAddressingMap<int, int, ClusteringHash> map;
    for (int i = 0; i < 60; ++i) map.put(i, i * 2);
    for (int i = 0; i < 60; i += 2) CHECK_TRUE(map.erase(i));
    for (int i = 1; i < 60; i += 2) CHECK_EQ(map.get(i).value(), i * 2);
    for (int i = 0; i < 60; i += 2) CHECK_FALSE(map.contains(i));
    CHECK_EQ(map.size(), 30u);
}

DAEDALUS_TEST(HashMap, open_addressing_matches_reference_under_random_load) {
    OpenAddressingMap<int, int> map;
    std::unordered_map<int, int> reference;
    std::mt19937 rng(777u);
    std::uniform_int_distribution<int> keys(0, 400);

    for (int step = 0; step < 8000; ++step) {
        const int key = keys(rng);
        if ((rng() & 3u) != 0u) {
            map.put(key, step);
            reference[key] = step;
        } else {
            CHECK_EQ(map.erase(key), reference.erase(key) > 0);
        }
    }
    CHECK_EQ(map.size(), reference.size());
    for (const auto& entry : reference) CHECK_EQ(map.get(entry.first).value(), entry.second);
}

DAEDALUS_TEST(HashMap, robin_hood_keeps_probe_distances_short) {
    OpenAddressingMap<int, int> map;
    for (int i = 0; i < 3000; ++i) map.put(i * 7919, i);
    CHECK_EQ(map.size(), 3000u);
    CHECK_LT(map.loadFactor(), 0.7);
    // Robin Hood's whole purpose: no key ends up far from its home slot.
    CHECK_LE(map.longestProbe(), 32u);
}

// ============================================================================
//  HashSet
// ============================================================================

DAEDALUS_TEST(HashSet, membership_and_duplicates) {
    HashSet<int> set{1, 2, 3, 2, 1};
    CHECK_EQ(set.size(), 3u);
    CHECK_TRUE(set.contains(2));
    CHECK_FALSE(set.contains(9));
    CHECK_TRUE(set.erase(2));
    CHECK_FALSE(set.erase(2));
    CHECK_EQ(set.size(), 2u);
}

DAEDALUS_TEST(HashSet, set_algebra) {
    HashSet<int> a{1, 2, 3, 4};
    HashSet<int> b{3, 4, 5, 6};

    CHECK_EQ(sortedCopy(a.unionWith(b).toVector()), (std::vector<int>{1, 2, 3, 4, 5, 6}));
    CHECK_EQ(sortedCopy(a.intersectionWith(b).toVector()), (std::vector<int>{3, 4}));
    CHECK_EQ(sortedCopy(a.differenceWith(b).toVector()), (std::vector<int>{1, 2}));
    CHECK_EQ(sortedCopy(a.symmetricDifferenceWith(b).toVector()),
             (std::vector<int>{1, 2, 5, 6}));
}

DAEDALUS_TEST(HashSet, subset_and_disjoint) {
    HashSet<int> whole{1, 2, 3, 4};
    HashSet<int> part{2, 3};
    HashSet<int> apart{7, 8};

    CHECK_TRUE(part.isSubsetOf(whole));
    CHECK_FALSE(whole.isSubsetOf(part));
    CHECK_TRUE(whole.isDisjointFrom(apart));
    CHECK_FALSE(whole.isDisjointFrom(part));
    CHECK_TRUE(HashSet<int>().isSubsetOf(whole));
    CHECK_TRUE(whole == HashSet<int>({4, 3, 2, 1}));
}

DAEDALUS_TEST(HashSet, works_with_strings_and_scales) {
    HashSet<std::string> set;
    for (int i = 0; i < 2000; ++i) set.insert("key-" + std::to_string(i));
    CHECK_EQ(set.size(), 2000u);
    CHECK_TRUE(set.contains("key-1999"));
    CHECK_FALSE(set.contains("key-2000"));
}

// ============================================================================
//  BloomFilter
// ============================================================================

DAEDALUS_TEST(BloomFilter, never_produces_a_false_negative) {
    BloomFilter<std::string> filter(1000, 0.01);
    std::vector<std::string> inserted;
    for (int i = 0; i < 1000; ++i) {
        const std::string key = "user:" + std::to_string(i);
        filter.add(key);
        inserted.push_back(key);
    }
    // The defining guarantee: everything added must still report as present.
    for (const std::string& key : inserted) CHECK_TRUE(filter.mightContain(key));
    CHECK_EQ(filter.insertedCount(), 1000u);
}

DAEDALUS_TEST(BloomFilter, false_positive_rate_is_near_the_target) {
    BloomFilter<std::string> filter(2000, 0.01);
    for (int i = 0; i < 2000; ++i) filter.add("in:" + std::to_string(i));

    std::size_t falsePositives = 0;
    const int probes = 20000;
    for (int i = 0; i < probes; ++i) {
        if (filter.mightContain("out:" + std::to_string(i))) ++falsePositives;
    }
    const double observed = static_cast<double>(falsePositives) / probes;
    // Target is 1%; allow generous slack so the test is not flaky, but a
    // broken filter (0% or 100%) still fails loudly.
    CHECK_LT(observed, 0.05);
    CHECK_LT(filter.estimatedFalsePositiveRate(), 0.05);
}

DAEDALUS_TEST(BloomFilter, uses_far_less_memory_than_storing_the_keys) {
    BloomFilter<std::string> filter(100000, 0.01);
    // 100k keys at 1% needs about 120 kB of bits; the strings alone would be
    // megabytes.
    CHECK_LT(filter.memoryBytes(), 200u * 1024u);
    CHECK_LT(0u, filter.hashCount());
    CHECK_LE(filter.hashCount(), 16u);
}

DAEDALUS_TEST(BloomFilter, clear_and_argument_validation) {
    BloomFilter<int> filter(100, 0.05);
    filter.add(42);
    CHECK_TRUE(filter.mightContain(42));
    CHECK_FALSE(filter.empty());
    filter.clear();
    CHECK_TRUE(filter.empty());
    CHECK_FALSE(filter.mightContain(42));
    CHECK_NEAR(filter.fillRatio(), 0.0, 1e-9);

    CHECK_THROWS_AS(BloomFilter<int>(0, 0.01), InvalidArgument);
    CHECK_THROWS_AS(BloomFilter<int>(10, 0.0), InvalidArgument);
    CHECK_THROWS_AS(BloomFilter<int>(10, 1.0), InvalidArgument);
}

// ============================================================================
//  Caches
// ============================================================================

DAEDALUS_TEST(LRUCache, evicts_the_least_recently_used) {
    LRUCache<int, std::string> cache(3);
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    CHECK_EQ(cache.get(1).value(), std::string("one"));   // 1 becomes most recent
    cache.put(4, "four");                                 // evicts 2, not 1

    CHECK_TRUE(cache.contains(1));
    CHECK_FALSE(cache.contains(2));
    CHECK_TRUE(cache.contains(3));
    CHECK_TRUE(cache.contains(4));
    CHECK_EQ(cache.size(), 3u);
    CHECK_EQ(cache.statistics().evictions, 1u);
}

DAEDALUS_TEST(LRUCache, updating_a_key_refreshes_recency) {
    LRUCache<int, int> cache(2);
    cache.put(1, 1);
    cache.put(2, 2);
    cache.put(1, 100);      // 1 is now the most recent
    cache.put(3, 3);        // evicts 2
    CHECK_FALSE(cache.contains(2));
    CHECK_EQ(cache.keysByEvictionOrder(), (std::vector<int>{3, 1}));
    CHECK_EQ(cache.get(1).value(), 100);
    // That read promotes 1, so 3 is now the next thing to be evicted.
    CHECK_EQ(cache.keysByEvictionOrder(), (std::vector<int>{1, 3}));
}

DAEDALUS_TEST(LRUCache, statistics_track_hits_and_misses) {
    LRUCache<int, int> cache(2);
    cache.put(1, 1);
    CHECK_TRUE(cache.get(1).has_value());
    CHECK_FALSE(cache.get(9).has_value());
    CHECK_EQ(cache.statistics().hits, 1u);
    CHECK_EQ(cache.statistics().misses, 1u);
    CHECK_NEAR(cache.statistics().hitRate(), 0.5, 1e-9);
    cache.resetStatistics();
    CHECK_EQ(cache.statistics().lookups(), 0u);
}

DAEDALUS_TEST(LRUCache, erase_clear_and_validation) {
    LRUCache<int, int> cache(4);
    for (int i = 0; i < 4; ++i) cache.put(i, i);
    CHECK_TRUE(cache.erase(2));
    CHECK_FALSE(cache.erase(2));
    CHECK_EQ(cache.size(), 3u);
    cache.clear();
    CHECK_TRUE(cache.empty());
    CHECK_THROWS_AS((LRUCache<int, int>(0)), InvalidArgument);
}

DAEDALUS_TEST(LFUCache, evicts_the_least_frequently_used) {
    LFUCache<int, std::string> cache(3);
    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");
    (void)cache.get(1);
    (void)cache.get(1);
    (void)cache.get(2);
    cache.put(4, "four");     // 3 has frequency 1, the lowest, so it goes

    CHECK_FALSE(cache.contains(3));
    CHECK_TRUE(cache.contains(1));
    CHECK_TRUE(cache.contains(2));
    CHECK_TRUE(cache.contains(4));
    CHECK_EQ(cache.frequencyOf(1), 3u);   // one put plus two gets
    CHECK_EQ(cache.frequencyOf(2), 2u);
}

DAEDALUS_TEST(LFUCache, ties_are_broken_by_recency) {
    LFUCache<int, int> cache(2);
    cache.put(1, 1);
    cache.put(2, 2);          // both have frequency 1; 1 is the older
    cache.put(3, 3);          // so 1 is evicted
    CHECK_FALSE(cache.contains(1));
    CHECK_TRUE(cache.contains(2));
    CHECK_TRUE(cache.contains(3));
}

DAEDALUS_TEST(LFUCache, erase_and_statistics) {
    LFUCache<int, int> cache(3);
    cache.put(1, 1);
    CHECK_TRUE(cache.get(1).has_value());
    CHECK_FALSE(cache.get(2).has_value());
    CHECK_EQ(cache.statistics().hits, 1u);
    CHECK_EQ(cache.statistics().misses, 1u);
    CHECK_TRUE(cache.erase(1));
    CHECK_FALSE(cache.erase(1));
    CHECK_TRUE(cache.empty());
    CHECK_EQ(cache.frequencyOf(1), 0u);
    CHECK_THROWS_AS((LFUCache<int, int>(0)), InvalidArgument);
}

DAEDALUS_TEST(LFUCache, policies_are_interchangeable_through_the_interface) {
    std::vector<std::unique_ptr<Cache<int, int>>> caches;
    caches.push_back(std::make_unique<LRUCache<int, int>>(16));
    caches.push_back(std::make_unique<LFUCache<int, int>>(16));

    for (auto& cache : caches) {
        for (int i = 0; i < 100; ++i) cache->put(i % 20, i);
        CHECK_EQ(cache->size(), 16u);
        CHECK_EQ(cache->capacity(), 16u);
        CHECK_FALSE(cache->name().empty());
        CHECK_EQ(cache->keysByEvictionOrder().size(), 16u);
        cache->clear();
        CHECK_TRUE(cache->empty());
    }
}

DAEDALUS_TEST(LFUCache, hot_key_workload_favours_LFU_over_LRU) {
    // A tiny hot set queried forever, interleaved with a cold scan LONGER than
    // the cache. The scan length is the whole point: it flushes every hot entry
    // out of an LRU before the next round can read them, while LFU keeps them
    // because their frequency dwarfs the scan's.
    constexpr int kCapacity = 10;
    constexpr int kHotKeys = 4;
    constexpr int kScanLength = 20;   // deliberately > kCapacity

    LRUCache<int, int> lru(kCapacity);
    LFUCache<int, int> lfu(kCapacity);

    // Warm the hot set so LFU has frequency to work with.
    for (int warm = 0; warm < 50; ++warm) {
        for (int hot = 0; hot < kHotKeys; ++hot) {
            if (!lru.get(hot).has_value()) lru.put(hot, hot);
            if (!lfu.get(hot).has_value()) lfu.put(hot, hot);
        }
    }
    lru.resetStatistics();
    lfu.resetStatistics();

    for (int round = 0; round < 100; ++round) {
        for (int hot = 0; hot < kHotKeys; ++hot) {
            if (!lru.get(hot).has_value()) lru.put(hot, hot);
            if (!lfu.get(hot).has_value()) lfu.put(hot, hot);
        }
        for (int i = 0; i < kScanLength; ++i) {
            const int cold = 1000 + round * kScanLength + i;
            if (!lru.get(cold).has_value()) lru.put(cold, cold);
            if (!lfu.get(cold).has_value()) lfu.put(cold, cold);
        }
    }
    CHECK_LT(lru.statistics().hitRate(), lfu.statistics().hitRate());
    CHECK_LT(0.0, lfu.statistics().hitRate());
}

// ============================================================================
//  DisjointSet
// ============================================================================

DAEDALUS_TEST(DisjointSet, starts_fully_disconnected) {
    DisjointSet sets(5);
    CHECK_EQ(sets.componentCount(), 5u);
    CHECK_EQ(sets.size(), 5u);
    for (std::size_t i = 0; i < 5; ++i) CHECK_EQ(sets.find(i), i);
    CHECK_FALSE(sets.connected(0, 1));
}

DAEDALUS_TEST(DisjointSet, union_merges_and_reports_novelty) {
    DisjointSet sets(6);
    CHECK_TRUE(sets.unite(0, 1));
    CHECK_TRUE(sets.unite(2, 3));
    CHECK_TRUE(sets.unite(1, 2));
    CHECK_FALSE(sets.unite(0, 3));       // already connected: the cycle test
    CHECK_EQ(sets.componentCount(), 3u);
    CHECK_TRUE(sets.connected(0, 3));
    CHECK_FALSE(sets.connected(0, 4));
    CHECK_EQ(sets.componentSizeOf(0), 4u);
    CHECK_EQ(sets.largestComponentSize(), 4u);
}

DAEDALUS_TEST(DisjointSet, components_are_grouped) {
    DisjointSet sets(7);
    sets.unite(0, 1);
    sets.unite(1, 2);
    sets.unite(4, 5);
    const auto groups = sets.components();
    CHECK_EQ(groups.size(), 4u);
    CHECK_EQ(groups[0], (std::vector<std::size_t>{0, 1, 2}));
    CHECK_EQ(groups[1], (std::vector<std::size_t>{3}));
    CHECK_EQ(groups[2], (std::vector<std::size_t>{4, 5}));
    CHECK_EQ(groups[3], (std::vector<std::size_t>{6}));
}

DAEDALUS_TEST(DisjointSet, path_compression_keeps_trees_flat) {
    // A chain union is the adversarial order for a naive implementation.
    const std::size_t n = 100000;
    DisjointSet sets(n);
    for (std::size_t i = 1; i < n; ++i) sets.unite(i - 1, i);
    for (std::size_t i = 0; i < n; ++i) (void)sets.find(i);
    // After compression every element points straight at the root.
    CHECK_LE(sets.maximumDepth(), 1u);
    CHECK_EQ(sets.componentCount(), 1u);
}

DAEDALUS_TEST(DisjointSet, const_lookup_and_bounds) {
    DisjointSet sets(3);
    sets.unite(0, 1);
    CHECK_EQ(sets.findWithoutCompression(1), sets.findWithoutCompression(0));
    CHECK_THROWS_AS(sets.find(3), IndexOutOfRange);
    CHECK_THROWS_AS(sets.findWithoutCompression(99), IndexOutOfRange);
    sets.reset();
    CHECK_EQ(sets.componentCount(), 3u);
    CHECK_FALSE(sets.connected(0, 1));
}
