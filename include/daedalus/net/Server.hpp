// ============================================================================
//  Daedalus :: net/Server.hpp
//
//  A small blocking HTTP/1.1 server: one accept loop, a fixed worker pool, one
//  connection per task, keep-alive supported.
//
//  It is not an event loop and does not pretend to be -- with a bounded pool
//  and a bounded queue, thread-per-connection is the right shape for a demo
//  server and it is far easier to reason about than epoll. What it does get
//  right is the parts that are actually load-bearing:
//
//    - SO_REUSEADDR, so a restart does not fail on TIME_WAIT
//    - a receive timeout, so a client that opens a socket and says nothing
//      cannot hold a worker forever (the slowloris case)
//    - a request size cap enforced while reading, not after
//    - load shedding: when the queue is full the connection gets a 503 and is
//      closed, instead of the accept loop blocking
//    - graceful shutdown via shutdown() on the listening socket, which breaks
//      accept() without the "close the fd and hope" race
//
//  Portable across POSIX and Winsock; the platform differences are confined to
//  the handful of aliases at the top.
// ============================================================================
#ifndef DAEDALUS_NET_SERVER_HPP
#define DAEDALUS_NET_SERVER_HPP

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "daedalus/concurrent/ThreadPool.hpp"
#include "daedalus/core/Exception.hpp"
#include "daedalus/net/Http.hpp"
#include "daedalus/net/Router.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace daedalus::net {

namespace platform {

#if defined(_WIN32)
using SocketHandle = SOCKET;
inline constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
inline void closeSocket(SocketHandle handle) { ::closesocket(handle); }
inline int lastError() { return ::WSAGetLastError(); }

/// Winsock needs explicit startup exactly once per process.
struct WinsockGuard {
    WinsockGuard() {
        WSADATA data;
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw DaedalusError("WSAStartup failed");
        }
    }
    ~WinsockGuard() { ::WSACleanup(); }
};
inline void ensureInitialised() { static WinsockGuard guard; }
#else
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;
inline void closeSocket(SocketHandle handle) { ::close(handle); }
inline int lastError() { return errno; }
inline void ensureInitialised() {}
#endif

}  // namespace platform

struct ServerConfig {
    std::string host{"127.0.0.1"};
    std::uint16_t port{8080};
    std::size_t workerThreads{4};
    std::size_t connectionQueueDepth{256};
    int listenBacklog{128};
    std::chrono::seconds receiveTimeout{10};
    HttpLimits limits{};
    std::size_t maximumKeepAliveRequests{64};
    bool logRequests{true};
};

/// Result of one handled request, for the access log and the metrics endpoint.
struct RequestRecord {
    std::string method;
    std::string path;
    int status{0};
    std::size_t responseBytes{0};
    double milliseconds{0.0};
};

class HttpServer {
public:
    using AccessLogger = std::function<void(const RequestRecord&)>;

    HttpServer(Router& router, ServerConfig config = ServerConfig{})
        : router_(router), config_(std::move(config)) {
        require(config_.workerThreads > 0, "server needs at least one worker thread");
    }

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    ~HttpServer() { stop(); }

    void setAccessLogger(AccessLogger logger) { accessLogger_ = std::move(logger); }

    /// Binds and starts listening. Returns once the socket is accepting, so a
    /// test can connect immediately afterwards with no sleep.
    void start() {
        if (running_.load()) return;
        platform::ensureInitialised();

        listening_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listening_ == platform::kInvalidSocket) {
            throw DaedalusError("could not create a listening socket");
        }

        int reuse = 1;
        ::setsockopt(listening_, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&reuse), sizeof(reuse));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(config_.port);
        if (::inet_pton(AF_INET, config_.host.c_str(), &address.sin_addr) != 1) {
            platform::closeSocket(listening_);
            listening_ = platform::kInvalidSocket;
            throw InvalidArgument("invalid bind address: " + config_.host);
        }

        if (::bind(listening_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            const int code = platform::lastError();
            platform::closeSocket(listening_);
            listening_ = platform::kInvalidSocket;
            throw DaedalusError("bind failed on port " + std::to_string(config_.port) +
                                " (error " + std::to_string(code) + ")");
        }
        if (::listen(listening_, config_.listenBacklog) != 0) {
            platform::closeSocket(listening_);
            listening_ = platform::kInvalidSocket;
            throw DaedalusError("listen failed");
        }

        // Port 0 means "any free port"; read back what the OS chose so tests
        // can bind without racing over a fixed port number.
        sockaddr_in bound{};
#if defined(_WIN32)
        int boundLength = sizeof(bound);
#else
        socklen_t boundLength = sizeof(bound);
#endif
        if (::getsockname(listening_, reinterpret_cast<sockaddr*>(&bound), &boundLength) == 0) {
            boundPort_ = ntohs(bound.sin_port);
        } else {
            boundPort_ = config_.port;
        }

        pool_ = std::make_unique<concurrent::ThreadPool>(config_.workerThreads,
                                                         config_.connectionQueueDepth);
        running_.store(true);
        acceptor_ = std::thread([this] { acceptLoop(); });
    }

    /// Stops accepting, drains in-flight requests and joins every thread.
    void stop() {
        if (!running_.exchange(false)) return;

        if (listening_ != platform::kInvalidSocket) {
            // shutdown() unblocks a thread sitting in accept(); closing alone
            // is racy because the fd number can be reused immediately.
#if defined(_WIN32)
            ::shutdown(listening_, SD_BOTH);
#else
            ::shutdown(listening_, SHUT_RDWR);
#endif
            platform::closeSocket(listening_);
            listening_ = platform::kInvalidSocket;
        }
        if (acceptor_.joinable()) acceptor_.join();
        if (pool_) {
            pool_->shutdown();
            pool_.reset();
        }
    }

    [[nodiscard]] bool running() const noexcept { return running_.load(); }
    [[nodiscard]] std::uint16_t boundPort() const noexcept { return boundPort_; }
    [[nodiscard]] std::size_t handledRequests() const noexcept { return handled_.load(); }
    [[nodiscard]] std::size_t sheddedConnections() const noexcept { return shedded_.load(); }

    [[nodiscard]] std::string baseUrl() const {
        return "http://" + config_.host + ":" + std::to_string(boundPort_);
    }

private:
    void acceptLoop() {
        while (running_.load()) {
            sockaddr_in peer{};
#if defined(_WIN32)
            int peerLength = sizeof(peer);
#else
            socklen_t peerLength = sizeof(peer);
#endif
            const platform::SocketHandle client =
                ::accept(listening_, reinterpret_cast<sockaddr*>(&peer), &peerLength);
            if (client == platform::kInvalidSocket) {
                if (!running_.load()) return;   // expected during shutdown
                continue;
            }

            char text[INET_ADDRSTRLEN] = {};
            ::inet_ntop(AF_INET, &peer.sin_addr, text, sizeof(text));
            const std::string clientAddress(text);

            // Shed rather than block: a full queue means the pool is saturated,
            // and stalling the accept loop would make it worse.
            if (!pool_->trySubmit([this, client, clientAddress] {
                    serveConnection(client, clientAddress);
                })) {
                ++shedded_;
                const std::string body =
                    HttpResponse::error(503, "server is busy").serialise(false);
                (void)sendAll(client, body);
                platform::closeSocket(client);
            }
        }
    }

    void serveConnection(platform::SocketHandle client, const std::string& clientAddress) {
        applyReceiveTimeout(client);

        for (std::size_t served = 0; served < config_.maximumKeepAliveRequests; ++served) {
            std::string raw;
            if (!readRequest(client, raw)) break;

            const auto started = std::chrono::steady_clock::now();
            HttpResponse response(200);
            bool keepAlive = true;

            try {
                HttpRequest request = parseRequest(raw, config_.limits);
                request.clientAddress = clientAddress;
                keepAlive = request.wantsKeepAlive();
                response = router_.dispatch(request);
                response.withSecurityHeaders();

                if (config_.logRequests || accessLogger_) {
                    const double elapsed =
                        std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - started)
                            .count();
                    if (accessLogger_) {
                        accessLogger_(RequestRecord{request.method, request.path,
                                                    response.status(), response.body().size(),
                                                    elapsed});
                    }
                }
            } catch (const InvalidArgument& bad) {
                response = HttpResponse::error(400, bad.what()).withSecurityHeaders();
                keepAlive = false;
            } catch (const std::exception& failure) {
                // Never leak an internal message to the client; log-worthy
                // detail stays server-side.
                (void)failure;
                response = HttpResponse::error(500, "internal server error")
                               .withSecurityHeaders();
                keepAlive = false;
            }

            ++handled_;
            if (!sendAll(client, response.serialise(keepAlive))) break;
            if (!keepAlive) break;
        }
        platform::closeSocket(client);
    }

    void applyReceiveTimeout(platform::SocketHandle client) const {
#if defined(_WIN32)
        DWORD milliseconds =
            static_cast<DWORD>(config_.receiveTimeout.count() * 1000);
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&milliseconds), sizeof(milliseconds));
#else
        timeval timeout{};
        timeout.tv_sec = static_cast<long>(config_.receiveTimeout.count());
        timeout.tv_usec = 0;
        ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#endif
    }

    /// Reads until the headers are complete and the declared body has arrived.
    /// Enforces the size cap DURING the read so an oversized request is cut off
    /// rather than buffered in full and then rejected.
    bool readRequest(platform::SocketHandle client, std::string& raw) const {
        const std::size_t hardCap =
            config_.limits.maximumHeaderBlock + config_.limits.maximumBody + 1024;
        char buffer[8192];

        for (;;) {
#if defined(_WIN32)
            const int received = ::recv(client, buffer, static_cast<int>(sizeof(buffer)), 0);
#else
            const ssize_t received = ::recv(client, buffer, sizeof(buffer), 0);
#endif
            if (received <= 0) return !raw.empty() && isComplete(raw);
            raw.append(buffer, static_cast<std::size_t>(received));
            if (raw.size() > hardCap) return false;
            if (isComplete(raw)) return true;
        }
    }

    [[nodiscard]] static bool isComplete(const std::string& raw) {
        const auto pending = pendingBodyBytes(raw);
        return pending.has_value() && *pending == 0;
    }

    static bool sendAll(platform::SocketHandle client, const std::string& data) {
        std::size_t sent = 0;
        while (sent < data.size()) {
#if defined(_WIN32)
            const int written = ::send(client, data.data() + sent,
                                       static_cast<int>(data.size() - sent), 0);
#else
            // MSG_NOSIGNAL: a client that vanishes mid-write must not kill the
            // process with SIGPIPE.
            const ssize_t written =
                ::send(client, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#endif
            if (written <= 0) return false;
            sent += static_cast<std::size_t>(written);
        }
        return true;
    }

    Router& router_;
    ServerConfig config_;
    platform::SocketHandle listening_{platform::kInvalidSocket};
    std::uint16_t boundPort_{0};
    std::atomic<bool> running_{false};
    std::atomic<std::size_t> handled_{0};
    std::atomic<std::size_t> shedded_{0};
    std::thread acceptor_;
    std::unique_ptr<concurrent::ThreadPool> pool_;
    AccessLogger accessLogger_;
};

}  // namespace daedalus::net

#endif  // DAEDALUS_NET_SERVER_HPP
