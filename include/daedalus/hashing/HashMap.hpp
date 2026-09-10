// ============================================================================
//  Daedalus :: hashing/HashMap.hpp
//
//  Hash map with separate chaining: each bucket owns a small array of entries,
//  and the table doubles once the load factor is exceeded. Chaining degrades
//  gracefully -- a bad hash costs long chains, not the clustering death spiral
//  that open addressing suffers -- which is why it is the default here and
//  OpenAddressingMap is offered alongside for the cache-friendly case.
//
//  Bucket counts are kept prime. With a power-of-two table a hash whose low
//  bits are constant collides on every key; a prime modulus mixes the high bits
//  back in, and loadFactorStatistics() makes the difference visible.
//
//  Complexity: get/put/erase O(1) expected, O(n) worst | space O(n)
//              rehash O(n), amortised to O(1) per insert
// ============================================================================
#ifndef DAEDALUS_HASHING_HASH_MAP_HPP
#define DAEDALUS_HASHING_HASH_MAP_HPP

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"
#include "daedalus/linear/DynamicArray.hpp"

namespace daedalus {

/// Table sizes: each roughly doubles the previous one and is prime.
inline constexpr std::size_t kHashTableSizes[] = {
    11u,      23u,       47u,       97u,       197u,     397u,     797u,
    1597u,    3203u,     6421u,     12853u,    25717u,   51437u,   102877u,
    205759u,  411527u,   823117u,   1646237u,  3292489u, 6584983u, 13169977u};

[[nodiscard]] inline std::size_t nextHashTableSize(std::size_t atLeast) {
    for (std::size_t candidate : kHashTableSizes) {
        if (candidate >= atLeast) return candidate;
    }
    return kHashTableSizes[sizeof(kHashTableSizes) / sizeof(kHashTableSizes[0]) - 1];
}

/// Bucket-occupancy report, used by the tests and by the CLI demo.
struct HashStatistics {
    std::size_t bucketCount{0};
    std::size_t usedBuckets{0};
    std::size_t longestChain{0};
    double loadFactor{0.0};
    double averageChain{0.0};
};

template <typename K, typename V, typename Hash = std::hash<K>>
    requires Hashable<K>
class HashMap final : public Map<K, V> {
public:
    using key_type = K;
    using mapped_type = V;
    using value_type = std::pair<K, V>;
    using size_type = std::size_t;

    static constexpr double kDefaultMaxLoadFactor = 0.75;

    HashMap() : buckets_(nextHashTableSize(11)) {}

    explicit HashMap(size_type expectedElements, Hash hash = Hash())
        : hash_(std::move(hash)),
          buckets_(nextHashTableSize(
              static_cast<size_type>(static_cast<double>(expectedElements) /
                                     kDefaultMaxLoadFactor) + 1)) {}

    HashMap(std::initializer_list<value_type> entries) : HashMap() {
        for (const auto& entry : entries) put(entry.first, entry.second);
    }

    // --- observers -----------------------------------------------------------

    [[nodiscard]] size_type size() const noexcept override { return size_; }
    [[nodiscard]] bool empty() const noexcept override { return size_ == 0; }
    [[nodiscard]] std::string name() const override { return "HashMap"; }

    [[nodiscard]] size_type bucketCount() const noexcept { return buckets_.size(); }

    [[nodiscard]] double loadFactor() const noexcept {
        return static_cast<double>(size_) / static_cast<double>(buckets_.size());
    }

    [[nodiscard]] double maxLoadFactor() const noexcept { return maxLoadFactor_; }

    void setMaxLoadFactor(double factor) {
        require(factor > 0.0, "max load factor must be positive");
        maxLoadFactor_ = factor;
        rehashIfNeeded();
    }

    void clear() override {
        for (size_type i = 0; i < buckets_.size(); ++i) buckets_[i].clear();
        size_ = 0;
    }

    // --- lookup --------------------------------------------------------------

    [[nodiscard]] std::optional<V> get(const K& key) const override {
        const auto& bucket = buckets_[bucketFor(key)];
        for (size_type i = 0; i < bucket.size(); ++i) {
            if (bucket[i].first == key) return bucket[i].second;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool contains(const K& key) const override {
        const auto& bucket = buckets_[bucketFor(key)];
        for (size_type i = 0; i < bucket.size(); ++i) {
            if (bucket[i].first == key) return true;
        }
        return false;
    }

    /// Reference to the mapped value; throws KeyNotFound when absent.
    [[nodiscard]] V& at(const K& key) {
        auto& bucket = buckets_[bucketFor(key)];
        for (size_type i = 0; i < bucket.size(); ++i) {
            if (bucket[i].first == key) return bucket[i].second;
        }
        throw KeyNotFound(formatElement(key));
    }

    [[nodiscard]] const V& at(const K& key) const {
        const auto& bucket = buckets_[bucketFor(key)];
        for (size_type i = 0; i < bucket.size(); ++i) {
            if (bucket[i].first == key) return bucket[i].second;
        }
        throw KeyNotFound(formatElement(key));
    }

    /// Default-constructs a value for a missing key, like std::map does.
    [[nodiscard]] V& operator[](const K& key) {
        rehashIfNeeded();
        auto& bucket = buckets_[bucketFor(key)];
        for (size_type i = 0; i < bucket.size(); ++i) {
            if (bucket[i].first == key) return bucket[i].second;
        }
        bucket.pushBack(value_type{key, V{}});
        ++size_;
        return bucket.back().second;
    }

    // --- mutation ------------------------------------------------------------

    void put(const K& key, const V& value) override {
        rehashIfNeeded();
        auto& bucket = buckets_[bucketFor(key)];
        for (size_type i = 0; i < bucket.size(); ++i) {
            if (bucket[i].first == key) {
                bucket[i].second = value;
                return;
            }
        }
        bucket.pushBack(value_type{key, value});
        ++size_;
    }

    bool erase(const K& key) override {
        auto& bucket = buckets_[bucketFor(key)];
        for (size_type i = 0; i < bucket.size(); ++i) {
            if (bucket[i].first == key) {
                bucket.eraseAt(i);
                --size_;
                return true;
            }
        }
        return false;
    }

    /// Grows the table to hold at least `expectedElements` under the current
    /// load factor, rehashing every entry.
    void reserve(size_type expectedElements) {
        const size_type wanted = nextHashTableSize(
            static_cast<size_type>(static_cast<double>(expectedElements) / maxLoadFactor_) + 1);
        if (wanted > buckets_.size()) rehash(wanted);
    }

    // --- enumeration ---------------------------------------------------------

    [[nodiscard]] std::vector<K> keys() const override {
        std::vector<K> out;
        out.reserve(size_);
        for (size_type b = 0; b < buckets_.size(); ++b) {
            for (size_type i = 0; i < buckets_[b].size(); ++i) out.push_back(buckets_[b][i].first);
        }
        return out;
    }

    [[nodiscard]] std::vector<V> values() const {
        std::vector<V> out;
        out.reserve(size_);
        for (size_type b = 0; b < buckets_.size(); ++b) {
            for (size_type i = 0; i < buckets_[b].size(); ++i) out.push_back(buckets_[b][i].second);
        }
        return out;
    }

    [[nodiscard]] std::vector<value_type> entries() const override {
        std::vector<value_type> out;
        out.reserve(size_);
        for (size_type b = 0; b < buckets_.size(); ++b) {
            for (size_type i = 0; i < buckets_[b].size(); ++i) out.push_back(buckets_[b][i]);
        }
        return out;
    }

    /// Chain-length report. A healthy table has a longest chain in the low
    /// single digits; a pathological hash shows up here immediately.
    [[nodiscard]] HashStatistics statistics() const {
        HashStatistics stats;
        stats.bucketCount = buckets_.size();
        for (size_type b = 0; b < buckets_.size(); ++b) {
            const size_type length = buckets_[b].size();
            if (length > 0) ++stats.usedBuckets;
            if (length > stats.longestChain) stats.longestChain = length;
        }
        stats.loadFactor = loadFactor();
        stats.averageChain =
            stats.usedBuckets == 0
                ? 0.0
                : static_cast<double>(size_) / static_cast<double>(stats.usedBuckets);
        return stats;
    }

private:
    [[nodiscard]] size_type bucketFor(const K& key) const {
        return hash_(key) % buckets_.size();
    }

    void rehashIfNeeded() {
        if (loadFactor() < maxLoadFactor_) return;
        rehash(nextHashTableSize(buckets_.size() + 1));
    }

    void rehash(size_type newBucketCount) {
        DynamicArray<DynamicArray<value_type>> fresh(newBucketCount);
        for (size_type b = 0; b < buckets_.size(); ++b) {
            for (size_type i = 0; i < buckets_[b].size(); ++i) {
                const value_type& entry = buckets_[b][i];
                fresh[hash_(entry.first) % newBucketCount].pushBack(entry);
            }
        }
        buckets_ = std::move(fresh);
    }

    Hash hash_{};
    DynamicArray<DynamicArray<value_type>> buckets_;
    size_type size_{0};
    double maxLoadFactor_{kDefaultMaxLoadFactor};
};

}  // namespace daedalus

#endif  // DAEDALUS_HASHING_HASH_MAP_HPP
