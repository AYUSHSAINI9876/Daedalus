// ============================================================================
//  Daedalus :: net/Http.hpp
//
//  HTTP/1.1 request parsing and response building. Transport-independent: this
//  header knows nothing about sockets, which is what makes the parser directly
//  unit-testable against hand-written byte strings.
//
//  Parsing is deliberately strict about the things that cause security bugs:
//    - the request line and header block are size-capped, so a client cannot
//      exhaust memory by never sending a newline
//    - Content-Length is validated and capped
//    - header names are case-folded, because HTTP says they are insensitive
//      and a router that forgets is trivially bypassed
//    - percent-decoding rejects malformed escapes rather than passing them
//      through
//    - path normalisation rejects ".." outright, which is the directory
//      traversal defence for the static file handler
// ============================================================================
#ifndef DAEDALUS_NET_HTTP_HPP
#define DAEDALUS_NET_HTTP_HPP

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Exception.hpp"
#include "daedalus/net/Json.hpp"

namespace daedalus::net {

/// Header maps are case-insensitive, as HTTP requires.
struct CaseInsensitiveLess {
    bool operator()(const std::string& a, const std::string& b) const {
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(),
            [](unsigned char x, unsigned char y) { return std::tolower(x) < std::tolower(y); });
    }
};

using Headers = std::map<std::string, std::string, CaseInsensitiveLess>;

// --- encoding helpers --------------------------------------------------------

/// Percent-decodes a URL component. '+' becomes a space only in query strings,
/// which is why that is a parameter rather than always-on.
[[nodiscard]] inline std::string percentDecode(const std::string& text, bool plusIsSpace = false) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (plusIsSpace && text[i] == '+') {
            out += ' ';
            continue;
        }
        if (text[i] != '%') {
            out += text[i];
            continue;
        }
        if (i + 2 >= text.size()) throw InvalidArgument("truncated percent-escape in URL");
        const auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            throw InvalidArgument("invalid percent-escape in URL");
        };
        out += static_cast<char>(nibble(text[i + 1]) * 16 + nibble(text[i + 2]));
        i += 2;
    }
    return out;
}

[[nodiscard]] inline std::string percentEncode(const std::string& text) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    for (char raw : text) {
        const unsigned char c = static_cast<unsigned char>(raw);
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                                c == '~';
        if (unreserved) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kDigits[(c >> 4) & 0x0Fu];
            out += kDigits[c & 0x0Fu];
        }
    }
    return out;
}

/// Splits "a=1&b=two%20words" into a map, percent-decoding both sides.
[[nodiscard]] inline std::map<std::string, std::string> parseQueryString(const std::string& query) {
    std::map<std::string, std::string> parameters;
    std::size_t start = 0;
    while (start < query.size()) {
        std::size_t end = query.find('&', start);
        if (end == std::string::npos) end = query.size();

        const std::string pair = query.substr(start, end - start);
        const std::size_t equals = pair.find('=');
        if (equals == std::string::npos) {
            if (!pair.empty()) parameters[percentDecode(pair, true)] = "";
        } else {
            parameters[percentDecode(pair.substr(0, equals), true)] =
                percentDecode(pair.substr(equals + 1), true);
        }
        start = end + 1;
    }
    return parameters;
}

/// Rejects any path containing a ".." segment, a NUL, or a backslash. Returns
/// the cleaned path. This is the whole directory-traversal defence, so it
/// refuses rather than trying to sanitise.
[[nodiscard]] inline std::string normalisePath(const std::string& path) {
    if (path.find('\0') != std::string::npos) throw InvalidArgument("path contains a NUL byte");
    if (path.find('\\') != std::string::npos) throw InvalidArgument("path contains a backslash");

    // Split into segments and rejoin. Editing the string in place looks
    // cheaper but gets the separators wrong: dropping a "." segment has to
    // drop one of its two surrounding slashes as well.
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string segment =
            path.substr(start, end == std::string::npos ? std::string::npos : end - start);

        if (segment == "..") throw InvalidArgument("path escapes the document root");
        if (!segment.empty() && segment != ".") segments.push_back(segment);

        if (end == std::string::npos) break;
        start = end + 1;
    }

    std::string cleaned;
    for (const std::string& segment : segments) {
        cleaned += '/';
        cleaned += segment;
    }
    return cleaned.empty() ? "/" : cleaned;
}

/// Content type from a file extension. Unknown types get the safest default:
/// application/octet-stream is downloaded rather than executed.
[[nodiscard]] inline std::string contentTypeFor(const std::string& path) {
    const std::size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    std::string extension = path.substr(dot + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (extension == "html" || extension == "htm") return "text/html; charset=utf-8";
    if (extension == "css") return "text/css; charset=utf-8";
    if (extension == "js" || extension == "mjs") return "application/javascript; charset=utf-8";
    if (extension == "json") return "application/json; charset=utf-8";
    if (extension == "svg") return "image/svg+xml";
    if (extension == "png") return "image/png";
    if (extension == "jpg" || extension == "jpeg") return "image/jpeg";
    if (extension == "gif") return "image/gif";
    if (extension == "ico") return "image/x-icon";
    if (extension == "txt" || extension == "md") return "text/plain; charset=utf-8";
    if (extension == "woff2") return "font/woff2";
    return "application/octet-stream";
}

// --- request -----------------------------------------------------------------

struct HttpRequest {
    std::string method;
    std::string target;   ///< the raw request target, query string included
    std::string path;     ///< decoded and normalised
    std::string version;
    Headers headers;
    std::map<std::string, std::string> query;
    std::map<std::string, std::string> pathParameters;   ///< filled by the router
    std::string body;
    std::string clientAddress;

    [[nodiscard]] std::string header(const std::string& name,
                                     const std::string& fallback = "") const {
        const auto found = headers.find(name);
        return found == headers.end() ? fallback : found->second;
    }

    [[nodiscard]] std::string queryParameter(const std::string& name,
                                             const std::string& fallback = "") const {
        const auto found = query.find(name);
        return found == query.end() ? fallback : found->second;
    }

    [[nodiscard]] std::string pathParameter(const std::string& name,
                                            const std::string& fallback = "") const {
        const auto found = pathParameters.find(name);
        return found == pathParameters.end() ? fallback : found->second;
    }

    [[nodiscard]] Json json() const { return Json::tryParse(body); }

    /// Reads one cookie by name from the Cookie header.
    [[nodiscard]] std::string cookie(const std::string& name) const {
        const std::string jar = header("Cookie");
        std::size_t start = 0;
        while (start < jar.size()) {
            std::size_t end = jar.find(';', start);
            if (end == std::string::npos) end = jar.size();
            std::string pair = jar.substr(start, end - start);

            const std::size_t first = pair.find_first_not_of(" \t");
            if (first != std::string::npos) pair = pair.substr(first);
            const std::size_t equals = pair.find('=');
            if (equals != std::string::npos && pair.substr(0, equals) == name) {
                return pair.substr(equals + 1);
            }
            start = end + 1;
        }
        return "";
    }

    [[nodiscard]] bool wantsKeepAlive() const {
        std::string connection = header("Connection");
        std::transform(connection.begin(), connection.end(), connection.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (connection == "close") return false;
        if (version == "HTTP/1.0") return connection == "keep-alive";
        return true;   // HTTP/1.1 defaults to persistent
    }
};

/// Limits applied while parsing, so a hostile client cannot exhaust memory.
struct HttpLimits {
    std::size_t maximumRequestLine{8 * 1024};
    std::size_t maximumHeaderBlock{32 * 1024};
    std::size_t maximumBody{4 * 1024 * 1024};
    std::size_t maximumHeaderCount{100};
};

/// Parses a complete request. Throws InvalidArgument on anything malformed --
/// the caller turns that into a 400.
[[nodiscard]] inline HttpRequest parseRequest(const std::string& raw,
                                              const HttpLimits& limits = HttpLimits{}) {
    const std::size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) throw InvalidArgument("request headers are incomplete");
    if (headerEnd > limits.maximumHeaderBlock) throw InvalidArgument("request headers too large");

    HttpRequest request;
    std::istringstream stream(raw.substr(0, headerEnd));
    std::string line;

    if (!std::getline(stream, line)) throw InvalidArgument("empty request");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() > limits.maximumRequestLine) throw InvalidArgument("request line too long");

    // "METHOD SP TARGET SP VERSION"
    const std::size_t firstSpace = line.find(' ');
    const std::size_t secondSpace = line.rfind(' ');
    if (firstSpace == std::string::npos || secondSpace == firstSpace) {
        throw InvalidArgument("malformed request line");
    }
    request.method = line.substr(0, firstSpace);
    request.target = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    request.version = line.substr(secondSpace + 1);
    if (request.version != "HTTP/1.0" && request.version != "HTTP/1.1") {
        throw InvalidArgument("unsupported HTTP version");
    }

    const std::size_t questionMark = request.target.find('?');
    if (questionMark == std::string::npos) {
        request.path = normalisePath(percentDecode(request.target));
    } else {
        request.path = normalisePath(percentDecode(request.target.substr(0, questionMark)));
        request.query = parseQueryString(request.target.substr(questionMark + 1));
    }

    std::size_t headerCount = 0;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        if (++headerCount > limits.maximumHeaderCount) {
            throw InvalidArgument("too many request headers");
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) throw InvalidArgument("malformed header line");

        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        const std::size_t firstValue = value.find_first_not_of(" \t");
        value = firstValue == std::string::npos ? "" : value.substr(firstValue);
        const std::size_t lastValue = value.find_last_not_of(" \t");
        if (lastValue != std::string::npos) value = value.substr(0, lastValue + 1);
        request.headers[name] = value;
    }

    const std::string lengthHeader = request.header("Content-Length");
    if (!lengthHeader.empty()) {
        std::size_t declared = 0;
        try {
            declared = static_cast<std::size_t>(std::stoull(lengthHeader));
        } catch (const std::exception&) {
            throw InvalidArgument("malformed Content-Length");
        }
        if (declared > limits.maximumBody) throw InvalidArgument("request body too large");
        const std::string available = raw.substr(headerEnd + 4);
        if (available.size() < declared) throw InvalidArgument("request body is incomplete");
        request.body = available.substr(0, declared);
    }
    return request;
}

/// How many bytes of body are still expected given the headers seen so far, or
/// nullopt when the header block itself is incomplete. The socket loop uses
/// this to know when to stop reading.
[[nodiscard]] inline std::optional<std::size_t> pendingBodyBytes(const std::string& sofar) {
    const std::size_t headerEnd = sofar.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return std::nullopt;

    // Find Content-Length without a full parse.
    std::istringstream stream(sofar.substr(0, headerEnd));
    std::string line;
    std::size_t declared = 0;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name != "content-length") continue;
        try {
            declared = static_cast<std::size_t>(std::stoull(line.substr(colon + 1)));
        } catch (const std::exception&) {
            return 0;
        }
    }
    const std::size_t haveBody = sofar.size() - (headerEnd + 4);
    return declared > haveBody ? declared - haveBody : 0;
}

// --- response ----------------------------------------------------------------

class HttpResponse {
public:
    HttpResponse() = default;

    explicit HttpResponse(int status) : status_(status) {}

    // --- factories -----------------------------------------------------------

    [[nodiscard]] static HttpResponse text(const std::string& body, int status = 200) {
        HttpResponse response(status);
        response.body_ = body;
        response.headers_["Content-Type"] = "text/plain; charset=utf-8";
        return response;
    }

    [[nodiscard]] static HttpResponse html(const std::string& body, int status = 200) {
        HttpResponse response(status);
        response.body_ = body;
        response.headers_["Content-Type"] = "text/html; charset=utf-8";
        return response;
    }

    [[nodiscard]] static HttpResponse json(const Json& value, int status = 200) {
        HttpResponse response(status);
        response.body_ = value.dump();
        response.headers_["Content-Type"] = "application/json; charset=utf-8";
        return response;
    }

    [[nodiscard]] static HttpResponse error(int status, const std::string& message) {
        Json body;
        body.set("error", message).set("status", status);
        return json(body, status);
    }

    [[nodiscard]] static HttpResponse redirect(const std::string& location, int status = 302) {
        HttpResponse response(status);
        response.headers_["Location"] = location;
        return response;
    }

    [[nodiscard]] static HttpResponse file(const std::string& body, const std::string& path,
                                           int status = 200) {
        HttpResponse response(status);
        response.body_ = body;
        response.headers_["Content-Type"] = contentTypeFor(path);
        return response;
    }

    // --- mutation ------------------------------------------------------------

    HttpResponse& setStatus(int status) {
        status_ = status;
        return *this;
    }

    HttpResponse& setHeader(const std::string& name, const std::string& value) {
        headers_[name] = value;
        return *this;
    }

    HttpResponse& setBody(std::string body) {
        body_ = std::move(body);
        return *this;
    }

    /// Adds a Set-Cookie. HttpOnly and SameSite=Strict are on by default:
    /// HttpOnly keeps the session token away from any injected script, and
    /// SameSite=Strict is the CSRF defence for a cookie-authenticated API.
    HttpResponse& setCookie(const std::string& name, const std::string& value,
                            long long maximumAgeSeconds = -1, bool httpOnly = true,
                            bool secure = false, const std::string& sameSite = "Strict") {
        std::ostringstream cookie;
        cookie << name << "=" << value << "; Path=/";
        if (maximumAgeSeconds >= 0) cookie << "; Max-Age=" << maximumAgeSeconds;
        if (httpOnly) cookie << "; HttpOnly";
        if (secure) cookie << "; Secure";
        if (!sameSite.empty()) cookie << "; SameSite=" << sameSite;
        cookies_.push_back(cookie.str());
        return *this;
    }

    HttpResponse& clearCookie(const std::string& name) {
        cookies_.push_back(name + "=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
        return *this;
    }

    /// Headers every response should carry. Content-Security-Policy is the one
    /// that matters: it stops an injected <script> from running at all.
    HttpResponse& withSecurityHeaders() {
        headers_["X-Content-Type-Options"] = "nosniff";
        headers_["X-Frame-Options"] = "DENY";
        headers_["Referrer-Policy"] = "no-referrer";
        headers_["Content-Security-Policy"] =
            "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; "
            "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'";
        return *this;
    }

    [[nodiscard]] int status() const noexcept { return status_; }
    [[nodiscard]] const std::string& body() const noexcept { return body_; }
    [[nodiscard]] const Headers& headers() const noexcept { return headers_; }

    [[nodiscard]] static std::string reasonPhrase(int status) {
        switch (status) {
            case 200: return "OK";
            case 201: return "Created";
            case 204: return "No Content";
            case 302: return "Found";
            case 304: return "Not Modified";
            case 400: return "Bad Request";
            case 401: return "Unauthorized";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 409: return "Conflict";
            case 413: return "Payload Too Large";
            case 422: return "Unprocessable Entity";
            case 429: return "Too Many Requests";
            case 500: return "Internal Server Error";
            case 501: return "Not Implemented";
            case 503: return "Service Unavailable";
            default: return "Unknown";
        }
    }

    /// Serialises the whole response, Content-Length included.
    [[nodiscard]] std::string serialise(bool keepAlive = true) const {
        std::ostringstream out;
        out << "HTTP/1.1 " << status_ << " " << reasonPhrase(status_) << "\r\n";
        out << "Content-Length: " << body_.size() << "\r\n";
        out << "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n";
        for (const auto& header : headers_) {
            out << header.first << ": " << header.second << "\r\n";
        }
        for (const std::string& cookie : cookies_) {
            out << "Set-Cookie: " << cookie << "\r\n";
        }
        out << "\r\n" << body_;
        return out.str();
    }

private:
    int status_{200};
    std::string body_;
    Headers headers_;
    std::vector<std::string> cookies_;
};

}   // namespace daedalus::net

#endif   // DAEDALUS_NET_HTTP_HPP
