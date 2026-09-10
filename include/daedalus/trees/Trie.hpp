// ============================================================================
//  Daedalus :: trees/Trie.hpp
//
//  Prefix tree over std::string. Lookup cost depends on the length of the key,
//  not on how many keys are stored -- which is what makes it the right
//  structure for autocomplete, routing tables and dictionary work where a hash
//  map cannot answer "what starts with this".
//
//  Children live in a std::map rather than a fixed 26-slot array, so the trie
//  accepts any byte value and enumerates completions in sorted order.
//
//  Deletion prunes: nodes that stop being useful are freed on the way back up.
//
//  Complexity: insert/contains/erase O(L) | prefix listing O(L + output)
//              space O(total characters stored)
// ============================================================================
#ifndef DAEDALUS_TREES_TRIE_HPP
#define DAEDALUS_TREES_TRIE_HPP

#include <cstddef>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Container.hpp"
#include "daedalus/core/Exception.hpp"

namespace daedalus {

class Trie final : public Collection<std::string> {
    struct Node {
        std::map<char, Node*> children;
        bool terminal{false};          ///< a stored word ends here
        std::size_t subtreeWords{0};   ///< words stored at or below this node

        ~Node() {
            for (auto& entry : children) delete entry.second;
        }
    };

public:
    using size_type = std::size_t;

    Trie() : root_(new Node()) {}

    Trie(std::initializer_list<std::string> words) : Trie() {
        for (const std::string& word : words) insert(word);
    }

    Trie(const Trie& other) : Trie() {
        for (const std::string& word : other.words()) insert(word);
    }

    Trie(Trie&& other) noexcept : root_(other.root_), size_(other.size_) {
        other.root_ = new Node();
        other.size_ = 0;
    }

    Trie& operator=(Trie other) noexcept {
        std::swap(root_, other.root_);
        std::swap(size_, other.size_);
        return *this;
    }

    ~Trie() override {
        delete root_;
        root_ = nullptr;
    }

    [[nodiscard]] size_type size() const noexcept override { return size_; }
    [[nodiscard]] bool empty() const noexcept override { return size_ == 0; }
    [[nodiscard]] std::string name() const override { return "Trie"; }

    void clear() override {
        delete root_;
        root_ = new Node();
        size_ = 0;
    }

    /// Stores `word`. Re-inserting an existing word is a no-op.
    void insert(const std::string& word) override {
        if (contains(word)) return;
        Node* node = root_;
        ++node->subtreeWords;
        for (char letter : word) {
            Node*& child = node->children[letter];
            if (child == nullptr) child = new Node();
            node = child;
            ++node->subtreeWords;
        }
        node->terminal = true;
        ++size_;
    }

    /// Removes `word`, pruning nodes that no longer lead anywhere.
    bool erase(const std::string& word) override {
        if (!contains(word)) return false;
        eraseFrom(root_, word, 0);
        --size_;
        return true;
    }

    [[nodiscard]] bool contains(const std::string& word) const override {
        const Node* node = descend(word);
        return node != nullptr && node->terminal;
    }

    /// True when any stored word begins with `prefix` (including a word equal
    /// to it). This is the query a hash map cannot answer.
    [[nodiscard]] bool startsWith(const std::string& prefix) const {
        return descend(prefix) != nullptr;
    }

    /// How many stored words begin with `prefix`. O(L) thanks to the per-node
    /// counter, rather than O(L + matches).
    [[nodiscard]] size_type countWithPrefix(const std::string& prefix) const {
        const Node* node = descend(prefix);
        return node == nullptr ? 0 : node->subtreeWords;
    }

    /// Every stored word beginning with `prefix`, in lexicographic order.
    [[nodiscard]] std::vector<std::string> withPrefix(const std::string& prefix) const {
        std::vector<std::string> out;
        const Node* node = descend(prefix);
        if (node == nullptr) return out;
        std::string buffer = prefix;
        collect(node, buffer, out);
        return out;
    }

    /// All stored words, lexicographically ordered.
    [[nodiscard]] std::vector<std::string> words() const { return withPrefix(""); }

    [[nodiscard]] std::vector<std::string> toVector() const override { return words(); }

    /// Longest prefix shared by every stored word. Empty when the trie is
    /// empty or the words diverge immediately.
    [[nodiscard]] std::string longestCommonPrefix() const {
        std::string prefix;
        const Node* node = root_;
        while (node != nullptr && node->children.size() == 1 && !node->terminal) {
            const auto& entry = *node->children.begin();
            prefix.push_back(entry.first);
            node = entry.second;
        }
        return prefix;
    }

    /// Longest stored word that is a prefix of `text` -- the lookup a router or
    /// a tokenizer performs.
    [[nodiscard]] std::string longestPrefixOf(const std::string& text) const {
        const Node* node = root_;
        std::size_t bestLength = 0;
        std::size_t depth = 0;
        for (char letter : text) {
            const auto found = node->children.find(letter);
            if (found == node->children.end()) break;
            node = found->second;
            ++depth;
            if (node->terminal) bestLength = depth;
        }
        return text.substr(0, bestLength);
    }

    /// Number of nodes, i.e. distinct prefixes plus the root.
    [[nodiscard]] size_type nodeCount() const { return countNodes(root_); }

private:
    [[nodiscard]] const Node* descend(const std::string& key) const {
        const Node* node = root_;
        for (char letter : key) {
            const auto found = node->children.find(letter);
            if (found == node->children.end()) return nullptr;
            node = found->second;
        }
        return node;
    }

    /// Returns true when `node` may be deleted by its parent.
    static bool eraseFrom(Node* node, const std::string& word, std::size_t index) {
        --node->subtreeWords;
        if (index == word.size()) {
            node->terminal = false;
        } else {
            const char letter = word[index];
            const auto found = node->children.find(letter);
            if (found != node->children.end() && eraseFrom(found->second, word, index + 1)) {
                delete found->second;
                node->children.erase(found);
            }
        }
        return !node->terminal && node->children.empty();
    }

    static void collect(const Node* node, std::string& buffer, std::vector<std::string>& out) {
        if (node->terminal) out.push_back(buffer);
        for (const auto& entry : node->children) {
            buffer.push_back(entry.first);
            collect(entry.second, buffer, out);
            buffer.pop_back();
        }
    }

    static size_type countNodes(const Node* node) {
        size_type total = 1;
        for (const auto& entry : node->children) total += countNodes(entry.second);
        return total;
    }

    Node* root_{nullptr};
    size_type size_{0};
};

}   // namespace daedalus

#endif   // DAEDALUS_TREES_TRIE_HPP
