// ============================================================================
//  Unit tests for JSON, HTTP parsing, routing, the thread pool, and an
//  end-to-end run of the real socket server.
// ============================================================================
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "daedalus/concurrent/ThreadPool.hpp"
#include "daedalus/net/Http.hpp"
#include "daedalus/net/Json.hpp"
#include "daedalus/net/Router.hpp"
#include "daedalus/net/Server.hpp"
#include "framework/TestFramework.hpp"

using namespace daedalus;
using namespace daedalus::net;

namespace {

/// Winsock's send/recv take an int length where POSIX takes size_t. These two
/// helpers keep the platform ifdef in one place instead of at every call site.
int sendBytes(platform::SocketHandle socketHandle, const char* data, std::size_t length) {
#if defined(_WIN32)
    return ::send(socketHandle, data, static_cast<int>(length), 0);
#else
    return static_cast<int>(::send(socketHandle, data, length, 0));
#endif
}

int receiveBytes(platform::SocketHandle socketHandle, char* buffer, std::size_t capacity) {
#if defined(_WIN32)
    return ::recv(socketHandle, buffer, static_cast<int>(capacity), 0);
#else
    return static_cast<int>(::recv(socketHandle, buffer, capacity, 0));
#endif
}

/// Minimal blocking HTTP client, used only by the end-to-end tests. Sends one
/// request and reads until the connection closes or the body is complete.
std::string sendRawRequest(std::uint16_t port, const std::string& raw) {
    platform::ensureInitialised();
    const platform::SocketHandle client = ::socket(AF_INET, SOCK_STREAM, 0);
    if (client == platform::kInvalidSocket) return "";

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    if (::connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        platform::closeSocket(client);
        return "";
    }

    std::size_t sent = 0;
    while (sent < raw.size()) {
        const int written = sendBytes(client, raw.data() + sent, raw.size() - sent);
        if (written <= 0) break;
        sent += static_cast<std::size_t>(written);
    }

    std::string response;
    char buffer[4096];
    for (;;) {
        const int received = receiveBytes(client, buffer, sizeof(buffer));
        if (received <= 0) break;
        response.append(buffer, static_cast<std::size_t>(received));

        // Stop once the declared body has arrived, so keep-alive does not stall.
        const std::size_t headerEnd = response.find("\r\n\r\n");
        if (headerEnd == std::string::npos) continue;
        const std::size_t lengthAt = response.find("Content-Length: ");
        if (lengthAt == std::string::npos || lengthAt > headerEnd) break;
        const std::size_t declared =
            static_cast<std::size_t>(std::stoul(response.substr(lengthAt + 16)));
        if (response.size() - headerEnd - 4 >= declared) break;
    }
    platform::closeSocket(client);
    return response;
}

std::string get(std::uint16_t port, const std::string& path) {
    return sendRawRequest(
        port, "GET " + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
}

int statusOf(const std::string& response) {
    if (response.size() < 12) return 0;
    return std::stoi(response.substr(9, 3));
}

std::string bodyOf(const std::string& response) {
    const std::size_t headerEnd = response.find("\r\n\r\n");
    return headerEnd == std::string::npos ? "" : response.substr(headerEnd + 4);
}

}   // namespace

// ============================================================================
//  JSON
// ============================================================================

DAEDALUS_TEST(Json, serialises_every_kind) {
    CHECK_EQ(Json().dump(), std::string("null"));
    CHECK_EQ(Json(true).dump(), std::string("true"));
    CHECK_EQ(Json(42).dump(), std::string("42"));
    CHECK_EQ(Json(-7).dump(), std::string("-7"));
    CHECK_EQ(Json(2.5).dump(), std::string("2.5"));
    CHECK_EQ(Json("hi").dump(), std::string("\"hi\""));
    CHECK_EQ(Json::array({1, 2, 3}).dump(), std::string("[1,2,3]"));

    Json object;
    object.set("name", "daedalus").set("version", 1);
    CHECK_EQ(object.dump(), std::string("{\"name\":\"daedalus\",\"version\":1}"));
}

DAEDALUS_TEST(Json, whole_numbers_do_not_gain_a_decimal_point) {
    CHECK_EQ(Json(3.0).dump(), std::string("3"));
    CHECK_EQ(Json(1000000).dump(), std::string("1000000"));
    CHECK_EQ(Json(0.5).dump(), std::string("0.5"));
}

DAEDALUS_TEST(Json, escaping_is_xss_safe) {
    // A naive serialiser lets this close the surrounding <script> tag.
    const Json payload("</script><script>alert(1)</script>");
    const std::string dumped = payload.dump();
    CHECK_TRUE(dumped.find("</script>") == std::string::npos);
    CHECK_TRUE(dumped.find("\\u003c") != std::string::npos);

    CHECK_EQ(Json("a\"b").dump(), std::string("\"a\\\"b\""));
    CHECK_EQ(Json("line\nbreak").dump(), std::string("\"line\\nbreak\""));
    CHECK_EQ(Json(std::string("bell\x07")).dump(), std::string("\"bell\\u0007\""));
}

DAEDALUS_TEST(Json, parses_and_round_trips) {
    const std::string text =
        "{\"name\":\"daedalus\",\"tags\":[\"cpp\",\"dsa\"],\"count\":3,\"ok\":true,"
        "\"missing\":null,\"nested\":{\"depth\":2}}";
    const Json parsed = Json::parse(text);

    CHECK_TRUE(parsed.isObject());
    CHECK_EQ(parsed["name"].asString(), std::string("daedalus"));
    CHECK_EQ(parsed["count"].asInteger(), 3);
    CHECK_TRUE(parsed["ok"].asBoolean());
    CHECK_TRUE(parsed["missing"].isNull());
    CHECK_EQ(parsed["tags"].size(), 2u);
    CHECK_EQ(parsed["tags"].asArray()[1].asString(), std::string("dsa"));
    CHECK_EQ(parsed["nested"]["depth"].asInteger(), 2);
    CHECK_EQ(Json::parse(parsed.dump()).dump(), parsed.dump());
}

DAEDALUS_TEST(Json, parses_escapes_including_unicode) {
    const Json parsed = Json::parse("{\"text\":\"tab\\there \\u00e9 \\u0041\"}");
    CHECK_EQ(parsed["text"].asString(), std::string("tab\there \xc3\xa9 A"));
    CHECK_EQ(Json::parse("\"a\\/b\"").asString(), std::string("a/b"));
}

DAEDALUS_TEST(Json, rejects_malformed_input) {
    CHECK_THROWS_AS(Json::parse("{"), InvalidArgument);
    CHECK_THROWS_AS(Json::parse("{\"a\":}"), InvalidArgument);
    CHECK_THROWS_AS(Json::parse("[1,2"), InvalidArgument);
    CHECK_THROWS_AS(Json::parse("\"unterminated"), InvalidArgument);
    CHECK_THROWS_AS(Json::parse("tru"), InvalidArgument);
    CHECK_THROWS_AS(Json::parse("{} trailing"), InvalidArgument);

    // tryParse never throws: a hostile request body must not crash a handler.
    CHECK_TRUE(Json::tryParse("{").isNull());
    CHECK_TRUE(Json::tryParse("").isNull());
}

DAEDALUS_TEST(Json, missing_keys_read_as_null_rather_than_throwing) {
    const Json parsed = Json::parse("{\"a\":1}");
    CHECK_TRUE(parsed["nope"].isNull());
    CHECK_EQ(parsed["nope"].asInteger(-1), -1);
    CHECK_EQ(parsed["nope"]["deeper"].asString("fallback"), std::string("fallback"));
    CHECK_FALSE(parsed.contains("nope"));
    CHECK_TRUE(parsed.contains("a"));
}

// ============================================================================
//  URL and path handling
// ============================================================================

DAEDALUS_TEST(Http, percent_coding) {
    CHECK_EQ(percentDecode("hello%20world"), std::string("hello world"));
    CHECK_EQ(percentDecode("a+b"), std::string("a+b"));
    CHECK_EQ(percentDecode("a+b", true), std::string("a b"));
    CHECK_EQ(percentDecode("%2F%2f"), std::string("//"));
    CHECK_EQ(percentEncode("a b/c"), std::string("a%20b%2Fc"));
    CHECK_EQ(percentEncode("safe-_.~"), std::string("safe-_.~"));
    CHECK_THROWS_AS(percentDecode("%zz"), InvalidArgument);
    CHECK_THROWS_AS(percentDecode("%4"), InvalidArgument);
}

DAEDALUS_TEST(Http, query_string_parsing) {
    const auto parameters = parseQueryString("a=1&b=two%20words&flag&c=");
    CHECK_EQ(parameters.at("a"), std::string("1"));
    CHECK_EQ(parameters.at("b"), std::string("two words"));
    CHECK_EQ(parameters.at("flag"), std::string(""));
    CHECK_EQ(parameters.at("c"), std::string(""));
    CHECK_EQ(parseQueryString("").size(), 0u);
}

DAEDALUS_TEST(Http, path_normalisation_blocks_traversal) {
    CHECK_EQ(normalisePath("/a//b/./c"), std::string("/a/b/c"));
    CHECK_EQ(normalisePath("/"), std::string("/"));
    CHECK_EQ(normalisePath(""), std::string("/"));
    // The directory traversal defence: refuse, do not sanitise.
    CHECK_THROWS_AS(normalisePath("/../etc/passwd"), InvalidArgument);
    CHECK_THROWS_AS(normalisePath("/static/../../secret"), InvalidArgument);
    CHECK_THROWS_AS(normalisePath("/a\\b"), InvalidArgument);
    CHECK_THROWS_AS(normalisePath(std::string("/a\0b", 4)), InvalidArgument);
}

DAEDALUS_TEST(Http, content_types) {
    CHECK_EQ(contentTypeFor("index.html"), std::string("text/html; charset=utf-8"));
    CHECK_EQ(contentTypeFor("app.JS"), std::string("application/javascript; charset=utf-8"));
    CHECK_EQ(contentTypeFor("style.css"), std::string("text/css; charset=utf-8"));
    // Unknown extensions must not be guessed into something executable.
    CHECK_EQ(contentTypeFor("mystery.xyz"), std::string("application/octet-stream"));
    CHECK_EQ(contentTypeFor("noextension"), std::string("application/octet-stream"));
}

// ============================================================================
//  Request parsing
// ============================================================================

DAEDALUS_TEST(Http, parses_a_complete_request) {
    const std::string raw =
        "POST /api/items?page=2 HTTP/1.1\r\n"
        "Host: localhost:8080\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 13\r\n"
        "Cookie: session=abc123; theme=dark\r\n"
        "\r\n"
        "{\"name\":\"x\"}\n";

    const HttpRequest request = parseRequest(raw);
    CHECK_EQ(request.method, std::string("POST"));
    CHECK_EQ(request.path, std::string("/api/items"));
    CHECK_EQ(request.version, std::string("HTTP/1.1"));
    CHECK_EQ(request.queryParameter("page"), std::string("2"));
    CHECK_EQ(request.body.size(), 13u);
    CHECK_EQ(request.json()["name"].asString(), std::string("x"));
    CHECK_EQ(request.cookie("session"), std::string("abc123"));
    CHECK_EQ(request.cookie("theme"), std::string("dark"));
    CHECK_EQ(request.cookie("absent"), std::string(""));
}

DAEDALUS_TEST(Http, header_lookup_is_case_insensitive) {
    const std::string raw = "GET / HTTP/1.1\r\nHost: x\r\nX-Custom-Header: value\r\n\r\n";
    const HttpRequest request = parseRequest(raw);
    CHECK_EQ(request.header("x-custom-header"), std::string("value"));
    CHECK_EQ(request.header("X-CUSTOM-HEADER"), std::string("value"));
    CHECK_EQ(request.header("missing", "fallback"), std::string("fallback"));
}

DAEDALUS_TEST(Http, rejects_malformed_and_oversized_requests) {
    CHECK_THROWS_AS(parseRequest("GET / HTTP/1.1\r\n"), InvalidArgument);       // no blank line
    CHECK_THROWS_AS(parseRequest("GARBAGE\r\n\r\n"), InvalidArgument);          // no target
    CHECK_THROWS_AS(parseRequest("GET / HTTP/9.9\r\n\r\n"), InvalidArgument);   // bad version
    CHECK_THROWS_AS(parseRequest("GET / HTTP/1.1\r\nbadheader\r\n\r\n"), InvalidArgument);
    CHECK_THROWS_AS(parseRequest("GET / HTTP/1.1\r\nContent-Length: abc\r\n\r\n"), InvalidArgument);

    HttpLimits tight;
    tight.maximumBody = 4;
    CHECK_THROWS_AS(parseRequest("POST / HTTP/1.1\r\nContent-Length: 100\r\n\r\n", tight),
                    InvalidArgument);

    // Content-Length larger than the bytes actually present.
    CHECK_THROWS_AS(parseRequest("POST / HTTP/1.1\r\nContent-Length: 50\r\n\r\nshort"),
                    InvalidArgument);
}

DAEDALUS_TEST(Http, keep_alive_defaults_follow_the_protocol_version) {
    const auto oneOne = parseRequest("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    CHECK_TRUE(oneOne.wantsKeepAlive());

    const auto closed = parseRequest("GET / HTTP/1.1\r\nConnection: close\r\n\r\n");
    CHECK_FALSE(closed.wantsKeepAlive());

    const auto oneZero = parseRequest("GET / HTTP/1.0\r\nHost: x\r\n\r\n");
    CHECK_FALSE(oneZero.wantsKeepAlive());   // 1.0 closes unless asked otherwise

    const auto oneZeroKeep = parseRequest("GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n");
    CHECK_TRUE(oneZeroKeep.wantsKeepAlive());
}

DAEDALUS_TEST(Http, pending_body_bytes_drives_the_read_loop) {
    CHECK_FALSE(pendingBodyBytes("GET / HTTP/1.1\r\nHost").has_value());
    CHECK_EQ(pendingBodyBytes("GET / HTTP/1.1\r\n\r\n").value(), 0u);
    CHECK_EQ(pendingBodyBytes("POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\n").value(), 10u);
    CHECK_EQ(pendingBodyBytes("POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\n12345").value(), 5u);
    CHECK_EQ(pendingBodyBytes("POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\n1234").value(), 0u);
}

// ============================================================================
//  Responses
// ============================================================================

DAEDALUS_TEST(Http, response_serialisation) {
    const std::string wire = HttpResponse::text("hello").serialise(true);
    CHECK_TRUE(wire.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
    CHECK_TRUE(wire.find("Content-Length: 5\r\n") != std::string::npos);
    CHECK_TRUE(wire.find("Connection: keep-alive\r\n") != std::string::npos);
    CHECK_TRUE(wire.find("\r\n\r\nhello") != std::string::npos);

    CHECK_TRUE(HttpResponse::text("x").serialise(false).find("Connection: close") !=
               std::string::npos);
    CHECK_EQ(HttpResponse::reasonPhrase(404), std::string("Not Found"));
    CHECK_EQ(HttpResponse::reasonPhrase(999), std::string("Unknown"));
}

DAEDALUS_TEST(Http, cookies_are_hardened_by_default) {
    HttpResponse response = HttpResponse::text("ok");
    response.setCookie("session", "token123", 3600);
    const std::string wire = response.serialise();

    CHECK_TRUE(wire.find("Set-Cookie: session=token123") != std::string::npos);
    CHECK_TRUE(wire.find("HttpOnly") != std::string::npos);
    CHECK_TRUE(wire.find("SameSite=Strict") != std::string::npos);
    CHECK_TRUE(wire.find("Max-Age=3600") != std::string::npos);

    HttpResponse cleared = HttpResponse::text("bye");
    cleared.clearCookie("session");
    CHECK_TRUE(cleared.serialise().find("Max-Age=0") != std::string::npos);
}

DAEDALUS_TEST(Http, security_headers_are_present) {
    const std::string wire = HttpResponse::html("<p>hi</p>").withSecurityHeaders().serialise();
    CHECK_TRUE(wire.find("X-Content-Type-Options: nosniff") != std::string::npos);
    CHECK_TRUE(wire.find("X-Frame-Options: DENY") != std::string::npos);
    CHECK_TRUE(wire.find("Content-Security-Policy:") != std::string::npos);
    CHECK_TRUE(wire.find("frame-ancestors 'none'") != std::string::npos);
}

DAEDALUS_TEST(Http, json_and_error_responses) {
    Json payload;
    payload.set("ok", true);
    const HttpResponse response = HttpResponse::json(payload);
    CHECK_EQ(response.status(), 200);
    CHECK_EQ(response.body(), std::string("{\"ok\":true}"));

    const HttpResponse failure = HttpResponse::error(404, "not found");
    CHECK_EQ(failure.status(), 404);
    CHECK_TRUE(failure.body().find("not found") != std::string::npos);
}

// ============================================================================
//  Router
// ============================================================================

DAEDALUS_TEST(Router, static_parameter_and_wildcard_matching) {
    Router router;
    router.get("/api/users/me", [](const HttpRequest&) { return HttpResponse::text("me"); });
    router.get("/api/users/:id", [](const HttpRequest& request) {
        return HttpResponse::text("user:" + request.pathParameter("id"));
    });
    router.get("/static/*path", [](const HttpRequest& request) {
        return HttpResponse::text("file:" + request.pathParameter("path"));
    });
    router.get("/", [](const HttpRequest&) { return HttpResponse::text("root"); });

    const auto call = [&router](const std::string& path) {
        HttpRequest request;
        request.method = "GET";
        request.path = path;
        return router.dispatch(request);
    };

    CHECK_EQ(call("/").body(), std::string("root"));
    // A static segment must beat the parameter route registered after it.
    CHECK_EQ(call("/api/users/me").body(), std::string("me"));
    CHECK_EQ(call("/api/users/42").body(), std::string("user:42"));
    CHECK_EQ(call("/static/css/app.css").body(), std::string("file:css/app.css"));
    CHECK_EQ(call("/nope").status(), 404);
    CHECK_EQ(router.routeCount(), 4u);
}

DAEDALUS_TEST(Router, method_mismatch_is_405_with_an_allow_header) {
    Router router;
    router.get("/thing", [](const HttpRequest&) { return HttpResponse::text("get"); });
    router.post("/thing", [](const HttpRequest&) { return HttpResponse::text("post"); });

    HttpRequest request;
    request.method = "DELETE";
    request.path = "/thing";
    const HttpResponse response = router.dispatch(request);
    CHECK_EQ(response.status(), 405);
    CHECK_EQ(response.headers().at("Allow"), std::string("GET, POST"));
}

DAEDALUS_TEST(Router, middleware_runs_outermost_first_and_can_short_circuit) {
    Router router;
    std::vector<std::string> order;

    router.use([&order](const HttpRequest& request, const Next& next) {
        order.push_back("outer-before");
        HttpResponse response = next(request);
        order.push_back("outer-after");
        return response;
    });
    router.use([&order](const HttpRequest& request, const Next& next) {
        order.push_back("inner-before");
        HttpResponse response = next(request);
        order.push_back("inner-after");
        return response;
    });
    router.get("/x", [&order](const HttpRequest&) {
        order.push_back("handler");
        return HttpResponse::text("done");
    });

    HttpRequest request;
    request.method = "GET";
    request.path = "/x";
    CHECK_EQ(router.dispatch(request).body(), std::string("done"));
    CHECK_EQ(order, (std::vector<std::string>{"outer-before", "inner-before", "handler",
                                              "inner-after", "outer-after"}));

    // A middleware that does not call next() stops the chain.
    Router guarded;
    bool handlerRan = false;
    guarded.use(
        [](const HttpRequest&, const Next&) { return HttpResponse::error(401, "unauthorised"); });
    guarded.get("/x", [&handlerRan](const HttpRequest&) {
        handlerRan = true;
        return HttpResponse::text("secret");
    });
    CHECK_EQ(guarded.dispatch(request).status(), 401);
    CHECK_FALSE(handlerRan);
}

DAEDALUS_TEST(Router, lists_its_routes_and_validates_patterns) {
    Router router;
    router.get("/a", [](const HttpRequest&) { return HttpResponse::text(""); });
    router.post("/b/:id", [](const HttpRequest&) { return HttpResponse::text(""); });

    const auto routes = router.routes();
    CHECK_EQ(routes.size(), 2u);
    CHECK_EQ(routes[0], std::string("GET /a"));
    CHECK_EQ(routes[1], std::string("POST /b/:id"));

    CHECK_THROWS_AS(
        router.get("no-leading-slash", [](const HttpRequest&) { return HttpResponse::text(""); }),
        InvalidArgument);
}

// ============================================================================
//  Thread pool
// ============================================================================

DAEDALUS_TEST(ThreadPool, runs_every_submitted_task) {
    std::atomic<int> total{0};
    {
        concurrent::ThreadPool pool(4, 256);
        for (int i = 0; i < 500; ++i) {
            CHECK_TRUE(pool.submit([&total] { total.fetch_add(1); }));
        }
        pool.shutdown();
        CHECK_EQ(pool.completed(), 500u);
    }
    CHECK_EQ(total.load(), 500);
}

DAEDALUS_TEST(ThreadPool, a_throwing_task_does_not_kill_the_worker) {
    std::atomic<int> completed{0};
    concurrent::ThreadPool pool(2, 64);
    for (int i = 0; i < 20; ++i) {
        (void)pool.submit([&completed, i] {
            if (i % 2 == 0) throw std::runtime_error("boom");
            completed.fetch_add(1);
        });
    }
    pool.shutdown();
    CHECK_EQ(completed.load(), 10);
    CHECK_EQ(pool.completed(), 20u);   // every task was retired, thrown or not
}

DAEDALUS_TEST(ThreadPool, bounded_queue_refuses_when_full) {
    concurrent::BlockingQueue<int> queue(3);
    CHECK_TRUE(queue.tryPush(1));
    CHECK_TRUE(queue.tryPush(2));
    CHECK_TRUE(queue.tryPush(3));
    CHECK_FALSE(queue.tryPush(4));   // full: backpressure rather than growth
    CHECK_EQ(queue.size(), 3u);

    CHECK_EQ(queue.pop().value(), 1);
    CHECK_TRUE(queue.tryPush(4));
    queue.close();
    CHECK_FALSE(queue.tryPush(5));
    CHECK_TRUE(queue.closed());
}

DAEDALUS_TEST(ThreadPool, closing_a_queue_releases_blocked_consumers) {
    concurrent::BlockingQueue<int> queue(4);
    std::atomic<bool> finished{false};
    std::thread consumer([&queue, &finished] {
        (void)queue.pop();   // blocks: the queue is empty
        finished.store(true);
    });
    queue.close();
    consumer.join();
    CHECK_TRUE(finished.load());
}

DAEDALUS_TEST(ThreadPool, rejects_a_zero_sized_pool_or_queue) {
    CHECK_THROWS_AS(concurrent::ThreadPool(0, 16), InvalidArgument);
    CHECK_THROWS_AS(concurrent::BlockingQueue<int>(0), InvalidArgument);
}

// ============================================================================
//  End-to-end server
// ============================================================================

DAEDALUS_TEST(Server, serves_real_requests_over_a_socket) {
    Router router;
    router.get("/ping", [](const HttpRequest&) { return HttpResponse::text("pong"); });
    router.get("/echo/:value", [](const HttpRequest& request) {
        Json payload;
        payload.set("value", request.pathParameter("value"));
        return HttpResponse::json(payload);
    });
    router.post("/sum", [](const HttpRequest& request) {
        const Json body = request.json();
        Json payload;
        payload.set("sum", body["a"].asInteger() + body["b"].asInteger());
        return HttpResponse::json(payload);
    });

    ServerConfig config;
    config.port = 0;   // let the OS pick a free port
    config.workerThreads = 2;
    config.logRequests = false;

    HttpServer server(router, config);
    server.start();
    CHECK_TRUE(server.running());
    CHECK_LT(0, static_cast<int>(server.boundPort()));

    const std::uint16_t port = server.boundPort();

    const std::string pong = get(port, "/ping");
    CHECK_EQ(statusOf(pong), 200);
    CHECK_EQ(bodyOf(pong), std::string("pong"));

    const std::string echoed = get(port, "/echo/hello");
    CHECK_EQ(statusOf(echoed), 200);
    CHECK_EQ(bodyOf(echoed), std::string("{\"value\":\"hello\"}"));

    const std::string missing = get(port, "/nothing-here");
    CHECK_EQ(statusOf(missing), 404);

    const std::string body = "{\"a\":20,\"b\":22}";
    const std::string summed = sendRawRequest(
        port, "POST /sum HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: " +
                  std::to_string(body.size()) + "\r\n\r\n" + body);
    CHECK_EQ(statusOf(summed), 200);
    CHECK_EQ(bodyOf(summed), std::string("{\"sum\":42}"));

    // Security headers must be applied by the server, not just available.
    CHECK_TRUE(pong.find("X-Content-Type-Options: nosniff") != std::string::npos);

    server.stop();
    CHECK_FALSE(server.running());
    CHECK_LE(4u, server.handledRequests());
}

DAEDALUS_TEST(Server, handles_concurrent_clients) {
    Router router;
    router.get("/work", [](const HttpRequest&) { return HttpResponse::text("ok"); });

    ServerConfig config;
    config.port = 0;
    config.workerThreads = 4;
    config.logRequests = false;

    HttpServer server(router, config);
    server.start();
    const std::uint16_t port = server.boundPort();

    std::atomic<int> succeeded{0};
    std::vector<std::thread> clients;
    for (int i = 0; i < 16; ++i) {
        clients.emplace_back([port, &succeeded] {
            const std::string response = get(port, "/work");
            if (statusOf(response) == 200 && bodyOf(response) == "ok") succeeded.fetch_add(1);
        });
    }
    for (std::thread& client : clients) client.join();

    CHECK_EQ(succeeded.load(), 16);
    server.stop();
}

DAEDALUS_TEST(Server, a_malformed_request_gets_400_not_a_crash) {
    Router router;
    router.get("/ping", [](const HttpRequest&) { return HttpResponse::text("pong"); });

    ServerConfig config;
    config.port = 0;
    config.workerThreads = 2;
    config.logRequests = false;

    HttpServer server(router, config);
    server.start();
    const std::uint16_t port = server.boundPort();

    // Directory traversal is rejected by the path normaliser as a 400.
    const std::string traversal = sendRawRequest(
        port, "GET /../../etc/passwd HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    CHECK_EQ(statusOf(traversal), 400);

    const std::string garbage = sendRawRequest(port, "NOT-A-REQUEST\r\nHost: x\r\n\r\n");
    CHECK_EQ(statusOf(garbage), 400);

    // The server is still healthy afterwards.
    CHECK_EQ(statusOf(get(port, "/ping")), 200);
    server.stop();
}

DAEDALUS_TEST(Server, keep_alive_serves_several_requests_on_one_connection) {
    Router router;
    router.get("/n", [](const HttpRequest&) { return HttpResponse::text("x"); });

    ServerConfig config;
    config.port = 0;
    config.workerThreads = 2;
    config.logRequests = false;
    HttpServer server(router, config);
    server.start();
    const std::uint16_t port = server.boundPort();

    platform::ensureInitialised();
    const platform::SocketHandle client = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    CHECK_EQ(::connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);

    const std::string request = "GET /n HTTP/1.1\r\nHost: x\r\n\r\n";
    int answered = 0;
    for (int i = 0; i < 3; ++i) {
        (void)sendBytes(client, request.data(), request.size());
        char buffer[2048];
        const int received = receiveBytes(client, buffer, sizeof(buffer));
        if (received > 0) {
            const std::string response(buffer, static_cast<std::size_t>(received));
            if (response.find("HTTP/1.1 200") != std::string::npos) ++answered;
        }
    }
    platform::closeSocket(client);
    CHECK_EQ(answered, 3);   // one socket, three responses
    server.stop();
}

DAEDALUS_TEST(Server, rejects_an_invalid_bind_address) {
    Router router;
    ServerConfig config;
    config.host = "not-an-address";
    HttpServer server(router, config);
    CHECK_THROWS_AS(server.start(), InvalidArgument);
    CHECK_FALSE(server.running());
}
