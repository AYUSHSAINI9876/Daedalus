// ============================================================================
//  Daedalus :: algorithms/Strings.hpp
//
//  String matching and analysis.
//
//    knuthMorrisPratt    O(n + m)      never re-reads a character
//    zAlgorithm          O(n)          the Z-array, and matching built on it
//    rabinKarp           O(n + m) avg  rolling hash, good for multi-pattern
//    boyerMooreHorspool  O(n/m) best   skips ahead, the fastest in practice
//    manacher            O(n)          every palindromic substring at once
//    suffixArray         O(n log^2 n)  plus the Kasai LCP array in O(n)
//    AhoCorasick         O(n + total)  all patterns in a single pass
//    editDistance        O(n*m)        Levenshtein, with a rolling row
//    longestCommonSubsequence / Substring
//
//  The naive matcher is included too, because the tests check every algorithm
//  against it -- an optimised matcher that is subtly wrong is worse than a slow
//  one that is right.
// ============================================================================
#ifndef DAEDALUS_ALGORITHMS_STRINGS_HPP
#define DAEDALUS_ALGORITHMS_STRINGS_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <numeric>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Exception.hpp"

namespace daedalus {

/// Every start index where `pattern` occurs in `text`, checked character by
/// character. O(n*m), and the reference the other matchers are tested against.
[[nodiscard]] inline std::vector<std::size_t> naiveSearch(const std::string& text,
                                                          const std::string& pattern) {
    std::vector<std::size_t> matches;
    if (pattern.empty() || pattern.size() > text.size()) return matches;
    for (std::size_t start = 0; start + pattern.size() <= text.size(); ++start) {
        std::size_t i = 0;
        while (i < pattern.size() && text[start + i] == pattern[i]) ++i;
        if (i == pattern.size()) matches.push_back(start);
    }
    return matches;
}

// --- Knuth-Morris-Pratt ------------------------------------------------------

/// Failure function: for each prefix, the length of its longest proper prefix
/// that is also a suffix. That is what lets the matcher resume mid-pattern
/// after a mismatch instead of restarting.
[[nodiscard]] inline std::vector<std::size_t> prefixFunction(const std::string& pattern) {
    std::vector<std::size_t> table(pattern.size(), 0);
    for (std::size_t i = 1; i < pattern.size(); ++i) {
        std::size_t length = table[i - 1];
        while (length > 0 && pattern[i] != pattern[length]) length = table[length - 1];
        if (pattern[i] == pattern[length]) ++length;
        table[i] = length;
    }
    return table;
}

/// KMP search. The text pointer never moves backwards, so it works on a stream.
[[nodiscard]] inline std::vector<std::size_t> knuthMorrisPratt(const std::string& text,
                                                               const std::string& pattern) {
    std::vector<std::size_t> matches;
    if (pattern.empty() || pattern.size() > text.size()) return matches;

    const std::vector<std::size_t> table = prefixFunction(pattern);
    std::size_t matched = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        while (matched > 0 && text[i] != pattern[matched]) matched = table[matched - 1];
        if (text[i] == pattern[matched]) ++matched;
        if (matched == pattern.size()) {
            matches.push_back(i + 1 - pattern.size());
            matched = table[matched - 1];
        }
    }
    return matches;
}

// --- Z algorithm -------------------------------------------------------------

/// z[i] is the length of the longest substring starting at i that is also a
/// prefix of the whole string. Maintains the rightmost match window so each
/// character is examined a constant number of times.
[[nodiscard]] inline std::vector<std::size_t> zArray(const std::string& text) {
    const std::size_t n = text.size();
    std::vector<std::size_t> z(n, 0);
    if (n == 0) return z;
    z[0] = n;

    std::size_t left = 0;
    std::size_t right = 0;
    for (std::size_t i = 1; i < n; ++i) {
        if (i < right) z[i] = std::min(right - i, z[i - left]);
        while (i + z[i] < n && text[z[i]] == text[i + z[i]]) ++z[i];
        if (i + z[i] > right) {
            left = i;
            right = i + z[i];
        }
    }
    return z;
}

/// Matching via the Z array of "pattern + sentinel + text".
[[nodiscard]] inline std::vector<std::size_t> zSearch(const std::string& text,
                                                      const std::string& pattern) {
    std::vector<std::size_t> matches;
    if (pattern.empty() || pattern.size() > text.size()) return matches;

    // The separator must not occur in either string, or a "match" could span it.
    const std::string combined = pattern + '\x01' + text;
    const std::vector<std::size_t> z = zArray(combined);
    for (std::size_t i = pattern.size() + 1; i < combined.size(); ++i) {
        if (z[i] >= pattern.size()) matches.push_back(i - pattern.size() - 1);
    }
    return matches;
}

// --- Rabin-Karp --------------------------------------------------------------

/// Rolling-hash search. Hash collisions are resolved by an explicit character
/// comparison, so the result is exact -- a hash-only version would be wrong,
/// which is the classic mistake with this algorithm.
[[nodiscard]] inline std::vector<std::size_t> rabinKarp(const std::string& text,
                                                        const std::string& pattern) {
    std::vector<std::size_t> matches;
    if (pattern.empty() || pattern.size() > text.size()) return matches;

    constexpr std::uint64_t kBase = 257;
    constexpr std::uint64_t kModulus = 1000000007ull;
    const std::size_t m = pattern.size();

    std::uint64_t highestPower = 1;
    for (std::size_t i = 1; i < m; ++i) highestPower = (highestPower * kBase) % kModulus;

    std::uint64_t patternHash = 0;
    std::uint64_t windowHash = 0;
    for (std::size_t i = 0; i < m; ++i) {
        patternHash = (patternHash * kBase + static_cast<unsigned char>(pattern[i])) % kModulus;
        windowHash = (windowHash * kBase + static_cast<unsigned char>(text[i])) % kModulus;
    }

    for (std::size_t start = 0; start + m <= text.size(); ++start) {
        if (windowHash == patternHash && text.compare(start, m, pattern) == 0) {
            matches.push_back(start);
        }
        if (start + m < text.size()) {
            const std::uint64_t leaving =
                (static_cast<unsigned char>(text[start]) * highestPower) % kModulus;
            windowHash = (windowHash + kModulus - leaving) % kModulus;
            windowHash = (windowHash * kBase + static_cast<unsigned char>(text[start + m])) %
                         kModulus;
        }
    }
    return matches;
}

// --- Boyer-Moore-Horspool ----------------------------------------------------

/// Compares the pattern right to left and, on a mismatch, jumps ahead by
/// however far the offending text character allows. Sublinear in the good case
/// -- most characters are never looked at.
[[nodiscard]] inline std::vector<std::size_t> boyerMooreHorspool(const std::string& text,
                                                                 const std::string& pattern) {
    std::vector<std::size_t> matches;
    if (pattern.empty() || pattern.size() > text.size()) return matches;

    const std::size_t m = pattern.size();
    std::array<std::size_t, 256> shift{};
    shift.fill(m);
    for (std::size_t i = 0; i + 1 < m; ++i) {
        shift[static_cast<unsigned char>(pattern[i])] = m - 1 - i;
    }

    std::size_t start = 0;
    while (start + m <= text.size()) {
        std::size_t i = m;
        while (i > 0 && text[start + i - 1] == pattern[i - 1]) --i;
        if (i == 0) {
            matches.push_back(start);
            start += 1;   // allow overlapping matches
        } else {
            start += shift[static_cast<unsigned char>(text[start + m - 1])];
        }
    }
    return matches;
}

// --- palindromes -------------------------------------------------------------

/// Manacher's algorithm. Returns the radius of the longest palindrome centred
/// at each of the 2n+1 centres (characters and gaps), computed in linear time
/// by reusing the mirror of the current palindrome.
[[nodiscard]] inline std::vector<std::size_t> manacherRadii(const std::string& text) {
    // Interleave separators so even and odd palindromes are handled uniformly.
    std::string padded = "^";
    for (char c : text) {
        padded += '#';
        padded += c;
    }
    padded += "#$";

    const std::size_t n = padded.size();
    std::vector<std::size_t> radius(n, 0);
    std::size_t centre = 0;
    std::size_t right = 0;

    for (std::size_t i = 1; i + 1 < n; ++i) {
        if (i < right) radius[i] = std::min(right - i, radius[2 * centre - i]);
        while (padded[i + radius[i] + 1] == padded[i - radius[i] - 1]) ++radius[i];
        if (i + radius[i] > right) {
            centre = i;
            right = i + radius[i];
        }
    }
    return radius;
}

/// The longest palindromic substring, in linear time.
[[nodiscard]] inline std::string longestPalindrome(const std::string& text) {
    if (text.empty()) return "";
    const std::vector<std::size_t> radius = manacherRadii(text);
    std::size_t bestLength = 0;
    std::size_t bestCentre = 0;
    for (std::size_t i = 1; i + 1 < radius.size(); ++i) {
        if (radius[i] > bestLength) {
            bestLength = radius[i];
            bestCentre = i;
        }
    }
    const std::size_t start = (bestCentre - bestLength) / 2;
    return text.substr(start, bestLength);
}

[[nodiscard]] inline bool isPalindrome(const std::string& text) {
    for (std::size_t i = 0, j = text.empty() ? 0 : text.size() - 1; i < j; ++i, --j) {
        if (text[i] != text[j]) return false;
    }
    return true;
}

// --- suffix array ------------------------------------------------------------

/// Suffix array by prefix doubling: sort suffixes by their first character,
/// then by their first two, four, eight... reusing the previous ranks so each
/// round is a single sort. O(n log^2 n).
[[nodiscard]] inline std::vector<std::size_t> suffixArray(const std::string& text) {
    const std::size_t n = text.size();
    std::vector<std::size_t> order(n);
    if (n == 0) return order;

    std::iota(order.begin(), order.end(), std::size_t{0});
    std::vector<std::size_t> rank(n);
    for (std::size_t i = 0; i < n; ++i) rank[i] = static_cast<unsigned char>(text[i]);

    std::vector<std::size_t> nextRank(n);
    for (std::size_t length = 1;; length *= 2) {
        const auto byPair = [&](std::size_t a, std::size_t b) {
            if (rank[a] != rank[b]) return rank[a] < rank[b];
            const std::size_t tailA = a + length < n ? rank[a + length] + 1 : 0;
            const std::size_t tailB = b + length < n ? rank[b + length] + 1 : 0;
            return tailA < tailB;
        };
        std::sort(order.begin(), order.end(), byPair);

        nextRank[order[0]] = 0;
        for (std::size_t i = 1; i < n; ++i) {
            nextRank[order[i]] =
                nextRank[order[i - 1]] + (byPair(order[i - 1], order[i]) ? 1 : 0);
        }
        rank = nextRank;
        if (rank[order[n - 1]] == n - 1) break;   // all ranks distinct: done
        if (length > n) break;
    }
    return order;
}

/// Kasai's algorithm: longest common prefix between each pair of adjacent
/// suffixes, in O(n). The trick is that the LCP can shrink by at most one as
/// you move to the next suffix in text order.
[[nodiscard]] inline std::vector<std::size_t> longestCommonPrefixArray(
    const std::string& text, const std::vector<std::size_t>& suffixes) {
    const std::size_t n = text.size();
    std::vector<std::size_t> lcp(n, 0);
    if (n == 0) return lcp;

    std::vector<std::size_t> position(n);
    for (std::size_t i = 0; i < n; ++i) position[suffixes[i]] = i;

    std::size_t carry = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (position[i] == 0) {
            carry = 0;
            continue;
        }
        const std::size_t previous = suffixes[position[i] - 1];
        while (i + carry < n && previous + carry < n && text[i + carry] == text[previous + carry]) {
            ++carry;
        }
        lcp[position[i]] = carry;
        if (carry > 0) --carry;
    }
    return lcp;
}

/// The longest substring that occurs at least twice -- the maximum LCP entry.
[[nodiscard]] inline std::string longestRepeatedSubstring(const std::string& text) {
    if (text.size() < 2) return "";
    const std::vector<std::size_t> suffixes = suffixArray(text);
    const std::vector<std::size_t> lcp = longestCommonPrefixArray(text, suffixes);

    std::size_t bestLength = 0;
    std::size_t bestIndex = 0;
    for (std::size_t i = 0; i < lcp.size(); ++i) {
        if (lcp[i] > bestLength) {
            bestLength = lcp[i];
            bestIndex = suffixes[i];
        }
    }
    return text.substr(bestIndex, bestLength);
}

// --- Aho-Corasick ------------------------------------------------------------

/// Multi-pattern matching: a trie of all patterns plus failure links, so one
/// pass over the text finds every occurrence of every pattern. This is what a
/// virus scanner or a profanity filter actually runs.
class AhoCorasick {
public:
    struct Match {
        std::size_t position;     ///< start index in the text
        std::size_t patternIndex; ///< which pattern matched
        std::string pattern;
    };

    AhoCorasick() { nodes_.emplace_back(); }

    explicit AhoCorasick(const std::vector<std::string>& patterns) : AhoCorasick() {
        for (const std::string& pattern : patterns) addPattern(pattern);
        build();
    }

    void addPattern(const std::string& pattern) {
        if (pattern.empty()) return;
        std::size_t current = 0;
        for (char letter : pattern) {
            auto& children = nodes_[current].children;
            const auto found = children.find(letter);
            if (found == children.end()) {
                children[letter] = nodes_.size();
                current = nodes_.size();
                nodes_.emplace_back();
            } else {
                current = found->second;
            }
        }
        nodes_[current].outputs.push_back(patterns_.size());
        patterns_.push_back(pattern);
        built_ = false;
    }

    /// Computes failure links by BFS: a node's failure link is the longest
    /// proper suffix of its path that is also a path in the trie.
    void build() {
        std::queue<std::size_t> pending;
        for (auto& entry : nodes_[0].children) {
            nodes_[entry.second].failure = 0;
            pending.push(entry.second);
        }
        while (!pending.empty()) {
            const std::size_t current = pending.front();
            pending.pop();
            for (auto& entry : nodes_[current].children) {
                const char letter = entry.first;
                const std::size_t child = entry.second;
                std::size_t fallback = nodes_[current].failure;
                while (fallback != 0 && nodes_[fallback].children.count(letter) == 0) {
                    fallback = nodes_[fallback].failure;
                }
                const auto found = nodes_[fallback].children.find(letter);
                nodes_[child].failure =
                    (found != nodes_[fallback].children.end() && found->second != child)
                        ? found->second
                        : 0;
                // Suffix patterns ending here are also matches at this node.
                const std::vector<std::size_t> inherited = nodes_[nodes_[child].failure].outputs;
                nodes_[child].outputs.insert(nodes_[child].outputs.end(), inherited.begin(),
                                             inherited.end());
                pending.push(child);
            }
        }
        built_ = true;
    }

    [[nodiscard]] std::vector<Match> search(const std::string& text) {
        if (!built_) build();
        std::vector<Match> matches;
        std::size_t current = 0;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char letter = text[i];
            while (current != 0 && nodes_[current].children.count(letter) == 0) {
                current = nodes_[current].failure;
            }
            const auto found = nodes_[current].children.find(letter);
            current = found != nodes_[current].children.end() ? found->second : 0;

            for (std::size_t patternIndex : nodes_[current].outputs) {
                const std::string& pattern = patterns_[patternIndex];
                matches.push_back(Match{i + 1 - pattern.size(), patternIndex, pattern});
            }
        }
        return matches;
    }

    [[nodiscard]] std::size_t patternCount() const noexcept { return patterns_.size(); }
    [[nodiscard]] std::size_t nodeCount() const noexcept { return nodes_.size(); }

private:
    struct Node {
        std::map<char, std::size_t> children;
        std::vector<std::size_t> outputs;
        std::size_t failure{0};
    };

    std::vector<Node> nodes_;
    std::vector<std::string> patterns_;
    bool built_{false};
};

// --- edit distance and subsequences -----------------------------------------

/// Levenshtein distance with a rolling row: O(n*m) time but only O(min(n,m))
/// space, which is what makes it usable on long inputs.
[[nodiscard]] inline std::size_t editDistance(const std::string& a, const std::string& b) {
    if (a.size() < b.size()) return editDistance(b, a);
    if (b.empty()) return a.size();

    std::vector<std::size_t> previous(b.size() + 1);
    std::iota(previous.begin(), previous.end(), std::size_t{0});
    std::vector<std::size_t> current(b.size() + 1);

    for (std::size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t substitution = previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            const std::size_t deletion = previous[j] + 1;
            const std::size_t insertion = current[j - 1] + 1;
            current[j] = std::min({substitution, deletion, insertion});
        }
        previous.swap(current);
    }
    return previous[b.size()];
}

/// Longest common subsequence, reconstructed (not just its length).
[[nodiscard]] inline std::string longestCommonSubsequence(const std::string& a,
                                                          const std::string& b) {
    const std::size_t n = a.size();
    const std::size_t m = b.size();
    std::vector<std::vector<std::size_t>> table(n + 1, std::vector<std::size_t>(m + 1, 0));

    for (std::size_t i = 1; i <= n; ++i) {
        for (std::size_t j = 1; j <= m; ++j) {
            table[i][j] = a[i - 1] == b[j - 1] ? table[i - 1][j - 1] + 1
                                               : std::max(table[i - 1][j], table[i][j - 1]);
        }
    }

    std::string result;
    std::size_t i = n;
    std::size_t j = m;
    while (i > 0 && j > 0) {
        if (a[i - 1] == b[j - 1]) {
            result += a[i - 1];
            --i;
            --j;
        } else if (table[i - 1][j] >= table[i][j - 1]) {
            --i;
        } else {
            --j;
        }
    }
    std::reverse(result.begin(), result.end());
    return result;
}

/// Longest contiguous run common to both strings.
[[nodiscard]] inline std::string longestCommonSubstring(const std::string& a,
                                                        const std::string& b) {
    const std::size_t n = a.size();
    const std::size_t m = b.size();
    if (n == 0 || m == 0) return "";

    std::vector<std::size_t> previous(m + 1, 0);
    std::vector<std::size_t> current(m + 1, 0);
    std::size_t bestLength = 0;
    std::size_t bestEnd = 0;

    for (std::size_t i = 1; i <= n; ++i) {
        for (std::size_t j = 1; j <= m; ++j) {
            current[j] = a[i - 1] == b[j - 1] ? previous[j - 1] + 1 : 0;
            if (current[j] > bestLength) {
                bestLength = current[j];
                bestEnd = i;
            }
        }
        previous.swap(current);
        std::fill(current.begin(), current.end(), std::size_t{0});
    }
    return a.substr(bestEnd - bestLength, bestLength);
}

// --- small utilities ---------------------------------------------------------

[[nodiscard]] inline bool areAnagrams(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    std::array<int, 256> counts{};
    counts.fill(0);
    for (char c : a) ++counts[static_cast<unsigned char>(c)];
    for (char c : b) {
        if (--counts[static_cast<unsigned char>(c)] < 0) return false;
    }
    return true;
}

/// True when `b` is `a` rotated by some amount. The one-line trick: a rotation
/// of `a` is a substring of `a + a`.
[[nodiscard]] inline bool isRotationOf(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    if (a.empty()) return true;
    return !knuthMorrisPratt(a + a, b).empty();
}

/// Length of the longest substring with no repeated character (sliding window).
[[nodiscard]] inline std::size_t longestUniqueSubstringLength(const std::string& text) {
    std::array<std::size_t, 256> lastSeen{};
    lastSeen.fill(0);
    std::array<bool, 256> seen{};
    seen.fill(false);

    std::size_t best = 0;
    std::size_t windowStart = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (seen[c] && lastSeen[c] >= windowStart) windowStart = lastSeen[c] + 1;
        seen[c] = true;
        lastSeen[c] = i;
        best = std::max(best, i - windowStart + 1);
    }
    return best;
}

}  // namespace daedalus

#endif  // DAEDALUS_ALGORITHMS_STRINGS_HPP
