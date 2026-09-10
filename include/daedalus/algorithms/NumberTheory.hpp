// ============================================================================
//  Daedalus :: algorithms/NumberTheory.hpp
//
//    sieveOfEratosthenes    all primes below n           O(n log log n)
//    linearSieve            same, plus smallest prime factor, in O(n)
//    isPrime                deterministic Miller-Rabin for 64-bit inputs
//    primeFactors           trial division by the sieve's factor table
//    gcd / lcm              Euclid, iterative
//    extendedGcd            Bezout coefficients -- the basis of modular inverse
//    modularPower           fast exponentiation by squaring
//    modularInverse         via extended Euclid (any modulus, coprime input)
//    chineseRemainder       merges congruences pairwise
//    binomial               Pascal's rule, and a modular version
//    eulerTotient           via the distinct prime factors
//
//  Everything modular uses __int128 for the multiply where available, because
//  (a * b) % m overflows 64-bit long before m does, and that overflow is silent.
// ============================================================================
#ifndef DAEDALUS_ALGORITHMS_NUMBER_THEORY_HPP
#define DAEDALUS_ALGORITHMS_NUMBER_THEORY_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

#include "daedalus/core/Exception.hpp"

namespace daedalus {

/// Multiplies modulo m without overflowing. Uses the compiler's 128-bit type
/// when it exists, and falls back to Russian-peasant doubling when it does not.
[[nodiscard]] inline std::uint64_t modularMultiply(std::uint64_t a, std::uint64_t b,
                                                   std::uint64_t modulus) {
#if defined(__SIZEOF_INT128__)
    return static_cast<std::uint64_t>((static_cast<__uint128_t>(a) * b) % modulus);
#else
    std::uint64_t result = 0;
    a %= modulus;
    while (b > 0) {
        if (b & 1u) result = (result + a) % modulus;
        a = (a + a) % modulus;
        b >>= 1;
    }
    return result;
#endif
}

/// a^exponent mod modulus, by repeated squaring. O(log exponent).
[[nodiscard]] inline std::uint64_t modularPower(std::uint64_t base, std::uint64_t exponent,
                                                std::uint64_t modulus) {
    require(modulus > 0, "modulus must be positive");
    if (modulus == 1) return 0;
    std::uint64_t result = 1;
    base %= modulus;
    while (exponent > 0) {
        if (exponent & 1u) result = modularMultiply(result, base, modulus);
        base = modularMultiply(base, base, modulus);
        exponent >>= 1;
    }
    return result;
}

// --- primality ---------------------------------------------------------------

/// Deterministic Miller-Rabin. The seven listed witnesses are proven sufficient
/// for every value below 2^64, so this is exact, not probabilistic.
[[nodiscard]] inline bool isPrime(std::uint64_t n) {
    if (n < 2) return false;
    for (std::uint64_t small :
         {2ull, 3ull, 5ull, 7ull, 11ull, 13ull, 17ull, 19ull, 23ull, 29ull, 31ull, 37ull}) {
        if (n % small == 0) return n == small;
    }

    // Write n - 1 as d * 2^r with d odd.
    std::uint64_t d = n - 1;
    int r = 0;
    while ((d & 1u) == 0) {
        d >>= 1;
        ++r;
    }

    for (std::uint64_t witness :
         {2ull, 325ull, 9375ull, 28178ull, 450775ull, 9780504ull, 1795265022ull}) {
        const std::uint64_t base = witness % n;
        // A witness that is a multiple of n carries no information: it reduces
        // to 0, every power stays 0, and the test would wrongly call n
        // composite. n = 73 hits this (it divides the witness 28178).
        if (base == 0) continue;

        std::uint64_t x = modularPower(base, d, n);
        if (x == 1 || x == n - 1) continue;
        bool composite = true;
        for (int i = 1; i < r; ++i) {
            x = modularMultiply(x, x, n);
            if (x == n - 1) {
                composite = false;
                break;
            }
        }
        if (composite) return false;
    }
    return true;
}

/// Every prime strictly below `limit`. Marks composites starting at p*p,
/// because anything smaller already has a smaller prime factor.
[[nodiscard]] inline std::vector<std::uint64_t> sieveOfEratosthenes(std::size_t limit) {
    std::vector<std::uint64_t> primes;
    if (limit < 3) return primes;

    std::vector<bool> composite(limit, false);
    for (std::size_t candidate = 2; candidate < limit; ++candidate) {
        if (composite[candidate]) continue;
        primes.push_back(candidate);
        if (candidate * candidate >= limit) continue;
        for (std::size_t multiple = candidate * candidate; multiple < limit;
             multiple += candidate) {
            composite[multiple] = true;
        }
    }
    return primes;
}

struct LinearSieve {
    std::vector<std::uint64_t> primes;
    std::vector<std::size_t> smallestPrimeFactor;   ///< indexed by value
};

/// Linear sieve: each composite is crossed off exactly once, by its smallest
/// prime factor. That gives O(n) instead of O(n log log n), and the factor
/// table it leaves behind makes factorisation O(log n) per query.
[[nodiscard]] inline LinearSieve linearSieve(std::size_t limit) {
    LinearSieve result;
    if (limit < 2) return result;
    result.smallestPrimeFactor.assign(limit, 0);

    for (std::size_t candidate = 2; candidate < limit; ++candidate) {
        if (result.smallestPrimeFactor[candidate] == 0) {
            result.smallestPrimeFactor[candidate] = candidate;
            result.primes.push_back(candidate);
        }
        for (std::uint64_t prime : result.primes) {
            const std::size_t product = candidate * static_cast<std::size_t>(prime);
            if (prime > result.smallestPrimeFactor[candidate] || product >= limit) break;
            result.smallestPrimeFactor[product] = static_cast<std::size_t>(prime);
        }
    }
    return result;
}

/// Prime factorisation with multiplicities, by trial division up to sqrt(n).
[[nodiscard]] inline std::vector<std::pair<std::uint64_t, std::size_t>> primeFactors(
    std::uint64_t n) {
    std::vector<std::pair<std::uint64_t, std::size_t>> factors;
    for (std::uint64_t divisor = 2; divisor * divisor <= n; ++divisor) {
        if (n % divisor != 0) continue;
        std::size_t power = 0;
        while (n % divisor == 0) {
            n /= divisor;
            ++power;
        }
        factors.push_back({divisor, power});
    }
    if (n > 1) factors.push_back({n, 1});
    return factors;
}

/// All divisors of n, ascending. Pairs each divisor below sqrt(n) with its
/// complement, so it is O(sqrt n) rather than O(n).
[[nodiscard]] inline std::vector<std::uint64_t> divisors(std::uint64_t n) {
    std::vector<std::uint64_t> small;
    std::vector<std::uint64_t> large;
    for (std::uint64_t d = 1; d * d <= n; ++d) {
        if (n % d != 0) continue;
        small.push_back(d);
        if (d != n / d) large.push_back(n / d);
    }
    for (std::size_t i = large.size(); i-- > 0;) small.push_back(large[i]);
    return small;
}

/// Euler's totient: how many integers below n are coprime to it.
[[nodiscard]] inline std::uint64_t eulerTotient(std::uint64_t n) {
    if (n == 0) return 0;
    std::uint64_t result = n;
    for (const auto& factor : primeFactors(n)) {
        result -= result / factor.first;
    }
    return result;
}

// --- gcd family --------------------------------------------------------------

[[nodiscard]] inline long long greatestCommonDivisor(long long a, long long b) {
    a = a < 0 ? -a : a;
    b = b < 0 ? -b : b;
    while (b != 0) {
        const long long remainder = a % b;
        a = b;
        b = remainder;
    }
    return a;
}

[[nodiscard]] inline long long leastCommonMultiple(long long a, long long b) {
    if (a == 0 || b == 0) return 0;
    const long long divisor = greatestCommonDivisor(a, b);
    const long long quotient = (a < 0 ? -a : a) / divisor;
    return quotient * (b < 0 ? -b : b);
}

struct BezoutIdentity {
    long long gcd{0};
    long long x{0};
    long long y{0};   ///< a*x + b*y == gcd
};

/// Extended Euclid. The coefficients are what make modular inversion possible:
/// a*x + m*y = 1 means x is a's inverse modulo m.
[[nodiscard]] inline BezoutIdentity extendedGcd(long long a, long long b) {
    long long oldR = a;
    long long r = b;
    long long oldX = 1;
    long long x = 0;
    long long oldY = 0;
    long long y = 1;

    while (r != 0) {
        const long long quotient = oldR / r;
        long long temp = oldR - quotient * r;
        oldR = r;
        r = temp;
        temp = oldX - quotient * x;
        oldX = x;
        x = temp;
        temp = oldY - quotient * y;
        oldY = y;
        y = temp;
    }
    return BezoutIdentity{oldR, oldX, oldY};
}

/// Inverse of a modulo m, or nullopt when gcd(a, m) != 1 (no inverse exists).
[[nodiscard]] inline std::optional<long long> modularInverse(long long a, long long modulus) {
    require(modulus > 0, "modulus must be positive");
    const BezoutIdentity identity = extendedGcd(((a % modulus) + modulus) % modulus, modulus);
    if (identity.gcd != 1) return std::nullopt;
    return ((identity.x % modulus) + modulus) % modulus;
}

/// Solves x = remainders[i] (mod moduli[i]) for all i, by merging the
/// congruences one at a time. Returns nullopt when they are inconsistent --
/// which they can be when the moduli are not pairwise coprime.
[[nodiscard]] inline std::optional<std::pair<long long, long long>> chineseRemainder(
    const std::vector<long long>& remainders, const std::vector<long long>& moduli) {
    require(remainders.size() == moduli.size(), "each remainder needs a modulus");
    if (remainders.empty()) return std::pair<long long, long long>{0, 1};

    long long result = remainders[0] % moduli[0];
    long long lcm = moduli[0];
    for (std::size_t i = 1; i < moduli.size(); ++i) {
        const BezoutIdentity identity = extendedGcd(lcm, moduli[i]);
        const long long difference = remainders[i] - result;
        if (difference % identity.gcd != 0) return std::nullopt;   // inconsistent

        const long long step = moduli[i] / identity.gcd;
        const long long multiplier =
            ((difference / identity.gcd) % step) * (identity.x % step) % step;
        result += lcm * multiplier;
        lcm *= step;
        result = ((result % lcm) + lcm) % lcm;
    }
    return std::pair<long long, long long>{result, lcm};
}

// --- combinatorics -----------------------------------------------------------

/// C(n, k) by Pascal's rule. Exact but overflows for large n -- use
/// binomialModulo for anything sizeable.
[[nodiscard]] inline long long binomial(std::size_t n, std::size_t k) {
    if (k > n) return 0;
    k = std::min(k, n - k);
    long long result = 1;
    for (std::size_t i = 0; i < k; ++i) {
        result = result * static_cast<long long>(n - i) / static_cast<long long>(i + 1);
    }
    return result;
}

/// C(n, k) mod a prime, via Fermat's little theorem for the inverse factorial.
[[nodiscard]] inline std::uint64_t binomialModulo(std::size_t n, std::size_t k,
                                                  std::uint64_t prime) {
    if (k > n) return 0;
    std::uint64_t numerator = 1;
    std::uint64_t denominator = 1;
    for (std::size_t i = 0; i < k; ++i) {
        numerator = modularMultiply(numerator, (n - i) % prime, prime);
        denominator = modularMultiply(denominator, (i + 1) % prime, prime);
    }
    // a^(p-2) is a's inverse when p is prime.
    return modularMultiply(numerator, modularPower(denominator, prime - 2, prime), prime);
}

/// The nth Fibonacci number, by fast doubling: F(2k) and F(2k+1) are computed
/// from F(k) and F(k+1), giving O(log n) instead of O(n).
[[nodiscard]] inline std::uint64_t fibonacci(std::uint64_t n) {
    struct Doubler {
        /// Returns (F(k), F(k+1)).
        std::pair<std::uint64_t, std::uint64_t> compute(std::uint64_t k) const {
            if (k == 0) return {0, 1};
            const auto half = compute(k / 2);
            const std::uint64_t a = half.first;
            const std::uint64_t b = half.second;
            const std::uint64_t c = a * (2 * b - a);
            const std::uint64_t d = a * a + b * b;
            return (k % 2 == 0) ? std::pair<std::uint64_t, std::uint64_t>{c, d}
                                : std::pair<std::uint64_t, std::uint64_t>{d, c + d};
        }
    };
    return Doubler{}.compute(n).first;
}

}   // namespace daedalus

#endif   // DAEDALUS_ALGORITHMS_NUMBER_THEORY_HPP
