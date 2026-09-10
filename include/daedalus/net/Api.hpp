// ============================================================================
//  Daedalus :: net/Api.hpp
//
//  The demo application: a session-authenticated JSON API over the library,
//  plus a static file handler for the web playground.
//
//  Route map
//    GET  /                         the playground (index.html)
//    GET  /static/*path             CSS and JS, LRU-cached, traversal-proof
//    GET  /api/health               liveness, unauthenticated
//    GET  /api/routes               self-describing route list
//
//    POST /api/auth/register        create an account
//    POST /api/auth/login           issue a session cookie
//    POST /api/auth/logout          revoke it
//    GET  /api/auth/me              who am I
//
//    POST /api/sort                 run a sorting strategy, with metrics
//    POST /api/tree                 build a search tree, get traversals + art
//    POST /api/graph                run a graph algorithm
//    POST /api/strings              run a string algorithm
//    GET  /api/algorithms           what the playground can offer
//
//    GET  /api/admin/users          admin only
//    GET  /api/admin/audit          admin only
//
//  Authorisation is a middleware, not a check repeated in every handler: any
//  path under /api/ that is not on the public list requires a valid session,
//  and /api/admin/ additionally requires the admin role. Adding a route cannot
//  accidentally leave it unauthenticated.
// ============================================================================
#ifndef DAEDALUS_NET_API_HPP
#define DAEDALUS_NET_API_HPP

#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "daedalus/algorithms/Sorting.hpp"
#include "daedalus/algorithms/Strings.hpp"
#include "daedalus/auth/Auth.hpp"
#include "daedalus/core/Version.hpp"
#include "daedalus/graph/Connectivity.hpp"
#include "daedalus/graph/Graph.hpp"
#include "daedalus/graph/MinimumSpanningTree.hpp"
#include "daedalus/graph/ShortestPath.hpp"
#include "daedalus/graph/Traversal.hpp"
#include "daedalus/hashing/Cache.hpp"
#include "daedalus/net/Http.hpp"
#include "daedalus/net/Json.hpp"
#include "daedalus/net/Router.hpp"
#include "daedalus/trees/AVLTree.hpp"
#include "daedalus/trees/BinarySearchTree.hpp"
#include "daedalus/trees/RedBlackTree.hpp"
#include "daedalus/trees/SplayTree.hpp"
#include "daedalus/trees/Treap.hpp"

namespace daedalus::net {

/// Serves files from a document root, with the decoded path already checked by
/// normalisePath(). Bodies are held in an LRU cache so a busy page does not hit
/// the disk on every request -- the library's own cache, doing a real job.
class StaticFiles {
public:
    explicit StaticFiles(std::string documentRoot, std::size_t cacheEntries = 64)
        : root_(std::move(documentRoot)), cache_(cacheEntries) {}

    /// Reads `relativePath` under the root. Returns nullopt when it is missing.
    /// The path must already have been normalised; a second explicit check for
    /// ".." is kept anyway, because defence in depth is cheap here.
    [[nodiscard]] std::optional<std::string> read(const std::string& relativePath) {
        if (relativePath.find("..") != std::string::npos) return std::nullopt;

        if (const auto cached = cache_.get(relativePath)) return cached;

        const std::string full = root_ + "/" + relativePath;
        std::ifstream file(full, std::ios::binary);
        if (!file) return std::nullopt;

        std::ostringstream contents;
        contents << file.rdbuf();
        const std::string body = contents.str();
        cache_.put(relativePath, body);
        return body;
    }

    [[nodiscard]] const std::string& root() const noexcept { return root_; }
    [[nodiscard]] const CacheStatistics& statistics() const noexcept {
        return cache_.statistics();
    }

private:
    std::string root_;
    LRUCache<std::string, std::string> cache_;
};

// ---------------------------------------------------------------------------

struct ApiOptions {
    std::string documentRoot{"web"};
    std::string sessionCookieName{"daedalus_session"};
    bool secureCookies{false};      ///< set when served over HTTPS
    std::size_t maximumValues{2000};   ///< cap on array sizes accepted from clients
};

namespace detail {

[[nodiscard]] inline Json toJsonArray(const std::vector<int>& values) {
    Json array = Json(JsonArray{});
    for (int value : values) array.push(value);
    return array;
}

[[nodiscard]] inline Json toJsonArray(const std::vector<std::string>& values) {
    Json array = Json(JsonArray{});
    for (const std::string& value : values) array.push(value);
    return array;
}

/// Reads an integer array from a request body, refusing anything oversized.
[[nodiscard]] inline std::vector<int> readIntegerArray(const Json& node, std::size_t maximum) {
    if (!node.isArray()) throw InvalidArgument("expected an array of numbers");
    if (node.size() > maximum) {
        throw InvalidArgument("at most " + std::to_string(maximum) + " values are accepted");
    }
    std::vector<int> values;
    values.reserve(node.size());
    for (const Json& element : node.asArray()) {
        if (!element.isNumber()) throw InvalidArgument("array elements must be numbers");
        values.push_back(static_cast<int>(element.asInteger()));
    }
    return values;
}

/// Builds the requested search tree and reports what makes it interesting.
[[nodiscard]] inline Json describeTree(const std::string& kind, const std::vector<int>& values) {
    std::unique_ptr<SortedSet<int>> tree;
    if (kind == "bst") {
        tree = std::make_unique<BinarySearchTree<int>>();
    } else if (kind == "avl") {
        tree = std::make_unique<AVLTree<int>>();
    } else if (kind == "redblack") {
        tree = std::make_unique<RedBlackTree<int>>();
    } else if (kind == "splay") {
        tree = std::make_unique<SplayTree<int>>();
    } else if (kind == "treap") {
        tree = std::make_unique<Treap<int>>();
    } else {
        throw InvalidArgument("unknown tree kind: " + kind);
    }

    for (int value : values) tree->insert(value);

    Json result;
    result.set("kind", kind);
    result.set("size", tree->size());
    result.set("height", tree->height());
    result.set("inOrder", toJsonArray(tree->toVector()));
    result.set("minimum", tree->minimum().has_value() ? Json(*tree->minimum()) : Json());
    result.set("maximum", tree->maximum().has_value() ? Json(*tree->maximum()) : Json());

    // The traversals and the ASCII rendering live on the concrete type, so the
    // polymorphic handle is narrowed here rather than being widened in the base.
    if (const auto* base = dynamic_cast<const BinaryTreeBase<int, BSTNode<int>>*>(tree.get())) {
        result.set("preOrder", toJsonArray(base->preOrder()));
        result.set("postOrder", toJsonArray(base->postOrder()));
        result.set("levelOrder", toJsonArray(base->levelOrder()));
        result.set("diagram", base->prettyPrint());
        result.set("balanced", base->isBalanced());
    } else if (const auto* avl =
                   dynamic_cast<const BinaryTreeBase<int, AVLNode<int>>*>(tree.get())) {
        result.set("preOrder", toJsonArray(avl->preOrder()));
        result.set("postOrder", toJsonArray(avl->postOrder()));
        result.set("levelOrder", toJsonArray(avl->levelOrder()));
        result.set("diagram", avl->prettyPrint());
        result.set("balanced", avl->isBalanced());
    } else if (const auto* redBlack =
                   dynamic_cast<const BinaryTreeBase<int, RBNode<int>>*>(tree.get())) {
        result.set("preOrder", toJsonArray(redBlack->preOrder()));
        result.set("postOrder", toJsonArray(redBlack->postOrder()));
        result.set("levelOrder", toJsonArray(redBlack->levelOrder()));
        result.set("diagram", redBlack->prettyPrint());
        result.set("balanced", redBlack->isBalanced());
    } else if (const auto* splay =
                   dynamic_cast<const BinaryTreeBase<int, SplayNode<int>>*>(tree.get())) {
        result.set("preOrder", toJsonArray(splay->preOrder()));
        result.set("postOrder", toJsonArray(splay->postOrder()));
        result.set("levelOrder", toJsonArray(splay->levelOrder()));
        result.set("diagram", splay->prettyPrint());
        result.set("balanced", splay->isBalanced());
    } else if (const auto* treap =
                   dynamic_cast<const BinaryTreeBase<int, TreapNode<int>>*>(tree.get())) {
        result.set("preOrder", toJsonArray(treap->preOrder()));
        result.set("postOrder", toJsonArray(treap->postOrder()));
        result.set("levelOrder", toJsonArray(treap->levelOrder()));
        result.set("diagram", treap->prettyPrint());
        result.set("balanced", treap->isBalanced());
    }
    return result;
}

/// Builds a graph from {"edges":[{"from":..,"to":..,"weight":..}], "directed":bool}.
[[nodiscard]] inline Graph<std::string, double> readGraph(const Json& body) {
    Graph<std::string, double> graph(body["directed"].asBoolean(false));
    if (!body["edges"].isArray()) throw InvalidArgument("expected an edges array");
    if (body["edges"].size() > 5000) throw InvalidArgument("at most 5000 edges are accepted");

    for (const Json& edge : body["edges"].asArray()) {
        const std::string from = edge["from"].asString();
        const std::string to = edge["to"].asString();
        if (from.empty() || to.empty()) throw InvalidArgument("every edge needs from and to");
        graph.addEdge(from, to, edge["weight"].asNumber(1.0));
    }
    for (const Json& vertex : body["vertices"].isArray() ? body["vertices"].asArray()
                                                         : JsonArray{}) {
        if (vertex.isString()) (void)graph.addVertex(vertex.asString());
    }
    return graph;
}

}  // namespace detail

// ---------------------------------------------------------------------------

/// Registers every route on `router`. The AuthService and StaticFiles are
/// captured by reference and must outlive the router.
inline void buildApi(Router& router, auth::AuthService& service, StaticFiles& files,
                     const ApiOptions& options = ApiOptions{}) {
    using auth::Role;

    // --- authorisation middleware -------------------------------------------
    //
    // One place decides what is public. A new /api/ route is protected by
    // default, which is the only safe direction for that mistake to fail in.
    router.use([&service, &options](const HttpRequest& request, const Next& next) {
        static const std::vector<std::string> publicPaths{
            "/api/health", "/api/routes", "/api/auth/login", "/api/auth/register"};

        const bool isApi = request.path.rfind("/api/", 0) == 0;
        if (!isApi) return next(request);

        const bool isPublic = std::find(publicPaths.begin(), publicPaths.end(), request.path) !=
                              publicPaths.end();
        if (isPublic) return next(request);

        const std::string token = request.cookie(options.sessionCookieName);
        const auto session = service.validate(token);
        if (!session.has_value()) {
            return HttpResponse::error(401, "sign in to use this endpoint");
        }
        if (request.path.rfind("/api/admin/", 0) == 0 && session->role != Role::Admin) {
            return HttpResponse::error(403, "this endpoint requires the admin role");
        }
        return next(request);
    });

    // --- static and health --------------------------------------------------

    router.get("/", [&files](const HttpRequest&) {
        const auto body = files.read("index.html");
        if (!body.has_value()) {
            return HttpResponse::html(
                "<h1>Daedalus</h1><p>The web playground was not found. Start the server from "
                "the repository root, or pass --web-root.</p>",
                200);
        }
        return HttpResponse::html(*body);
    });

    router.get("/static/*path", [&files](const HttpRequest& request) {
        const std::string relative = request.pathParameter("path");
        const auto body = files.read(relative);
        if (!body.has_value()) return HttpResponse::error(404, "no such file");
        return HttpResponse::file(*body, relative);
    });

    router.get("/api/health", [&service](const HttpRequest&) {
        Json payload;
        payload.set("status", "ok")
            .set("library", "daedalus")
            .set("version", version())
            .set("users", service.userCount())
            .set("sessions", service.activeSessionCount());
        return HttpResponse::json(payload);
    });

    router.get("/api/routes", [&router](const HttpRequest&) {
        Json payload;
        payload.set("routes", detail::toJsonArray(router.routes()));
        return HttpResponse::json(payload);
    });

    // --- authentication ------------------------------------------------------

    router.post("/api/auth/register", [&service](const HttpRequest& request) {
        const Json body = request.json();
        const auto outcome = service.registerUser(
            body["username"].asString(), body["email"].asString(), body["password"].asString(),
            auth::roleFromString(body["role"].asString("viewer")).value_or(Role::Viewer),
            request.clientAddress);

        Json payload;
        payload.set("ok", outcome.success).set("message", outcome.message);
        return HttpResponse::json(payload, outcome.success ? 201 : 400);
    });

    router.post("/api/auth/login", [&service, &options](const HttpRequest& request) {
        const Json body = request.json();
        const auto outcome = service.login(body["username"].asString(),
                                           body["password"].asString(), request.clientAddress);
        if (!outcome.success) {
            Json payload;
            payload.set("ok", false).set("message", outcome.message);
            // 429 when throttled, 401 otherwise -- the client shows a different
            // message for "slow down" than for "wrong password".
            const int status =
                outcome.message.find("too many") != std::string::npos ? 429 : 401;
            return HttpResponse::json(payload, status);
        }

        Json payload;
        payload.set("ok", true)
            .set("username", body["username"].asString())
            .set("role", auth::toString(outcome.role));
        HttpResponse response = HttpResponse::json(payload);
        response.setCookie(options.sessionCookieName, outcome.token,
                           static_cast<long long>(service.config().sessionLifetime.count()),
                           true, options.secureCookies);
        return response;
    });

    router.post("/api/auth/logout", [&service, &options](const HttpRequest& request) {
        (void)service.logout(request.cookie(options.sessionCookieName));
        Json payload;
        payload.set("ok", true);
        HttpResponse response = HttpResponse::json(payload);
        response.clearCookie(options.sessionCookieName);
        return response;
    });

    router.get("/api/auth/me", [&service, &options](const HttpRequest& request) {
        const auto session = service.validate(request.cookie(options.sessionCookieName));
        if (!session.has_value()) return HttpResponse::error(401, "not signed in");

        Json payload;
        payload.set("username", session->username).set("role", auth::toString(session->role));
        return HttpResponse::json(payload);
    });

    // --- the playground ------------------------------------------------------

    router.get("/api/algorithms", [](const HttpRequest&) {
        Json payload;
        payload.set("sorting", detail::toJsonArray(availableSortStrategies<int>()));
        payload.set("trees", detail::toJsonArray(std::vector<std::string>{
                                 "bst", "avl", "redblack", "splay", "treap"}));
        payload.set("graph", detail::toJsonArray(std::vector<std::string>{
                                 "bfs", "dfs", "dijkstra", "bellman-ford", "mst-kruskal",
                                 "mst-prim", "topological", "scc", "components", "bridges",
                                 "articulation"}));
        payload.set("strings", detail::toJsonArray(std::vector<std::string>{
                                   "kmp", "z", "rabin-karp", "boyer-moore", "palindrome",
                                   "edit-distance", "lcs", "suffix-array"}));
        return HttpResponse::json(payload);
    });

    router.post("/api/sort", [&options](const HttpRequest& request) {
        const Json body = request.json();
        std::vector<int> values = detail::readIntegerArray(body["values"], options.maximumValues);
        const std::string algorithm = body["algorithm"].asString("intro");

        auto strategy = makeSortStrategy<int>(algorithm);
        auto metrics = std::make_shared<MetricsObserver>();
        auto trace = std::make_shared<TraceObserver>(60);
        strategy->attach(metrics);
        strategy->attach(trace);

        const auto started = std::chrono::steady_clock::now();
        strategy->sort(values);
        const double elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();

        Json payload;
        payload.set("algorithm", strategy->name())
            .set("stable", strategy->stable())
            .set("inPlace", strategy->inPlace())
            .set("averageComplexity", strategy->averageComplexity())
            .set("worstComplexity", strategy->worstComplexity())
            .set("comparisons", metrics->comparisons())
            .set("swaps", metrics->swaps())
            .set("milliseconds", elapsed)
            .set("sorted", detail::toJsonArray(values))
            .set("trace", detail::toJsonArray(trace->lines()));
        return HttpResponse::json(payload);
    });

    router.post("/api/tree", [&options](const HttpRequest& request) {
        const Json body = request.json();
        const std::vector<int> values =
            detail::readIntegerArray(body["values"], options.maximumValues);
        return HttpResponse::json(
            detail::describeTree(body["kind"].asString("avl"), values));
    });

    router.post("/api/strings", [](const HttpRequest& request) {
        const Json body = request.json();
        const std::string algorithm = body["algorithm"].asString("kmp");
        const std::string text = body["text"].asString();
        const std::string pattern = body["pattern"].asString();
        if (text.size() > 100000) throw InvalidArgument("text is too long");

        Json payload;
        payload.set("algorithm", algorithm);

        if (algorithm == "kmp" || algorithm == "z" || algorithm == "rabin-karp" ||
            algorithm == "boyer-moore") {
            std::vector<std::size_t> matches;
            if (algorithm == "kmp") matches = knuthMorrisPratt(text, pattern);
            if (algorithm == "z") matches = zSearch(text, pattern);
            if (algorithm == "rabin-karp") matches = rabinKarp(text, pattern);
            if (algorithm == "boyer-moore") matches = boyerMooreHorspool(text, pattern);

            Json positions = Json(JsonArray{});
            for (std::size_t position : matches) positions.push(position);
            payload.set("matches", positions).set("count", matches.size());
        } else if (algorithm == "palindrome") {
            payload.set("longestPalindrome", longestPalindrome(text));
        } else if (algorithm == "edit-distance") {
            payload.set("distance", editDistance(text, pattern));
        } else if (algorithm == "lcs") {
            payload.set("subsequence", longestCommonSubsequence(text, pattern));
            payload.set("substring", longestCommonSubstring(text, pattern));
        } else if (algorithm == "suffix-array") {
            Json order = Json(JsonArray{});
            for (std::size_t suffix : suffixArray(text)) order.push(suffix);
            payload.set("suffixArray", order);
            payload.set("longestRepeated", longestRepeatedSubstring(text));
        } else {
            throw InvalidArgument("unknown string algorithm: " + algorithm);
        }
        return HttpResponse::json(payload);
    });

    router.post("/api/graph", [](const HttpRequest& request) {
        const Json body = request.json();
        const Graph<std::string, double> graph = detail::readGraph(body);
        const std::string algorithm = body["algorithm"].asString("bfs");
        const std::string source = body["source"].asString();

        Json payload;
        payload.set("algorithm", algorithm)
            .set("vertexCount", graph.vertexCount())
            .set("edgeCount", graph.edgeCount())
            .set("directed", graph.directed());

        const auto requireSource = [&graph, &source]() {
            if (source.empty() || !graph.hasVertex(source)) {
                throw InvalidArgument("this algorithm needs a source vertex present in the graph");
            }
        };

        if (algorithm == "bfs" || algorithm == "dfs") {
            requireSource();
            const auto walk = algorithm == "bfs" ? breadthFirstSearch(graph, source)
                                                 : depthFirstSearch(graph, source);
            payload.set("order", detail::toJsonArray(walk.order));
        } else if (algorithm == "dijkstra" || algorithm == "bellman-ford") {
            requireSource();
            const auto result = algorithm == "dijkstra" ? dijkstra(graph, source)
                                                        : bellmanFord(graph, source);
            Json distances;
            for (std::size_t v = 0; v < graph.vertexCount(); ++v) {
                distances.set(graph.label(v), result.reachable(v) ? Json(result.distance[v])
                                                                  : Json());
            }
            payload.set("distances", distances);
            payload.set("negativeCycle", result.negativeCycle);
            payload.set("settled", result.settledCount);
        } else if (algorithm == "mst-kruskal" || algorithm == "mst-prim") {
            const auto tree = algorithm == "mst-kruskal" ? kruskal(graph) : prim(graph);
            Json edges = Json(JsonArray{});
            for (const auto& edge : tree.edges) {
                Json item;
                item.set("from", edge.from).set("to", edge.to).set("weight", edge.weight);
                edges.push(item);
            }
            payload.set("edges", edges)
                .set("totalWeight", tree.totalWeight)
                .set("spansEveryVertex", tree.spansEveryVertex);
        } else if (algorithm == "topological") {
            const auto order = topologicalSort(graph);
            payload.set("acyclic", order.has_value());
            if (order.has_value()) payload.set("order", detail::toJsonArray(*order));
        } else if (algorithm == "scc") {
            Json groups = Json(JsonArray{});
            for (const auto& component : tarjanStronglyConnectedComponents(graph)) {
                groups.push(detail::toJsonArray(component));
            }
            payload.set("components", groups);
        } else if (algorithm == "components") {
            Json groups = Json(JsonArray{});
            for (const auto& component : connectedComponents(graph)) {
                groups.push(detail::toJsonArray(component));
            }
            payload.set("components", groups).set("connected", isConnected(graph));
        } else if (algorithm == "bridges") {
            Json edges = Json(JsonArray{});
            for (const auto& edge : findBridges(graph)) {
                Json item;
                item.set("from", edge.from).set("to", edge.to);
                edges.push(item);
            }
            payload.set("bridges", edges);
        } else if (algorithm == "articulation") {
            payload.set("articulationPoints",
                        detail::toJsonArray(findArticulationPoints(graph)));
        } else {
            throw InvalidArgument("unknown graph algorithm: " + algorithm);
        }
        return HttpResponse::json(payload);
    });

    // --- admin ---------------------------------------------------------------

    router.get("/api/admin/users", [&service](const HttpRequest&) {
        Json users = Json(JsonArray{});
        for (const std::string& username : service.usernames()) {
            const auto user = service.findUser(username);
            if (!user.has_value()) continue;
            Json item;
            item.set("username", user->username)
                .set("email", user->email)
                .set("role", auth::toString(user->role))
                .set("active", user->active)
                .set("failedAttempts", user->failedAttempts);
            users.push(item);
        }
        Json payload;
        payload.set("users", users).set("count", service.userCount());
        return HttpResponse::json(payload);
    });

    router.get("/api/admin/audit", [&service](const HttpRequest&) {
        Json entries = Json(JsonArray{});
        for (const auto& entry : service.auditTrail()) {
            Json item;
            item.set("action", entry.action)
                .set("username", entry.username)
                .set("client", entry.clientAddress)
                .set("succeeded", entry.succeeded)
                .set("detail", entry.detail);
            entries.push(item);
        }
        Json payload;
        payload.set("entries", entries);
        return HttpResponse::json(payload);
    });
}

}  // namespace daedalus::net

#endif  // DAEDALUS_NET_API_HPP
