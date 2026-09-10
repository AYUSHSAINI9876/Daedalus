// ============================================================================
//  Daedalus :: hashing/Cache.hpp
//
//  Two bounded caches with different eviction policies, both O(1) per
//  operation. They share an abstract interface so a caller can swap policies
//  at runtime and the benchmark can measure hit rates side by side -- which is
//  the only honest way to choose between them, since neither wins in general.
//
//    LRUCache  evicts the least recently used entry.
//              Hash map of key -> list node, plus a recency list. Winner on
//              workloads with temporal locality (scans, sessions).
//
//    LFUCache  evicts the least frequently used entry, breaking ties by
//              recency. Frequency buckets in a hash map with a running minimum,
//              which is what keeps eviction O(1) instead of O(n) or O(log n).
//              Winner when a small hot set is queried forever.
//
//  Complexity: get / put O(1) expected for both | space O(capacity)
// ============================================================================
#ifndef DAEDALUS_HASHING_CACHE_HPP
#define DAEDALUS_HASHING_CACHE_HPP

#include <algorithm>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/core/Exception.hpp"
#include "daedalus/linear/DoublyLinkedList.hpp"

namespace daedalus {

/// Hits and misses, so a policy comparison is a measurement, not an opinion.
struct CacheStatistics {
    std::size_t hits{0};
    std::size_t misses{0};
    std::size_t evictions{0};

    [[nodiscard]] std::size_t lookups() const noexcept { return hits + misses; }
    [[nodiscard]] double hitRate() const noexcept {
        return lookups() == 0 ? 0.0 : static_cast<double>(hits) / static_cast<double>(lookups());
    }
};

/// Interface shared by every eviction policy.
template <typename K, typename V>
class Cache {
public:
    virtual ~Cache() = default;

    [[nodiscard]] virtual std::optional<V> get(const K& key) = 0;
    virtual void put(const K& key, const V& value) = 0;
    [[nodiscard]] virtual bool contains(const K& key) const = 0;
    virtual bool erase(const K& key) = 0;
    virtual void clear() = 0;

    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    [[nodiscard]] virtual std::size_t capacity() const noexcept = 0;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::vector<K> keysByEvictionOrder() const = 0;

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] const CacheStatistics& statistics() const noexcept { return stats_; }
    void resetStatistics() noexcept { stats_ = CacheStatistics{}; }

protected:
    CacheStatistics stats_;
};

// ---------------------------------------------------------------------------

template <typename K, typename V>
    requires Hashable<K>
class LRUCache final : public Cache<K, V> {
    using Entry = std::pair<K, V>;
    using List = DoublyLinkedList<Entry>;
    using Position = typename List::Iterator;

public:
    explicit LRUCache(std::size_t capacity) : capacity_(capacity) {
        require(capacity > 0, "cache capacity must be greater than zero");
    }

    [[nodiscard]] std::size_t size() const noexcept override { return index_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept override { return capacity_; }
    [[nodiscard]] std::string name() const override { return "LRUCache"; }

    void clear() override {
        recency_.clear();
        index_.clear();
    }

    [[nodiscard]] std::optional<V> get(const K& key) override {
        const auto found = index_.find(key);
        if (found == index_.end()) {
            ++this->stats_.misses;
            return std::nullopt;
        }
        ++this->stats_.hits;
        const V value = found->second->second;
        promote(key, found->second, value);
        return value;
    }

    void put(const K& key, const V& value) override {
        const auto found = index_.find(key);
        if (found != index_.end()) {
            promote(key, found->second, value);
            return;
        }
        if (index_.size() >= capacity_) evictOldest();
        recency_.pushFront(Entry{key, value});
        index_[key] = recency_.begin();
    }

    [[nodiscard]] bool contains(const K& key) const override {
        return index_.find(key) != index_.end();
    }

    bool erase(const K& key) override {
        const auto found = index_.find(key);
        if (found == index_.end()) return false;
        recency_.erase(found->second);
        index_.erase(found);
        return true;
    }

    /// Most recently used first; the last element is the next eviction victim.
    [[nodiscard]] std::vector<K> keysByEvictionOrder() const override {
        std::vector<K> out;
        out.reserve(index_.size());
        for (const Entry& entry : recency_) out.push_back(entry.first);
        return out;
    }

private:
    /// Moves an entry to the front of the recency list, updating its value.
    void promote(const K& key, Position position, const V& value) {
        recency_.erase(position);
        recency_.pushFront(Entry{key, value});
        index_[key] = recency_.begin();
    }

    void evictOldest() {
        if (recency_.empty()) return;
        const K victim = recency_.back().first;
        (void)recency_.popBack();
        index_.erase(victim);
        ++this->stats_.evictions;
    }

    std::size_t capacity_;
    List recency_;  ///< most recently used at the front
    std::unordered_map<K, Position> index_;
};

// ---------------------------------------------------------------------------

template <typename K, typename V>
    requires Hashable<K>
class LFUCache final : public Cache<K, V> {
    using KeyList = DoublyLinkedList<K>;
    using Position = typename KeyList::Iterator;

    struct Record {
        V value;
        std::size_t frequency{1};
        Position position{};
    };

public:
    explicit LFUCache(std::size_t capacity) : capacity_(capacity) {
        require(capacity > 0, "cache capacity must be greater than zero");
    }

    [[nodiscard]] std::size_t size() const noexcept override { return records_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept override { return capacity_; }
    [[nodiscard]] std::string name() const override { return "LFUCache"; }

    void clear() override {
        records_.clear();
        buckets_.clear();
        minFrequency_ = 0;
    }

    [[nodiscard]] std::optional<V> get(const K& key) override {
        const auto found = records_.find(key);
        if (found == records_.end()) {
            ++this->stats_.misses;
            return std::nullopt;
        }
        ++this->stats_.hits;
        touch(key, found->second);
        return found->second.value;
    }

    void put(const K& key, const V& value) override {
        const auto found = records_.find(key);
        if (found != records_.end()) {
            found->second.value = value;
            touch(key, found->second);
            return;
        }
        if (records_.size() >= capacity_) evictLeastFrequent();

        // Aggregate-initialise and insert, rather than records_[key] = ...,
        // so V never has to be default-constructible.
        KeyList& bucket = buckets_[1];
        bucket.pushFront(key);
        records_.insert({key, Record{value, 1, bucket.begin()}});
        minFrequency_ = 1;
    }

    [[nodiscard]] bool contains(const K& key) const override {
        return records_.find(key) != records_.end();
    }

    bool erase(const K& key) override {
        const auto found = records_.find(key);
        if (found == records_.end()) return false;
        buckets_[found->second.frequency].erase(found->second.position);
        records_.erase(found);
        return true;
    }

    /// Least frequently used first, i.e. eviction order.
    [[nodiscard]] std::vector<K> keysByEvictionOrder() const override {
        std::vector<std::pair<std::size_t, K>> ranked;
        ranked.reserve(records_.size());
        for (const auto& entry : records_) {
            ranked.push_back({entry.second.frequency, entry.first});
        }
        std::sort(ranked.begin(), ranked.end());
        std::vector<K> out;
        out.reserve(ranked.size());
        for (const auto& entry : ranked) out.push_back(entry.second);
        return out;
    }

    /// How many times `key` has been touched. Zero when absent.
    [[nodiscard]] std::size_t frequencyOf(const K& key) const {
        const auto found = records_.find(key);
        return found == records_.end() ? 0 : found->second.frequency;
    }

private:
    /// Moves a record from its frequency bucket into the next one up.
    void touch(const K& key, Record& record) {
        const std::size_t from = record.frequency;
        KeyList& source = buckets_[from];
        source.erase(record.position);
        if (source.empty() && minFrequency_ == from) ++minFrequency_;

        record.frequency = from + 1;
        KeyList& target = buckets_[record.frequency];
        target.pushFront(key);
        record.position = target.begin();
    }

    void evictLeastFrequent() {
        auto bucket = buckets_.find(minFrequency_);
        if (bucket == buckets_.end() || bucket->second.empty()) {
            // The running minimum can go stale after an explicit erase(), which
            // removes an entry without going through touch(). Recover rather
            // than silently letting the cache grow past its capacity.
            if (!recomputeMinimumFrequency()) return;
            bucket = buckets_.find(minFrequency_);
        }
        const K victim = bucket->second.back();   // oldest within the frequency
        (void)bucket->second.popBack();
        records_.erase(victim);
        ++this->stats_.evictions;
    }

    /// Returns false when every bucket is empty, i.e. the cache is empty.
    bool recomputeMinimumFrequency() {
        bool found = false;
        std::size_t smallest = 0;
        for (const auto& entry : buckets_) {
            if (entry.second.empty()) continue;
            if (!found || entry.first < smallest) {
                smallest = entry.first;
                found = true;
            }
        }
        if (found) minFrequency_ = smallest;
        return found;
    }

    std::size_t capacity_;
    std::size_t minFrequency_{0};
    std::unordered_map<K, Record> records_;
    std::unordered_map<std::size_t, KeyList> buckets_;
};

}  // namespace daedalus

#endif  // DAEDALUS_HASHING_CACHE_HPP
