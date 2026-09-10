// ============================================================================
//  Daedalus :: net/Json.hpp
//
//  A small JSON value type with a serialiser and a recursive-descent parser.
//  It exists because the demo API needs to talk JSON and the project has a
//  no-dependencies rule; it is not trying to be nlohmann/json.
//
//  What it does support: objects, arrays, strings (with the standard escapes
//  and \uXXXX decoded to UTF-8), numbers, booleans, null, and correct escaping
//  on output -- including the control characters and the forward slash cases
//  that a naive serialiser gets wrong and that turn into XSS when the output
//  lands inside a <script> tag.
//
//  What it does not: streaming, comments, big integers, or preserving the key
//  order of a parsed object (it sorts, so output is deterministic).
// ============================================================================
#ifndef DAEDALUS_NET_JSON_HPP
#define DAEDALUS_NET_JSON_HPP

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Exception.hpp"

namespace daedalus::net {

class Json;
using JsonObject = std::map<std::string, Json>;
using JsonArray = std::vector<Json>;

class Json {
public:
    enum class Kind { Null, Boolean, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool value) : kind_(Kind::Boolean), boolean_(value) {}
    Json(int value) : kind_(Kind::Number), number_(value) {}
    Json(long value) : kind_(Kind::Number), number_(static_cast<double>(value)) {}
    Json(long long value) : kind_(Kind::Number), number_(static_cast<double>(value)) {}
    Json(unsigned value) : kind_(Kind::Number), number_(value) {}
    Json(unsigned long value) : kind_(Kind::Number), number_(static_cast<double>(value)) {}
    Json(unsigned long long value) : kind_(Kind::Number), number_(static_cast<double>(value)) {}
    Json(double value) : kind_(Kind::Number), number_(value) {}
    Json(const char* value) : kind_(Kind::String), string_(value) {}
    Json(std::string value) : kind_(Kind::String), string_(std::move(value)) {}
    Json(JsonArray value) : kind_(Kind::Array), array_(std::move(value)) {}
    Json(JsonObject value) : kind_(Kind::Object), object_(std::move(value)) {}

    [[nodiscard]] static Json array(std::initializer_list<Json> values) {
        return Json(JsonArray(values));
    }

    [[nodiscard]] static Json object(
        std::initializer_list<std::pair<const std::string, Json>> entries) {
        return Json(JsonObject(entries));
    }

    [[nodiscard]] Kind kind() const noexcept { return kind_; }
    [[nodiscard]] bool isNull() const noexcept { return kind_ == Kind::Null; }
    [[nodiscard]] bool isObject() const noexcept { return kind_ == Kind::Object; }
    [[nodiscard]] bool isArray() const noexcept { return kind_ == Kind::Array; }
    [[nodiscard]] bool isString() const noexcept { return kind_ == Kind::String; }
    [[nodiscard]] bool isNumber() const noexcept { return kind_ == Kind::Number; }
    [[nodiscard]] bool isBoolean() const noexcept { return kind_ == Kind::Boolean; }

    // --- typed access, with a default rather than an exception ---------------

    [[nodiscard]] bool asBoolean(bool fallback = false) const {
        return kind_ == Kind::Boolean ? boolean_ : fallback;
    }

    [[nodiscard]] double asNumber(double fallback = 0.0) const {
        return kind_ == Kind::Number ? number_ : fallback;
    }

    [[nodiscard]] long long asInteger(long long fallback = 0) const {
        return kind_ == Kind::Number ? static_cast<long long>(number_) : fallback;
    }

    [[nodiscard]] std::string asString(const std::string& fallback = "") const {
        return kind_ == Kind::String ? string_ : fallback;
    }

    [[nodiscard]] const JsonArray& asArray() const {
        if (kind_ != Kind::Array) throw InvalidArgument("JSON value is not an array");
        return array_;
    }

    [[nodiscard]] const JsonObject& asObject() const {
        if (kind_ != Kind::Object) throw InvalidArgument("JSON value is not an object");
        return object_;
    }

    /// Object member access; returns a null Json when absent, so chained reads
    /// of a malformed request body cannot throw.
    [[nodiscard]] const Json& operator[](const std::string& key) const {
        static const Json kNull;
        if (kind_ != Kind::Object) return kNull;
        const auto found = object_.find(key);
        return found == object_.end() ? kNull : found->second;
    }

    [[nodiscard]] bool contains(const std::string& key) const {
        return kind_ == Kind::Object && object_.find(key) != object_.end();
    }

    Json& set(const std::string& key, Json value) {
        if (kind_ != Kind::Object) {
            kind_ = Kind::Object;
            object_.clear();
        }
        object_[key] = std::move(value);
        return *this;
    }

    Json& push(Json value) {
        if (kind_ != Kind::Array) {
            kind_ = Kind::Array;
            array_.clear();
        }
        array_.push_back(std::move(value));
        return *this;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        if (kind_ == Kind::Array) return array_.size();
        if (kind_ == Kind::Object) return object_.size();
        return 0;
    }

    // --- serialisation -------------------------------------------------------

    [[nodiscard]] std::string dump() const {
        std::ostringstream os;
        write(os);
        return os.str();
    }

    /// Escapes a string for safe inclusion in JSON. Control characters become
    /// \u00XX, and '<' '>' '&' are escaped too so the output stays safe inside
    /// an HTML <script> block.
    [[nodiscard]] static std::string escape(const std::string& text) {
        std::ostringstream os;
        for (char raw : text) {
            const unsigned char c = static_cast<unsigned char>(raw);
            switch (c) {
                case '"': os << "\\\""; break;
                case '\\': os << "\\\\"; break;
                case '\b': os << "\\b"; break;
                case '\f': os << "\\f"; break;
                case '\n': os << "\\n"; break;
                case '\r': os << "\\r"; break;
                case '\t': os << "\\t"; break;
                case '<': os << "\\u003c"; break;
                case '>': os << "\\u003e"; break;
                case '&': os << "\\u0026"; break;
                default:
                    if (c < 0x20) {
                        static constexpr char kDigits[] = "0123456789abcdef";
                        os << "\\u00" << kDigits[(c >> 4) & 0x0Fu] << kDigits[c & 0x0Fu];
                    } else {
                        os << static_cast<char>(c);
                    }
            }
        }
        return os.str();
    }

    // --- parsing -------------------------------------------------------------

    /// Parses `text`. Throws InvalidArgument on malformed input rather than
    /// returning a partially built value.
    [[nodiscard]] static Json parse(const std::string& text) {
        std::size_t position = 0;
        Json value = parseValue(text, position);
        skipWhitespace(text, position);
        if (position != text.size()) throw InvalidArgument("trailing characters after JSON value");
        return value;
    }

    /// Non-throwing parse, for request bodies that may be anything at all.
    [[nodiscard]] static Json tryParse(const std::string& text) {
        try {
            return parse(text);
        } catch (const std::exception&) {
            return Json{};
        }
    }

private:
    void write(std::ostringstream& os) const {
        switch (kind_) {
            case Kind::Null: os << "null"; return;
            case Kind::Boolean: os << (boolean_ ? "true" : "false"); return;
            case Kind::Number: {
                // Render whole numbers without a decimal point so ids and
                // counts do not come out as "3.000000".
                const long long rounded = static_cast<long long>(number_);
                if (static_cast<double>(rounded) == number_) {
                    os << rounded;
                } else {
                    std::ostringstream digits;
                    digits.precision(15);
                    digits << number_;
                    os << digits.str();
                }
                return;
            }
            case Kind::String: os << '"' << escape(string_) << '"'; return;
            case Kind::Array: {
                os << '[';
                for (std::size_t i = 0; i < array_.size(); ++i) {
                    if (i > 0) os << ',';
                    array_[i].write(os);
                }
                os << ']';
                return;
            }
            case Kind::Object: {
                os << '{';
                bool first = true;
                for (const auto& entry : object_) {
                    if (!first) os << ',';
                    os << '"' << escape(entry.first) << "\":";
                    entry.second.write(os);
                    first = false;
                }
                os << '}';
                return;
            }
        }
    }

    static void skipWhitespace(const std::string& text, std::size_t& position) {
        while (position < text.size() && (text[position] == ' ' || text[position] == '\t' ||
                                          text[position] == '\n' || text[position] == '\r')) {
            ++position;
        }
    }

    static void expect(const std::string& text, std::size_t& position, char character) {
        skipWhitespace(text, position);
        if (position >= text.size() || text[position] != character) {
            throw InvalidArgument(std::string("expected '") + character + "' in JSON");
        }
        ++position;
    }

    static Json parseValue(const std::string& text, std::size_t& position) {
        skipWhitespace(text, position);
        if (position >= text.size()) throw InvalidArgument("unexpected end of JSON input");

        switch (text[position]) {
            case '{': return parseObject(text, position);
            case '[': return parseArray(text, position);
            case '"': return Json(parseString(text, position));
            case 't': requireLiteral(text, position, "true"); return Json(true);
            case 'f': requireLiteral(text, position, "false"); return Json(false);
            case 'n': requireLiteral(text, position, "null"); return Json();
            default: return Json(parseNumber(text, position));
        }
    }

    static void requireLiteral(const std::string& text, std::size_t& position,
                               const std::string& literal) {
        if (text.compare(position, literal.size(), literal) != 0) {
            throw InvalidArgument("invalid JSON literal");
        }
        position += literal.size();
    }

    static Json parseObject(const std::string& text, std::size_t& position) {
        expect(text, position, '{');
        JsonObject entries;
        skipWhitespace(text, position);
        if (position < text.size() && text[position] == '}') {
            ++position;
            return Json(std::move(entries));
        }
        for (;;) {
            skipWhitespace(text, position);
            const std::string key = parseString(text, position);
            expect(text, position, ':');
            entries[key] = parseValue(text, position);
            skipWhitespace(text, position);
            if (position < text.size() && text[position] == ',') {
                ++position;
                continue;
            }
            expect(text, position, '}');
            return Json(std::move(entries));
        }
    }

    static Json parseArray(const std::string& text, std::size_t& position) {
        expect(text, position, '[');
        JsonArray values;
        skipWhitespace(text, position);
        if (position < text.size() && text[position] == ']') {
            ++position;
            return Json(std::move(values));
        }
        for (;;) {
            values.push_back(parseValue(text, position));
            skipWhitespace(text, position);
            if (position < text.size() && text[position] == ',') {
                ++position;
                continue;
            }
            expect(text, position, ']');
            return Json(std::move(values));
        }
    }

    static std::string parseString(const std::string& text, std::size_t& position) {
        expect(text, position, '"');
        std::string value;
        while (position < text.size() && text[position] != '"') {
            if (text[position] != '\\') {
                value += text[position++];
                continue;
            }
            ++position;
            if (position >= text.size()) throw InvalidArgument("unterminated JSON escape");
            const char escaped = text[position++];
            switch (escaped) {
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                case '/': value += '/'; break;
                case 'b': value += '\b'; break;
                case 'f': value += '\f'; break;
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                case 'u': value += decodeUnicodeEscape(text, position); break;
                default: throw InvalidArgument("invalid JSON escape sequence");
            }
        }
        if (position >= text.size()) throw InvalidArgument("unterminated JSON string");
        ++position;
        return value;
    }

    /// Decodes \uXXXX (already past the 'u') into UTF-8.
    static std::string decodeUnicodeEscape(const std::string& text, std::size_t& position) {
        if (position + 4 > text.size()) throw InvalidArgument("truncated \\u escape");
        std::uint32_t codepoint = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text[position++];
            codepoint *= 16;
            if (c >= '0' && c <= '9') {
                codepoint += static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                codepoint += static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                codepoint += static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                throw InvalidArgument("invalid \\u escape digit");
            }
        }

        std::string out;
        if (codepoint < 0x80) {
            out += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            out += static_cast<char>(0xC0u | (codepoint >> 6));
            out += static_cast<char>(0x80u | (codepoint & 0x3Fu));
        } else {
            out += static_cast<char>(0xE0u | (codepoint >> 12));
            out += static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu));
            out += static_cast<char>(0x80u | (codepoint & 0x3Fu));
        }
        return out;
    }

    static double parseNumber(const std::string& text, std::size_t& position) {
        const std::size_t start = position;
        if (position < text.size() && (text[position] == '-' || text[position] == '+')) ++position;
        while (position < text.size() &&
               ((text[position] >= '0' && text[position] <= '9') || text[position] == '.' ||
                text[position] == 'e' || text[position] == 'E' || text[position] == '-' ||
                text[position] == '+')) {
            ++position;
        }
        if (position == start) throw InvalidArgument("expected a JSON number");
        try {
            return std::stod(text.substr(start, position - start));
        } catch (const std::exception&) {
            throw InvalidArgument("malformed JSON number");
        }
    }

    Kind kind_{Kind::Null};
    bool boolean_{false};
    double number_{0.0};
    std::string string_;
    JsonArray array_;
    JsonObject object_;
};

}   // namespace daedalus::net

#endif   // DAEDALUS_NET_JSON_HPP
