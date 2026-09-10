// ============================================================================
//  Daedalus :: net/Router.hpp
//
//  Path routing on a trie -- the same structure as trees/Trie.hpp, one node per
//  path SEGMENT instead of per character.
//
//  That choice is the reason this file is in a data structures project rather
//  than being a list of if-statements: matching costs O(number of segments),
//  independent of how many routes are registered, and the segment trie gives
//  the correct precedence for free. A static segment always wins over a
//  parameter, and a parameter always wins over a wildcard, because they are
//  tried in that order at each node:
//
//      /api/users/me        static   -> wins
//      /api/users/:id       parameter
//      /static/*path        wildcard, matches the whole remaining path
//
//  Middleware wraps the matched handler in reverse registration order, so the
//  first registered middleware is the outermost -- the same nesting as every
//  other framework, and the one people expect.
// ============================================================================
#ifndef DAEDALUS_NET_ROUTER_HPP
#define DAEDALUS_NET_ROUTER_HPP

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "daedalus/core/Exception.hpp"
#include "daedalus/net/Http.hpp"

namespace daedalus::net {

using Handler = std::function<HttpResponse(const HttpRequest&)>;

/// A middleware receives the request and a continuation. Calling `next` runs
/// the rest of the chain; not calling it short-circuits (which is how the auth
/// middleware returns 401 without ever reaching the handler).
using Next = std::function<HttpResponse(const HttpRequest&)>;
using Middleware = std::function<HttpResponse(const HttpRequest&, const Next&)>;

class Router {
public:
    Router() : root_(std::make_unique<Node>()) {}

    // --- registration --------------------------------------------------------

    Router& add(const std::string& method, const std::string& pattern, Handler handler) {
        require(!pattern.empty() && pattern[0] == '/', "route pattern must start with '/'");

        Node* node = root_.get();
        for (const std::string& segment : splitPath(pattern)) {
            if (!segment.empty() && segment[0] == ':') {
                if (!node->parameterChild) {
                    node->parameterChild = std::make_unique<Node>();
                    node->parameterName = segment.substr(1);
                }
                node = node->parameterChild.get();
            } else if (!segment.empty() && segment[0] == '*') {
                if (!node->wildcardChild) {
                    node->wildcardChild = std::make_unique<Node>();
                    node->wildcardName = segment.substr(1);
                }
                node = node->wildcardChild.get();
                node->terminalWildcard = true;
            } else {
                auto& child = node->staticChildren[segment];
                if (!child) child = std::make_unique<Node>();
                node = child.get();
            }
        }
        node->handlers[method] = std::move(handler);
        ++routeCount_;
        return *this;
    }

    Router& get(const std::string& pattern, Handler handler) {
        return add("GET", pattern, std::move(handler));
    }
    Router& post(const std::string& pattern, Handler handler) {
        return add("POST", pattern, std::move(handler));
    }
    Router& put(const std::string& pattern, Handler handler) {
        return add("PUT", pattern, std::move(handler));
    }
    Router& remove(const std::string& pattern, Handler handler) {
        return add("DELETE", pattern, std::move(handler));
    }

    /// Registers a middleware. Runs outermost-first in registration order.
    Router& use(Middleware middleware) {
        middleware_.push_back(std::move(middleware));
        return *this;
    }

    /// Handler used when nothing matches. Defaults to a JSON 404.
    Router& setNotFoundHandler(Handler handler) {
        notFound_ = std::move(handler);
        return *this;
    }

    [[nodiscard]] std::size_t routeCount() const noexcept { return routeCount_; }

    // --- dispatch ------------------------------------------------------------

    /// Matches and runs a request. Path parameters are written into a copy of
    /// the request, so the handler sees them.
    [[nodiscard]] HttpResponse dispatch(const HttpRequest& request) const {
        HttpRequest matched = request;

        const Node* node = find(root_.get(), splitPath(request.path), 0, matched);
        if (node == nullptr) return runChain(matched, notFound_);

        const auto handler = node->handlers.find(request.method);
        if (handler == node->handlers.end()) {
            // The path exists but not for this method: that is a 405, and the
            // Allow header is required by the spec.
            std::string allowed;
            for (const auto& entry : node->handlers) {
                if (!allowed.empty()) allowed += ", ";
                allowed += entry.first;
            }
            const std::string allowHeader = allowed;
            Handler methodNotAllowed = [allowHeader](const HttpRequest&) {
                return HttpResponse::error(405, "method not allowed")
                    .setHeader("Allow", allowHeader);
            };
            return runChain(matched, methodNotAllowed);
        }
        return runChain(matched, handler->second);
    }

    /// Every registered route, as "METHOD /pattern". Sorted, for the CLI and
    /// for a self-describing /api/routes endpoint.
    [[nodiscard]] std::vector<std::string> routes() const {
        std::vector<std::string> out;
        collect(root_.get(), "", out);
        std::sort(out.begin(), out.end());
        return out;
    }

private:
    struct Node {
        std::map<std::string, std::unique_ptr<Node>> staticChildren;
        std::unique_ptr<Node> parameterChild;
        std::unique_ptr<Node> wildcardChild;
        std::string parameterName;
        std::string wildcardName;
        bool terminalWildcard{false};
        std::map<std::string, Handler> handlers;
    };

    [[nodiscard]] static std::vector<std::string> splitPath(const std::string& path) {
        std::vector<std::string> segments;
        std::size_t start = 0;
        while (start < path.size()) {
            if (path[start] == '/') {
                ++start;
                continue;
            }
            const std::size_t end = path.find('/', start);
            segments.push_back(
                path.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        return segments;
    }

    /// Depth-first match, trying static then parameter then wildcard so the
    /// most specific route always wins.
    [[nodiscard]] static const Node* find(const Node* node,
                                          const std::vector<std::string>& segments,
                                          std::size_t index, HttpRequest& request) {
        if (node == nullptr) return nullptr;
        if (index == segments.size()) return node->handlers.empty() ? nullptr : node;

        const std::string& segment = segments[index];

        const auto staticChild = node->staticChildren.find(segment);
        if (staticChild != node->staticChildren.end()) {
            if (const Node* found = find(staticChild->second.get(), segments, index + 1, request)) {
                return found;
            }
        }

        if (node->parameterChild) {
            HttpRequest attempt = request;
            attempt.pathParameters[node->parameterName] = segment;
            if (const Node* found =
                    find(node->parameterChild.get(), segments, index + 1, attempt)) {
                request = attempt;   // only commit the binding on a full match
                return found;
            }
        }

        if (node->wildcardChild) {
            std::string rest;
            for (std::size_t i = index; i < segments.size(); ++i) {
                if (!rest.empty()) rest += '/';
                rest += segments[i];
            }
            request.pathParameters[node->wildcardName] = rest;
            return node->wildcardChild->handlers.empty() ? nullptr : node->wildcardChild.get();
        }
        return nullptr;
    }

    [[nodiscard]] HttpResponse runChain(const HttpRequest& request, const Handler& handler) const {
        // Build the chain from the inside out so index 0 ends up outermost.
        Next next = [&handler](const HttpRequest& inner) { return handler(inner); };
        for (std::size_t i = middleware_.size(); i-- > 0;) {
            const Middleware& layer = middleware_[i];
            Next inner = next;
            next = [&layer, inner](const HttpRequest& r) { return layer(r, inner); };
        }
        return next(request);
    }

    static void collect(const Node* node, const std::string& prefix,
                        std::vector<std::string>& out) {
        for (const auto& entry : node->handlers) {
            out.push_back(entry.first + " " + (prefix.empty() ? "/" : prefix));
        }
        for (const auto& child : node->staticChildren) {
            collect(child.second.get(), prefix + "/" + child.first, out);
        }
        if (node->parameterChild) {
            collect(node->parameterChild.get(), prefix + "/:" + node->parameterName, out);
        }
        if (node->wildcardChild) {
            collect(node->wildcardChild.get(), prefix + "/*" + node->wildcardName, out);
        }
    }

    std::unique_ptr<Node> root_;
    std::vector<Middleware> middleware_;
    std::size_t routeCount_{0};
    Handler notFound_ = [](const HttpRequest&) { return HttpResponse::error(404, "not found"); };
};

}   // namespace daedalus::net

#endif   // DAEDALUS_NET_ROUTER_HPP
