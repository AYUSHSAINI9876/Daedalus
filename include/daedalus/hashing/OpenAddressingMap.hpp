// ============================================================================
//  Daedalus :: hashing/OpenAddressingMap.hpp
//
//  Hash map with Robin Hood linear probing. Everything lives in one flat array,
//  so a lookup touches one or two cache lines instead of chasing a chain
//  pointer -- the reason modern hash maps are built this way.
//
//  Robin Hood: on insert, if the key being placed is further from its ideal
//  slot than the key currently occupying it, they swap. That evens out the
//  probe distances, so the worst lookup stays close to the average instead of
//  degenerating into one long run.
//
//  Deletion uses backward-shift rather than tombstones: the following run is
//  pulled back one slot, which keeps the table clean under churn instead of
//  slowly filling with dead markers.
//
//  Complexity: get/put/erase O(1) expected | space O(capacity)
// ============================================================================
#ifndef DAEDALUS_HASHING_OPEN_ADDRESSING_MAP_HPP
#define DAEDALUS_HASHING_OPEN_ADDRESSING_MAP_HPP

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
#include "daedalus/hashing/HashMap.hpp"

namespace daedalus {

template <typename K, typename V, typename Hash = std::hash<K>>
    requires Hashable<K>
class OpenAddressingMap final : public Map<K, V> {
    struct Slot {
        K key{};
        V value{};
        bool occupied{false};
        std::size_t home{0};  ///< ideal bucket, cached to avoid rehashing
    };

public:
    using key_type = K;
    using mapped_type = V;
    using value_type = std::pair<K, V>;
    using size_type = std::size_t;

    static constexpr double kDefaultMaxLoadFactor = 0.7;

    OpenAddressingMap() : slots_(nextHashTableSize(11)) {}

    explicit OpenAddressingMap(size_type expectedElements)
        : slots_(nextHashTableSize(
              static_cast<size_type>(static_cast<double>(expectedElements) /
                                     kDefaultMaxLoadFactor) + 1)) {}

    OpenAddressingMap(std::initializer_list<value_type> entries) : OpenAddressingMap() {
        for (const auto& entry : entries) put(entry.first, entry.second);
    }

    // --- observers -----------------------------------------------------------

    [[nodiscard]] size_type size() const noexcept override { return size_; }
    [[nodiscard]] bool empty() const noexcept override { return size_ == 0; }
    [[nodiscard]] std::string name() const override { return "OpenAddressingMap"; }
    [[nodiscard]] size_type capacity() const noexcept { return slots_.size(); }

    [[nodiscard]] double loadFactor() const noexcept {
        return static_cast<double>(size_) / static_cast<double>(slots_.size());
    }

    void clear() override {
        for (Slot& slot : slots_) slot = Slot{};
        size_ = 0;
    }

    /// Longest probe distance in the table. Robin Hood keeps this small even
    /// at high load; plain linear probing does not.
    [[nodiscard]] size_type longestProbe() const {
        size_type worst = 0;
        for (size_type i = 0; i < slots_.size(); ++i) {
            if (!slots_[i].occupied) continue;
            const size_type distance = probeDistance(i);
            if (distance > worst) worst = distance;
        }
        return worst;
    }

    // --- lookup --------------------------------------------------------------

    [[nodiscard]] std::optional<V> get(const K& key) const override {
        const size_type found = findSlot(key);
        if (found == kNotFound) return std::nullopt;
        return slots_[found].value;
    }

    [[nodiscard]] bool contains(const K& key) const override {
        return findSlot(key) != kNotFound;
    }

    [[nodiscard]] V& at(const K& key) {
        const size_type found = findSlot(key);
        if (found == kNotFound) throw KeyNotFound(formatElement(key));
        return slots_[found].value;
    }

    // --- mutation ------------------------------------------------------------

    void put(const K& key, const V& value) override {
        if (loadFactor() >= kDefaultMaxLoadFactor) {
            rehash(nextHashTableSize(slots_.size() + 1));
        }
        insertInternal(key, value);
    }

    bool erase(const K& key) override {
        const size_type found = findSlot(key);
        if (found == kNotFound) return false;

        // Backward shift: pull the following run back while those entries are
        // not already sitting in their home slot.
        size_type hole = found;
        for (;;) {
            const size_type next = (hole + 1) % slots_.size();
            if (!slots_[next].occupied || slots_[next].home == next) break;
            slots_[hole] = slots_[next];
            hole = next;
        }
        slots_[hole] = Slot{};
        --size_;
        return true;
    }

    void reserve(size_type expectedElements) {
        const size_type wanted = nextHashTableSize(
            static_cast<size_type>(static_cast<double>(expectedElements) /
                                   kDefaultMaxLoadFactor) + 1);
        if (wanted > slots_.size()) rehash(wanted);
    }

    // --- enumeration ---------------------------------------------------------

    [[nodiscard]] std::vector<K> keys() const override {
        std::vector<K> out;
        out.reserve(size_);
        for (const Slot& slot : slots_) {
            if (slot.occupied) out.push_back(slot.key);
        }
        return out;
    }

    [[nodiscard]] std::vector<value_type> entries() const override {
        std::vector<value_type> out;
        out.reserve(size_);
        for (const Slot& slot : slots_) {
            if (slot.occupied) out.push_back(value_type{slot.key, slot.value});
        }
        return out;
    }

private:
    static constexpr size_type kNotFound = static_cast<size_type>(-1);

    [[nodiscard]] size_type probeDistance(size_type index) const {
        const size_type home = slots_[index].home;
        return index >= home ? index - home : index + slots_.size() - home;
    }

    [[nodiscard]] size_type findSlot(const K& key) const {
        const size_type home = hash_(key) % slots_.size();
        size_type index = home;
        size_type distance = 0;
        while (slots_[index].occupied) {
            if (slots_[index].key == key) return index;
            // Robin Hood invariant: a key never sits past a slot whose own
            // probe distance is shorter, so this proves absence early.
            if (probeDistance(index) < distance) return kNotFound;
            index = (index + 1) % slots_.size();
            ++distance;
            if (distance > slots_.size()) return kNotFound;
        }
        return kNotFound;
    }

    void insertInternal(const K& key, const V& value) {
        Slot candidate{key, value, true, hash_(key) % slots_.size()};
        size_type index = candidate.home;
        size_type distance = 0;

        for (;;) {
            if (!slots_[index].occupied) {
                slots_[index] = candidate;
                ++size_;
                return;
            }
            if (slots_[index].key == candidate.key) {
                slots_[index].value = candidate.value;   // overwrite
                return;
            }
            // The richer entry gives up its slot to the poorer one. Capture the
            // occupant's distance before the swap -- afterwards the slot holds
            // our candidate and reports the wrong number.
            const size_type occupantDistance = probeDistance(index);
            if (occupantDistance < distance) {
                std::swap(slots_[index], candidate);
                distance = occupantDistance;
            }
            index = (index + 1) % slots_.size();
            ++distance;
        }
    }

    void rehash(size_type newCapacity) {
        std::vector<Slot> old = std::move(slots_);
        slots_.assign(newCapacity, Slot{});
        size_ = 0;
        for (const Slot& slot : old) {
            if (slot.occupied) insertInternal(slot.key, slot.value);
        }
    }

    Hash hash_{};
    std::vector<Slot> slots_;
    size_type size_{0};
};

}  // namespace daedalus

#endif  // DAEDALUS_HASHING_OPEN_ADDRESSING_MAP_HPP
