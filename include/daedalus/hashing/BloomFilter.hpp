// ============================================================================
//  Daedalus :: hashing/BloomFilter.hpp
//
//  Probabilistic set membership in a fixed bit array. It answers "definitely
//  not present" or "probably present" -- never a false negative, sometimes a
//  false positive -- in a fraction of the memory a real set needs. That trade
//  is why every LSM-tree database puts one in front of each SSTable: a negative
//  answer skips a disk read outright.
//
//  Sizing follows the standard formulas for a target false-positive rate p over
//  n expected items:
//
//      m = -n * ln(p) / (ln 2)^2         bits
//      k = (m / n) * ln 2                hash functions
//
//  The k hashes come from Kirsch-Mitzenmacher double hashing, h_i = h1 + i*h2,
//  which is provably as good as k independent hashes here and costs two.
//
//  Complexity: add/mightContain O(k) | space m bits, independent of item size
// ============================================================================
#ifndef DAEDALUS_HASHING_BLOOM_FILTER_HPP
#define DAEDALUS_HASHING_BLOOM_FILTER_HPP

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "daedalus/core/Concepts.hpp"
#include "daedalus/core/Exception.hpp"

namespace daedalus {

template <typename T, typename Hash = std::hash<T>>
    requires Hashable<T>
class BloomFilter {
public:
    using size_type = std::size_t;

    /// Sizes the filter for `expectedItems` at the given false-positive rate.
    explicit BloomFilter(size_type expectedItems, double falsePositiveRate = 0.01) {
        require(expectedItems > 0, "bloom filter needs a positive expected item count");
        require(falsePositiveRate > 0.0 && falsePositiveRate < 1.0,
                "false positive rate must lie strictly between 0 and 1");

        const double n = static_cast<double>(expectedItems);
        const double ln2 = std::log(2.0);
        const double bits = -n * std::log(falsePositiveRate) / (ln2 * ln2);

        bitCount_ = static_cast<size_type>(std::ceil(bits));
        if (bitCount_ < 8) bitCount_ = 8;
        hashCount_ = static_cast<size_type>(
            std::round(static_cast<double>(bitCount_) / n * ln2));
        if (hashCount_ < 1) hashCount_ = 1;
        if (hashCount_ > 16) hashCount_ = 16;

        bits_.assign((bitCount_ + 63) / 64, 0u);
        expectedItems_ = expectedItems;
        targetRate_ = falsePositiveRate;
    }

    [[nodiscard]] std::string name() const { return "BloomFilter"; }
    [[nodiscard]] size_type bitCount() const noexcept { return bitCount_; }
    [[nodiscard]] size_type hashCount() const noexcept { return hashCount_; }
    [[nodiscard]] size_type insertedCount() const noexcept { return inserted_; }
    [[nodiscard]] size_type memoryBytes() const noexcept { return bits_.size() * 8; }
    [[nodiscard]] bool empty() const noexcept { return inserted_ == 0; }

    void clear() {
        bits_.assign(bits_.size(), 0u);
        inserted_ = 0;
    }

    void add(const T& value) {
        const auto [h1, h2] = seedPair(value);
        for (size_type i = 0; i < hashCount_; ++i) setBit(bitFor(h1, h2, i));
        ++inserted_;
    }

    /// False means definitely absent. True means probably present.
    [[nodiscard]] bool mightContain(const T& value) const {
        const auto [h1, h2] = seedPair(value);
        for (size_type i = 0; i < hashCount_; ++i) {
            if (!testBit(bitFor(h1, h2, i))) return false;
        }
        return true;
    }

    /// Fraction of bits set, which is what drives the actual error rate.
    [[nodiscard]] double fillRatio() const {
        size_type set = 0;
        for (size_type i = 0; i < bitCount_; ++i) {
            if (testBit(i)) ++set;
        }
        return static_cast<double>(set) / static_cast<double>(bitCount_);
    }

    /// Estimated false-positive rate at the current fill, (1 - e^(-kn/m))^k.
    [[nodiscard]] double estimatedFalsePositiveRate() const {
        const double k = static_cast<double>(hashCount_);
        const double n = static_cast<double>(inserted_);
        const double m = static_cast<double>(bitCount_);
        return std::pow(1.0 - std::exp(-k * n / m), k);
    }

    [[nodiscard]] double targetFalsePositiveRate() const noexcept { return targetRate_; }
    [[nodiscard]] size_type expectedItems() const noexcept { return expectedItems_; }

private:
    /// Two independent-enough seeds from one std::hash call: the raw hash, and
    /// the hash of its own bit-mixed form.
    [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> seedPair(const T& value) const {
        const std::uint64_t base = static_cast<std::uint64_t>(hash_(value));
        std::uint64_t mixed = base;
        mixed ^= mixed >> 33;
        mixed *= 0xFF51AFD7ED558CCDull;   // the MurmurHash3 finaliser constants
        mixed ^= mixed >> 33;
        mixed *= 0xC4CEB9FE1A85EC53ull;
        mixed ^= mixed >> 33;
        return {base, mixed | 1ull};      // odd second seed keeps the walk full-cycle
    }

    [[nodiscard]] size_type bitFor(std::uint64_t h1, std::uint64_t h2, size_type i) const {
        return static_cast<size_type>((h1 + static_cast<std::uint64_t>(i) * h2) %
                                      static_cast<std::uint64_t>(bitCount_));
    }

    void setBit(size_type index) { bits_[index / 64] |= (1ull << (index % 64)); }

    [[nodiscard]] bool testBit(size_type index) const {
        return (bits_[index / 64] & (1ull << (index % 64))) != 0;
    }

    Hash hash_{};
    std::vector<std::uint64_t> bits_;
    size_type bitCount_{0};
    size_type hashCount_{1};
    size_type inserted_{0};
    size_type expectedItems_{0};
    double targetRate_{0.01};
};

}  // namespace daedalus

#endif  // DAEDALUS_HASHING_BLOOM_FILTER_HPP
