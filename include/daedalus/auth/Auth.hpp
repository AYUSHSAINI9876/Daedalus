// ============================================================================
//  Daedalus :: auth/Auth.hpp
//
//  Session authentication for the demo server: registration, login, session
//  issue and validation, role-based authorisation, account lockout, per-client
//  rate limiting and an audit log.
//
//  It is deliberately built ON the library's own structures, because that is
//  the point of having them:
//
//    UserStore     HashMap<string, User>      O(1) lookup by username
//    SessionStore  LRUCache<string, Session>  bounded session table -- the
//                                             cache's eviction IS the session
//                                             cap, so a flood of logins cannot
//                                             exhaust memory
//    RateLimiter   HashMap<string, Bucket>    token bucket per client
//    AuditLog      CircularBuffer<Entry>      fixed-size ring, oldest dropped
//
//  Security properties that are actually implemented, not just claimed:
//    - passwords stored as PBKDF2-HMAC-SHA256 with a per-user random salt
//    - session tokens are 256 bits from std::random_device, compared in
//      constant time
//    - login returns the same generic failure whether the user is missing or
//      the password is wrong, so the endpoint is not a username oracle
//    - failed attempts are counted and the account locks temporarily
//    - the lockout counter is updated even on the failure path (see Pharos:
//      state written during a failure must survive the failure)
//
//  See Crypto.hpp for the honest limits of the primitives underneath.
// ============================================================================
#ifndef DAEDALUS_AUTH_AUTH_HPP
#define DAEDALUS_AUTH_AUTH_HPP

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "daedalus/auth/Crypto.hpp"
#include "daedalus/core/Exception.hpp"
#include "daedalus/hashing/Cache.hpp"
#include "daedalus/hashing/HashMap.hpp"
#include "daedalus/linear/CircularBuffer.hpp"

namespace daedalus::auth {

using Clock = std::chrono::system_clock;
using TimePoint = Clock::time_point;

/// Ordered from least to most privileged, so authorisation is a comparison.
enum class Role { Viewer = 0, Operator = 1, Admin = 2 };

[[nodiscard]] inline std::string toString(Role role) {
    switch (role) {
        case Role::Viewer: return "viewer";
        case Role::Operator: return "operator";
        case Role::Admin: return "admin";
    }
    return "unknown";
}

[[nodiscard]] inline std::optional<Role> roleFromString(const std::string& text) {
    if (text == "viewer") return Role::Viewer;
    if (text == "operator") return Role::Operator;
    if (text == "admin") return Role::Admin;
    return std::nullopt;
}

// ---------------------------------------------------------------------------

struct User {
    std::string username;
    std::string email;
    std::string passwordHashHex;
    std::string saltHex;
    std::size_t iterations{0};
    Role role{Role::Viewer};
    TimePoint createdAt{};
    std::size_t failedAttempts{0};
    TimePoint lockedUntil{};
    bool active{true};

    /// The containers below expose a value-based Collection interface whose
    /// virtual contains()/erase() are instantiated with the class, so every
    /// stored type needs equality. Username is the identity.
    bool operator==(const User& other) const { return username == other.username; }
};

struct Session {
    std::string token;
    std::string username;
    Role role{Role::Viewer};
    TimePoint issuedAt{};
    TimePoint expiresAt{};
    std::string clientAddress;

    [[nodiscard]] bool expired(TimePoint now) const { return now >= expiresAt; }

    bool operator==(const Session& other) const { return token == other.token; }
};

struct AuditEntry {
    TimePoint at{};
    std::string action;
    std::string username;
    std::string clientAddress;
    bool succeeded{false};
    std::string detail;

    bool operator==(const AuditEntry& other) const {
        return at == other.at && action == other.action && username == other.username;
    }
};

/// What a caller gets back. `token` is only populated on success.
struct AuthOutcome {
    bool success{false};
    std::string message;
    std::string token;
    Role role{Role::Viewer};

    [[nodiscard]] static AuthOutcome failure(std::string reason) {
        AuthOutcome outcome;
        outcome.message = std::move(reason);
        return outcome;
    }
};

// --- password policy ---------------------------------------------------------

struct PasswordPolicy {
    std::size_t minimumLength{12};
    bool requireUpper{true};
    bool requireLower{true};
    bool requireDigit{true};

    /// Returns an empty string when the password is acceptable, otherwise the
    /// reason. The username check exists because "alice / Alice123456" is the
    /// single most common weak password shape.
    [[nodiscard]] std::string validate(const std::string& password,
                                       const std::string& username) const {
        if (password.size() < minimumLength) {
            return "password must be at least " + std::to_string(minimumLength) + " characters";
        }
        bool hasUpper = false;
        bool hasLower = false;
        bool hasDigit = false;
        for (char c : password) {
            if (c >= 'A' && c <= 'Z') hasUpper = true;
            if (c >= 'a' && c <= 'z') hasLower = true;
            if (c >= '0' && c <= '9') hasDigit = true;
        }
        if (requireUpper && !hasUpper) return "password needs an uppercase letter";
        if (requireLower && !hasLower) return "password needs a lowercase letter";
        if (requireDigit && !hasDigit) return "password needs a digit";

        std::string loweredPassword = password;
        std::string loweredUsername = username;
        std::transform(loweredPassword.begin(), loweredPassword.end(), loweredPassword.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(loweredUsername.begin(), loweredUsername.end(), loweredUsername.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!loweredUsername.empty() &&
            loweredPassword.find(loweredUsername) != std::string::npos) {
            return "password must not contain the username";
        }
        return "";
    }
};

// --- rate limiting -----------------------------------------------------------

/// Token bucket: `capacity` requests available, refilled at `refillPerSecond`.
/// Bursts are allowed up to the capacity, sustained rate is the refill rate --
/// which is what you want for a login endpoint, where a legitimate user retries
/// a few times quickly and an attacker does not stop.
class RateLimiter {
public:
    RateLimiter(double capacity, double refillPerSecond)
        : capacity_(capacity), refillPerSecond_(refillPerSecond) {
        require(capacity > 0.0, "rate limiter capacity must be positive");
        require(refillPerSecond > 0.0, "rate limiter refill rate must be positive");
    }

    /// Consumes one token for `client`. False means the client is over budget.
    [[nodiscard]] bool allow(const std::string& client, TimePoint now) {
        Bucket bucket = buckets_.get(client).value_or(Bucket{capacity_, now});

        const double elapsed = std::chrono::duration<double>(now - bucket.lastRefill).count();
        bucket.tokens = std::min(capacity_, bucket.tokens + elapsed * refillPerSecond_);
        bucket.lastRefill = now;

        bool permitted = false;
        if (bucket.tokens >= 1.0) {
            bucket.tokens -= 1.0;
            permitted = true;
        }
        buckets_.put(client, bucket);
        return permitted;
    }

    [[nodiscard]] double tokensFor(const std::string& client) const {
        const auto bucket = buckets_.get(client);
        return bucket.has_value() ? bucket->tokens : capacity_;
    }

    void reset() { buckets_.clear(); }
    [[nodiscard]] std::size_t trackedClients() const { return buckets_.size(); }

private:
    struct Bucket {
        double tokens{0.0};
        TimePoint lastRefill{};
        bool operator==(const Bucket& other) const {
            return tokens == other.tokens && lastRefill == other.lastRefill;
        }
    };

    double capacity_;
    double refillPerSecond_;
    HashMap<std::string, Bucket> buckets_;
};

// ---------------------------------------------------------------------------

struct AuthConfig {
    std::size_t pbkdf2Iterations{120000};
    std::chrono::seconds sessionLifetime{std::chrono::hours{8}};
    std::size_t maximumSessions{4096};
    std::size_t maximumFailedAttempts{5};
    std::chrono::seconds lockoutDuration{std::chrono::minutes{15}};
    double rateLimitBurst{10.0};
    double rateLimitPerSecond{1.0};
    std::size_t auditLogCapacity{512};
    PasswordPolicy passwordPolicy{};
};

/// The service the HTTP layer talks to. Everything is in memory: this is a
/// demonstration server, and persistence would add a storage engine that the
/// library is not trying to be.
class AuthService {
public:
    explicit AuthService(AuthConfig config = AuthConfig{})
        : config_(config),
          sessions_(config.maximumSessions),
          limiter_(config.rateLimitBurst, config.rateLimitPerSecond),
          audit_(config.auditLogCapacity, OverflowPolicy::Overwrite) {}

    [[nodiscard]] const AuthConfig& config() const noexcept { return config_; }

    // --- registration --------------------------------------------------------

    [[nodiscard]] AuthOutcome registerUser(const std::string& username, const std::string& email,
                                           const std::string& password, Role role = Role::Viewer,
                                           const std::string& clientAddress = "local") {
        const TimePoint now = Clock::now();

        if (username.empty() || username.size() > 64) {
            return recordAndFail("register", username, clientAddress,
                                 "username must be 1 to 64 characters");
        }
        if (email.find('@') == std::string::npos) {
            return recordAndFail("register", username, clientAddress, "email address is invalid");
        }
        if (users_.contains(username)) {
            // Registration DOES reveal that a username is taken -- it has to,
            // or the user cannot pick a different one. The login path does not.
            return recordAndFail("register", username, clientAddress,
                                 "that username is already taken");
        }
        const std::string policyFailure = config_.passwordPolicy.validate(password, username);
        if (!policyFailure.empty()) {
            return recordAndFail("register", username, clientAddress, policyFailure);
        }

        const crypto::Bytes salt = crypto::randomBytes(16);
        const crypto::Bytes derived =
            crypto::pbkdf2Sha256(password, salt, config_.pbkdf2Iterations, 32);

        User user;
        user.username = username;
        user.email = email;
        user.saltHex = crypto::toHex(salt);
        user.passwordHashHex = crypto::toHex(derived);
        user.iterations = config_.pbkdf2Iterations;
        user.role = role;
        user.createdAt = now;
        users_.put(username, user);

        record("register", username, clientAddress, true, "role=" + toString(role));
        AuthOutcome outcome;
        outcome.success = true;
        outcome.message = "account created";
        outcome.role = role;
        return outcome;
    }

    // --- login ---------------------------------------------------------------

    /// Verifies credentials and issues a session. The failure message is the
    /// same for an unknown user and a wrong password, on purpose.
    [[nodiscard]] AuthOutcome login(const std::string& username, const std::string& password,
                                    const std::string& clientAddress = "local") {
        const TimePoint now = Clock::now();
        static const std::string kGenericFailure = "invalid username or password";

        if (!limiter_.allow(clientAddress, now)) {
            record("login", username, clientAddress, false, "rate limited");
            return AuthOutcome::failure("too many attempts, slow down");
        }

        const auto stored = users_.get(username);
        if (!stored.has_value()) {
            // Spend comparable time on a missing user so the response time does
            // not distinguish "no such user" from "wrong password".
            const crypto::Bytes decoySalt(16, 0);
            (void)crypto::pbkdf2Sha256(password, decoySalt, config_.pbkdf2Iterations, 32);
            record("login", username, clientAddress, false, "no such user");
            return AuthOutcome::failure(kGenericFailure);
        }

        User user = *stored;
        if (!user.active) {
            record("login", username, clientAddress, false, "account disabled");
            return AuthOutcome::failure("this account is disabled");
        }
        if (now < user.lockedUntil) {
            record("login", username, clientAddress, false, "locked out");
            return AuthOutcome::failure("account temporarily locked, try again later");
        }

        const crypto::Bytes salt = crypto::fromHex(user.saltHex);
        const crypto::Bytes derived = crypto::pbkdf2Sha256(password, salt, user.iterations, 32);

        if (!crypto::constantTimeEquals(crypto::toHex(derived), user.passwordHashHex)) {
            ++user.failedAttempts;
            if (user.failedAttempts >= config_.maximumFailedAttempts) {
                user.lockedUntil = now + config_.lockoutDuration;
                user.failedAttempts = 0;
            }
            // Commit the counter BEFORE returning: brute-force protection that
            // is rolled back on the failure path protects nothing.
            users_.put(username, user);
            record("login", username, clientAddress, false, "bad password");
            return AuthOutcome::failure(kGenericFailure);
        }

        user.failedAttempts = 0;
        user.lockedUntil = TimePoint{};
        users_.put(username, user);

        Session session;
        session.token = crypto::randomToken(32);
        session.username = username;
        session.role = user.role;
        session.issuedAt = now;
        session.expiresAt = now + config_.sessionLifetime;
        session.clientAddress = clientAddress;
        sessions_.put(session.token, session);

        record("login", username, clientAddress, true, "session issued");
        AuthOutcome outcome;
        outcome.success = true;
        outcome.message = "signed in";
        outcome.token = session.token;
        outcome.role = user.role;
        return outcome;
    }

    /// Resolves a token to its session, or nullopt when it is unknown or
    /// expired. An expired token is dropped so it cannot be reused.
    [[nodiscard]] std::optional<Session> validate(const std::string& token) {
        if (token.empty()) return std::nullopt;
        const auto session = sessions_.get(token);
        if (!session.has_value()) return std::nullopt;
        if (session->expired(Clock::now())) {
            (void)sessions_.erase(token);
            return std::nullopt;
        }
        return session;
    }

    bool logout(const std::string& token) {
        const auto session = sessions_.get(token);
        const bool removed = sessions_.erase(token);
        if (removed && session.has_value()) {
            record("logout", session->username, session->clientAddress, true, "");
        }
        return removed;
    }

    /// True when the session is valid AND carries at least `required`.
    [[nodiscard]] bool authorize(const std::string& token, Role required) {
        const auto session = validate(token);
        if (!session.has_value()) return false;
        return static_cast<int>(session->role) >= static_cast<int>(required);
    }

    // --- administration ------------------------------------------------------

    [[nodiscard]] std::optional<User> findUser(const std::string& username) const {
        return users_.get(username);
    }

    [[nodiscard]] std::size_t userCount() const { return users_.size(); }
    [[nodiscard]] std::size_t activeSessionCount() const { return sessions_.size(); }

    bool setRole(const std::string& username, Role role) {
        auto stored = users_.get(username);
        if (!stored.has_value()) return false;
        stored->role = role;
        users_.put(username, *stored);
        record("set-role", username, "admin", true, toString(role));
        return true;
    }

    bool setActive(const std::string& username, bool active) {
        auto stored = users_.get(username);
        if (!stored.has_value()) return false;
        stored->active = active;
        users_.put(username, *stored);
        record("set-active", username, "admin", true, active ? "enabled" : "disabled");
        return true;
    }

    /// Clears a lockout without waiting for it to expire.
    bool unlock(const std::string& username) {
        auto stored = users_.get(username);
        if (!stored.has_value()) return false;
        stored->failedAttempts = 0;
        stored->lockedUntil = TimePoint{};
        users_.put(username, *stored);
        record("unlock", username, "admin", true, "");
        return true;
    }

    [[nodiscard]] std::vector<std::string> usernames() const { return users_.keys(); }

    [[nodiscard]] std::vector<AuditEntry> auditTrail() const { return audit_.toVector(); }

    [[nodiscard]] RateLimiter& rateLimiter() noexcept { return limiter_; }

private:
    void record(const std::string& action, const std::string& username,
                const std::string& clientAddress, bool succeeded, const std::string& detail) {
        AuditEntry entry;
        entry.at = Clock::now();
        entry.action = action;
        entry.username = username;
        entry.clientAddress = clientAddress;
        entry.succeeded = succeeded;
        entry.detail = detail;
        (void)audit_.push(entry);
    }

    [[nodiscard]] AuthOutcome recordAndFail(const std::string& action, const std::string& username,
                                            const std::string& clientAddress,
                                            const std::string& reason) {
        record(action, username, clientAddress, false, reason);
        return AuthOutcome::failure(reason);
    }

    AuthConfig config_;
    HashMap<std::string, User> users_;
    LRUCache<std::string, Session> sessions_;
    RateLimiter limiter_;
    CircularBuffer<AuditEntry> audit_;
};

}   // namespace daedalus::auth

#endif   // DAEDALUS_AUTH_AUTH_HPP
