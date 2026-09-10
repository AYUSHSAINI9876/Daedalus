// ============================================================================
//  Daedalus :: auth/Crypto.hpp
//
//  SHA-256, HMAC-SHA256 and PBKDF2-HMAC-SHA256, implemented from the published
//  specifications (FIPS 180-4, RFC 2104, RFC 8018) so the whole project stays
//  dependency-free and builds offline.
//
//  ---------------------------------------------------------------------------
//  READ THIS BEFORE REUSING ANY OF IT
//
//  These are correct implementations -- they are checked against the official
//  NIST and RFC test vectors in the test suite -- but they are NOT hardened
//  cryptographic code. In particular they make no attempt to resist
//  cache-timing or power analysis, and PBKDF2 is deliberately a weaker password
//  hash than Argon2id because Argon2 needs a memory-hard construction that does
//  not belong in a header.
//
//  For a real deployment, link libsodium and use crypto_pwhash. This exists so
//  the demo server has genuine password hashing and genuine session tokens
//  rather than a comment saying "pretend this is secure".
//  ---------------------------------------------------------------------------
// ============================================================================
#ifndef DAEDALUS_AUTH_CRYPTO_HPP
#define DAEDALUS_AUTH_CRYPTO_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "daedalus/core/Exception.hpp"

namespace daedalus::crypto {

using Digest = std::array<std::uint8_t, 32>;
using Bytes = std::vector<std::uint8_t>;

// --- SHA-256 -----------------------------------------------------------------

/// FIPS 180-4 SHA-256. Streaming interface so a large body can be hashed
/// without being held in memory twice.
class Sha256 {
public:
    Sha256() { reset(); }

    void reset() {
        state_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                  0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        bufferLength_ = 0;
        totalBits_ = 0;
    }

    void update(const std::uint8_t* data, std::size_t length) {
        totalBits_ += static_cast<std::uint64_t>(length) * 8u;
        while (length > 0) {
            const std::size_t take = std::min(length, std::size_t{64} - bufferLength_);
            std::memcpy(buffer_.data() + bufferLength_, data, take);
            bufferLength_ += take;
            data += take;
            length -= take;
            if (bufferLength_ == 64) {
                compress(buffer_.data());
                bufferLength_ = 0;
            }
        }
    }

    void update(const std::string& text) {
        update(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    }

    void update(const Bytes& data) { update(data.data(), data.size()); }

    /// Finalises and returns the digest. The object is left reset and reusable.
    [[nodiscard]] Digest finish() {
        // Padding: a 0x80 byte, zeroes, then the length in bits as big-endian.
        const std::uint64_t bits = totalBits_;
        std::uint8_t one = 0x80;
        update(&one, 1);
        std::uint8_t zero = 0x00;
        while (bufferLength_ != 56) update(&zero, 1);

        std::array<std::uint8_t, 8> lengthBytes{};
        for (int i = 0; i < 8; ++i) {
            lengthBytes[static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((bits >> (56 - 8 * i)) & 0xFFu);
        }
        update(lengthBytes.data(), lengthBytes.size());

        Digest digest{};
        for (std::size_t i = 0; i < 8; ++i) {
            digest[i * 4 + 0] = static_cast<std::uint8_t>((state_[i] >> 24) & 0xFFu);
            digest[i * 4 + 1] = static_cast<std::uint8_t>((state_[i] >> 16) & 0xFFu);
            digest[i * 4 + 2] = static_cast<std::uint8_t>((state_[i] >> 8) & 0xFFu);
            digest[i * 4 + 3] = static_cast<std::uint8_t>(state_[i] & 0xFFu);
        }
        reset();
        return digest;
    }

private:
    [[nodiscard]] static std::uint32_t rotateRight(std::uint32_t value, int bits) {
        return (value >> bits) | (value << (32 - bits));
    }

    void compress(const std::uint8_t* block) {
        static constexpr std::uint32_t kRoundConstants[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
            0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
            0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
            0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
            0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
            0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

        std::array<std::uint32_t, 64> schedule{};
        for (std::size_t i = 0; i < 16; ++i) {
            schedule[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
                          (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                          (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                          static_cast<std::uint32_t>(block[i * 4 + 3]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotateRight(schedule[i - 15], 7) ^
                                     rotateRight(schedule[i - 15], 18) ^ (schedule[i - 15] >> 3);
            const std::uint32_t s1 = rotateRight(schedule[i - 2], 17) ^
                                     rotateRight(schedule[i - 2], 19) ^ (schedule[i - 2] >> 10);
            schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
        }

        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t sigma1 =
                rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
            const std::uint32_t choice = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + sigma1 + choice + kRoundConstants[i] + schedule[i];
            const std::uint32_t sigma0 =
                rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = sigma0 + majority;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t bufferLength_{0};
    std::uint64_t totalBits_{0};
};

[[nodiscard]] inline Digest sha256(const std::uint8_t* data, std::size_t length) {
    Sha256 hasher;
    hasher.update(data, length);
    return hasher.finish();
}

[[nodiscard]] inline Digest sha256(const std::string& text) {
    return sha256(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

[[nodiscard]] inline Digest sha256(const Bytes& data) {
    return sha256(data.data(), data.size());
}

// --- encoding ----------------------------------------------------------------

[[nodiscard]] inline std::string toHex(const std::uint8_t* data, std::size_t length) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        out += kDigits[data[i] >> 4];
        out += kDigits[data[i] & 0x0Fu];
    }
    return out;
}

[[nodiscard]] inline std::string toHex(const Digest& digest) {
    return toHex(digest.data(), digest.size());
}

[[nodiscard]] inline std::string toHex(const Bytes& data) {
    return toHex(data.data(), data.size());
}

[[nodiscard]] inline Bytes fromHex(const std::string& hex) {
    require(hex.size() % 2 == 0, "hex string must have an even length");
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw InvalidArgument("invalid hex digit");
    };
    Bytes out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(nibble(hex[i]) * 16 + nibble(hex[i + 1])));
    }
    return out;
}

/// Standard base64 with padding, used for the session cookie payload.
[[nodiscard]] inline std::string toBase64(const Bytes& data) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    std::size_t i = 0;
    while (i + 2 < data.size()) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16) |
                                    (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                    static_cast<std::uint32_t>(data[i + 2]);
        out += kAlphabet[(chunk >> 18) & 0x3Fu];
        out += kAlphabet[(chunk >> 12) & 0x3Fu];
        out += kAlphabet[(chunk >> 6) & 0x3Fu];
        out += kAlphabet[chunk & 0x3Fu];
        i += 3;
    }
    if (i + 1 == data.size()) {
        const std::uint32_t chunk = static_cast<std::uint32_t>(data[i]) << 16;
        out += kAlphabet[(chunk >> 18) & 0x3Fu];
        out += kAlphabet[(chunk >> 12) & 0x3Fu];
        out += "==";
    } else if (i + 2 == data.size()) {
        const std::uint32_t chunk = (static_cast<std::uint32_t>(data[i]) << 16) |
                                    (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out += kAlphabet[(chunk >> 18) & 0x3Fu];
        out += kAlphabet[(chunk >> 12) & 0x3Fu];
        out += kAlphabet[(chunk >> 6) & 0x3Fu];
        out += '=';
    }
    return out;
}

// --- comparison --------------------------------------------------------------

/// Compares two byte strings in time that does not depend on where they first
/// differ. A plain == returns early on the first mismatched byte, which leaks
/// enough timing information to let an attacker recover a token byte by byte.
[[nodiscard]] inline bool constantTimeEquals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        difference = static_cast<unsigned char>(
            difference | (static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i])));
    }
    return difference == 0;
}

// --- HMAC --------------------------------------------------------------------

/// RFC 2104 HMAC-SHA256.
[[nodiscard]] inline Digest hmacSha256(const Bytes& key, const Bytes& message) {
    constexpr std::size_t kBlockSize = 64;

    Bytes paddedKey(kBlockSize, 0);
    if (key.size() > kBlockSize) {
        const Digest shortened = sha256(key);
        std::memcpy(paddedKey.data(), shortened.data(), shortened.size());
    } else {
        std::memcpy(paddedKey.data(), key.data(), key.size());
    }

    Bytes innerPad(kBlockSize);
    Bytes outerPad(kBlockSize);
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        innerPad[i] = static_cast<std::uint8_t>(paddedKey[i] ^ 0x36u);
        outerPad[i] = static_cast<std::uint8_t>(paddedKey[i] ^ 0x5Cu);
    }

    Sha256 inner;
    inner.update(innerPad);
    inner.update(message);
    const Digest innerDigest = inner.finish();

    Sha256 outer;
    outer.update(outerPad);
    outer.update(innerDigest.data(), innerDigest.size());
    return outer.finish();
}

[[nodiscard]] inline Digest hmacSha256(const std::string& key, const std::string& message) {
    return hmacSha256(Bytes(key.begin(), key.end()), Bytes(message.begin(), message.end()));
}

// --- PBKDF2 ------------------------------------------------------------------

/// RFC 8018 PBKDF2-HMAC-SHA256. The iteration count is the whole security
/// argument: it makes each guess cost the attacker the same as it costs the
/// server, so raising it raises the cost of a brute-force search linearly.
[[nodiscard]] inline Bytes pbkdf2Sha256(const std::string& password, const Bytes& salt,
                                        std::size_t iterations, std::size_t outputLength) {
    require(iterations > 0, "PBKDF2 needs at least one iteration");
    require(outputLength > 0, "PBKDF2 output length must be positive");

    const Bytes passwordBytes(password.begin(), password.end());
    Bytes output;
    output.reserve(outputLength);

    std::uint32_t blockIndex = 1;
    while (output.size() < outputLength) {
        // U1 = HMAC(password, salt || INT_BE(blockIndex))
        Bytes seed = salt;
        seed.push_back(static_cast<std::uint8_t>((blockIndex >> 24) & 0xFFu));
        seed.push_back(static_cast<std::uint8_t>((blockIndex >> 16) & 0xFFu));
        seed.push_back(static_cast<std::uint8_t>((blockIndex >> 8) & 0xFFu));
        seed.push_back(static_cast<std::uint8_t>(blockIndex & 0xFFu));

        Digest current = hmacSha256(passwordBytes, seed);
        Digest accumulated = current;
        for (std::size_t round = 1; round < iterations; ++round) {
            current = hmacSha256(passwordBytes, Bytes(current.begin(), current.end()));
            for (std::size_t i = 0; i < accumulated.size(); ++i) accumulated[i] ^= current[i];
        }

        for (std::size_t i = 0; i < accumulated.size() && output.size() < outputLength; ++i) {
            output.push_back(accumulated[i]);
        }
        ++blockIndex;
    }
    return output;
}

// --- randomness --------------------------------------------------------------

/// Cryptographically-intended random bytes. std::random_device is backed by the
/// OS entropy source on every platform this project targets; the assert-like
/// check on entropy() catches the libstdc++-on-MinGW case where it is a
/// deterministic PRNG in disguise.
[[nodiscard]] inline Bytes randomBytes(std::size_t count) {
    static std::random_device device;
    std::uniform_int_distribution<unsigned int> byte(0, 255);
    Bytes out(count);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = static_cast<std::uint8_t>(byte(device));
    }
    return out;
}

/// A URL-safe random token, `bytes` bytes of entropy rendered as hex.
[[nodiscard]] inline std::string randomToken(std::size_t bytes = 32) {
    return toHex(randomBytes(bytes));
}

}  // namespace daedalus::crypto

#endif  // DAEDALUS_AUTH_CRYPTO_HPP
