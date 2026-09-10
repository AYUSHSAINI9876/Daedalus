// ============================================================================
//  Daedalus :: examples/daedalus_server.cpp
//
//  Runs the web playground: the HTTP server, the auth service and the JSON API
//  over the library.
//
//      ./daedalus_server --port 8080 --web-root web --seed
//
//  --seed creates three demo accounts so the playground is usable immediately.
//  It is refused unless --allow-weak-seed is also passed OR the bind address is
//  loopback, because seeding fixed credentials on a public interface is exactly
//  the mistake that turns a demo into an incident.
// ============================================================================
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

#include "daedalus/auth/Auth.hpp"
#include "daedalus/core/Version.hpp"
#include "daedalus/net/Api.hpp"
#include "daedalus/net/Router.hpp"
#include "daedalus/net/Server.hpp"

using namespace daedalus;

namespace {

std::atomic<bool> g_stopRequested{false};

extern "C" void handleSignal(int) {
    g_stopRequested.store(true);
}

struct Options {
    std::string host{"127.0.0.1"};
    std::uint16_t port{8080};
    std::string webRoot{"web"};
    std::size_t workers{4};
    bool seed{false};
    bool allowWeakSeed{false};
    bool quiet{false};
};

void printUsage(const char* program) {
    std::cout << daedalus::banner() << "\n\n"
              << "usage: " << program << " [options]\n"
              << "  --host <address>     bind address (default 127.0.0.1)\n"
              << "  --port <number>      port, 0 for any free port (default 8080)\n"
              << "  --web-root <dir>     directory holding index.html (default web)\n"
              << "  --workers <count>    worker threads (default 4)\n"
              << "  --seed               create the three demo accounts\n"
              << "  --allow-weak-seed    permit --seed on a non-loopback address\n"
              << "  --quiet              do not log each request\n"
              << "  --help               this message\n";
}

/// The demo accounts. One shared passphrase, and it deliberately does not
/// contain any of the usernames -- the password policy rejects that.
void seedAccounts(auth::AuthService& service) {
    const std::string passphrase = "Minotaur-Thread-2026";
    (void)service.registerUser("admin", "admin@daedalus.dev", passphrase, auth::Role::Admin,
                               "seed");
    (void)service.registerUser("operator", "operator@daedalus.dev", passphrase,
                               auth::Role::Operator, "seed");
    (void)service.registerUser("viewer", "viewer@daedalus.dev", passphrase, auth::Role::Viewer,
                               "seed");

    std::cout << "  seeded accounts: admin / operator / viewer\n"
              << "  password:        " << passphrase << "\n";
}

}   // namespace

int main(int argc, char** argv) {
    Options options;

    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        const auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << "\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (flag == "--help" || flag == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (flag == "--host") {
            options.host = next("--host");
        } else if (flag == "--port") {
            options.port = static_cast<std::uint16_t>(std::stoi(next("--port")));
        } else if (flag == "--web-root") {
            options.webRoot = next("--web-root");
        } else if (flag == "--workers") {
            options.workers = static_cast<std::size_t>(std::stoul(next("--workers")));
        } else if (flag == "--seed") {
            options.seed = true;
        } else if (flag == "--allow-weak-seed") {
            options.allowWeakSeed = true;
        } else if (flag == "--quiet") {
            options.quiet = true;
        } else {
            std::cerr << "unknown option: " << flag << "\n";
            printUsage(argv[0]);
            return 2;
        }
    }

    const bool loopback = options.host == "127.0.0.1" || options.host == "localhost";
    if (options.seed && !loopback && !options.allowWeakSeed) {
        std::cerr << "refusing to seed fixed demo credentials on " << options.host
                  << ".\nPass --allow-weak-seed if this really is what you want.\n";
        return 2;
    }

    try {
        auth::AuthService service;
        net::StaticFiles files(options.webRoot);
        net::Router router;
        net::ApiOptions apiOptions;
        apiOptions.documentRoot = options.webRoot;
        net::buildApi(router, service, files, apiOptions);

        net::ServerConfig config;
        config.host = options.host;
        config.port = options.port;
        config.workerThreads = options.workers;
        config.logRequests = !options.quiet;

        net::HttpServer server(router, config);
        if (!options.quiet) {
            server.setAccessLogger([](const net::RequestRecord& record) {
                std::cout << "  " << record.method << " " << record.path << " -> " << record.status
                          << "  " << record.responseBytes << "B  " << record.milliseconds << "ms\n";
            });
        }

        std::cout << daedalus::banner() << "\n";
        server.start();
        std::cout << "  listening on " << server.baseUrl() << "\n"
                  << "  document root: " << files.root() << "\n"
                  << "  routes: " << router.routeCount() << "\n";
        if (options.seed) seedAccounts(service);
        std::cout << "  press Ctrl+C to stop\n\n";

        std::signal(SIGINT, handleSignal);
        std::signal(SIGTERM, handleSignal);
        while (!g_stopRequested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\n  shutting down after " << server.handledRequests() << " request(s)\n";
        server.stop();
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << "fatal: " << failure.what() << "\n";
        return 1;
    }
}
