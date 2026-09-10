// ============================================================================
//  Unit tests for the crypto primitives and the authentication service.
//
//  The primitives are checked against the OFFICIAL published test vectors --
//  FIPS 180-4 for SHA-256, RFC 4231 for HMAC-SHA256, RFC 7914 for PBKDF2. A
//  hash function that is self-consistent but wrong is worse than useless, so
//  "it round-trips" is not an acceptable test here.
// ============================================================================
#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "daedalus/auth/Auth.hpp"
#include "daedalus/auth/Crypto.hpp"
#include "framework/TestFramework.hpp"

using namespace daedalus;
using namespace daedalus::auth;
namespace crypto = daedalus::crypto;

namespace {

/// A config with a low iteration count so the tests stay fast. Production uses
/// the 120,000 default; the algorithm under test is identical either way.
AuthConfig fastConfig() {
    AuthConfig config;
    config.pbkdf2Iterations = 1000;
    return config;
}

const std::string kGoodPassword = "Labyrinth2026x";

}  // namespace

// ============================================================================
//  SHA-256 -- FIPS 180-4 vectors
// ============================================================================

DAEDALUS_TEST(Crypto, sha256_official_vectors) {
    CHECK_EQ(crypto::toHex(crypto::sha256(std::string(""))),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(crypto::toHex(crypto::sha256(std::string("abc"))),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(crypto::toHex(crypto::sha256(std::string(
                 "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))),
             std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
}

DAEDALUS_TEST(Crypto, sha256_handles_block_boundaries) {
    // 55, 56 and 64 bytes exercise the padding cases: just fits, forces an
    // extra block, and exactly one block.
    for (std::size_t length : {std::size_t{55}, std::size_t{56}, std::size_t{63},
                               std::size_t{64}, std::size_t{65}, std::size_t{1000}}) {
        const std::string message(length, 'a');
        const crypto::Digest digest = crypto::sha256(message);

        // Streaming in one-byte chunks must give the same answer.
        crypto::Sha256 streamed;
        for (char c : message) {
            streamed.update(reinterpret_cast<const std::uint8_t*>(&c), 1);
        }
        CHECK_EQ(crypto::toHex(streamed.finish()), crypto::toHex(digest));
    }
}

DAEDALUS_TEST(Crypto, sha256_million_a_vector) {
    // The FIPS long message: one million 'a' characters.
    crypto::Sha256 hasher;
    const std::string chunk(1000, 'a');
    for (int i = 0; i < 1000; ++i) hasher.update(chunk);
    CHECK_EQ(crypto::toHex(hasher.finish()),
             std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
}

DAEDALUS_TEST(Crypto, sha256_object_is_reusable_after_finish) {
    crypto::Sha256 hasher;
    hasher.update(std::string("abc"));
    const std::string first = crypto::toHex(hasher.finish());
    hasher.update(std::string("abc"));
    CHECK_EQ(crypto::toHex(hasher.finish()), first);
}

// ============================================================================
//  HMAC -- RFC 4231 vectors
// ============================================================================

DAEDALUS_TEST(Crypto, hmac_sha256_official_vectors) {
    const crypto::Bytes key1(20, 0x0b);
    const std::string data1 = "Hi There";
    CHECK_EQ(crypto::toHex(crypto::hmacSha256(key1, crypto::Bytes(data1.begin(), data1.end()))),
             std::string("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));

    CHECK_EQ(crypto::toHex(crypto::hmacSha256("Jefe", "what do ya want for nothing?")),
             std::string("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));

    // A key longer than the 64-byte block must be hashed down first.
    const crypto::Bytes longKey(131, 0xaa);
    const std::string data3 = "Test Using Larger Than Block-Size Key - Hash Key First";
    CHECK_EQ(crypto::toHex(crypto::hmacSha256(longKey, crypto::Bytes(data3.begin(), data3.end()))),
             std::string("60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"));
}

// ============================================================================
//  PBKDF2 -- RFC 7914 section 11 vectors
// ============================================================================

DAEDALUS_TEST(Crypto, pbkdf2_official_vectors) {
    const std::string salt = "salt";
    const crypto::Bytes saltBytes(salt.begin(), salt.end());

    CHECK_EQ(crypto::toHex(crypto::pbkdf2Sha256("password", saltBytes, 1, 32)),
             std::string("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"));
    CHECK_EQ(crypto::toHex(crypto::pbkdf2Sha256("password", saltBytes, 2, 32)),
             std::string("ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"));
    CHECK_EQ(crypto::toHex(crypto::pbkdf2Sha256("password", saltBytes, 4096, 32)),
             std::string("c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"));
}

DAEDALUS_TEST(Crypto, pbkdf2_output_length_and_validation) {
    const crypto::Bytes salt{1, 2, 3, 4};
    CHECK_EQ(crypto::pbkdf2Sha256("pw", salt, 10, 16).size(), 16u);
    CHECK_EQ(crypto::pbkdf2Sha256("pw", salt, 10, 64).size(), 64u);
    // A longer request must extend, not restart: the first 32 bytes agree.
    const std::string shortHex = crypto::toHex(crypto::pbkdf2Sha256("pw", salt, 10, 32));
    const std::string longHex = crypto::toHex(crypto::pbkdf2Sha256("pw", salt, 10, 64));
    CHECK_EQ(longHex.substr(0, 64), shortHex);

    CHECK_THROWS_AS(crypto::pbkdf2Sha256("pw", salt, 0, 16), InvalidArgument);
    CHECK_THROWS_AS(crypto::pbkdf2Sha256("pw", salt, 10, 0), InvalidArgument);
}

// ============================================================================
//  Encoding and comparison
// ============================================================================

DAEDALUS_TEST(Crypto, hex_round_trip) {
    const crypto::Bytes data{0x00, 0x0f, 0x10, 0xff, 0x7a};
    CHECK_EQ(crypto::toHex(data), std::string("000f10ff7a"));
    CHECK_EQ(crypto::fromHex("000f10ff7a"), data);
    CHECK_EQ(crypto::fromHex("000F10FF7A"), data);
    CHECK_THROWS_AS(crypto::fromHex("abc"), InvalidArgument);
    CHECK_THROWS_AS(crypto::fromHex("zz"), InvalidArgument);
}

DAEDALUS_TEST(Crypto, base64_vectors) {
    const auto encode = [](const std::string& text) {
        return crypto::toBase64(crypto::Bytes(text.begin(), text.end()));
    };
    CHECK_EQ(encode(""), std::string(""));
    CHECK_EQ(encode("M"), std::string("TQ=="));
    CHECK_EQ(encode("Ma"), std::string("TWE="));
    CHECK_EQ(encode("Man"), std::string("TWFu"));
    CHECK_EQ(encode("any carnal pleasure."), std::string("YW55IGNhcm5hbCBwbGVhc3VyZS4="));
}

DAEDALUS_TEST(Crypto, constant_time_equality) {
    CHECK_TRUE(crypto::constantTimeEquals("abcdef", "abcdef"));
    CHECK_FALSE(crypto::constantTimeEquals("abcdef", "abcdeg"));
    CHECK_FALSE(crypto::constantTimeEquals("abcdef", "abcde"));   // different lengths
    CHECK_TRUE(crypto::constantTimeEquals("", ""));
}

DAEDALUS_TEST(Crypto, random_tokens_are_unique_and_sized) {
    const std::string token = crypto::randomToken(32);
    CHECK_EQ(token.size(), 64u);   // 32 bytes as hex

    std::vector<std::string> tokens;
    for (int i = 0; i < 200; ++i) tokens.push_back(crypto::randomToken(32));
    std::sort(tokens.begin(), tokens.end());
    CHECK_TRUE(std::unique(tokens.begin(), tokens.end()) == tokens.end());
    CHECK_EQ(crypto::randomBytes(0).size(), 0u);
}

// ============================================================================
//  Password policy
// ============================================================================

DAEDALUS_TEST(Auth, password_policy) {
    const PasswordPolicy policy;
    CHECK_EQ(policy.validate("Labyrinth2026x", "ayush"), std::string(""));
    CHECK_FALSE(policy.validate("short1A", "ayush").empty());
    CHECK_FALSE(policy.validate("alllowercase1", "ayush").empty());   // no uppercase
    CHECK_FALSE(policy.validate("ALLUPPERCASE1", "ayush").empty());   // no lowercase
    CHECK_FALSE(policy.validate("NoDigitsAtAllHere", "ayush").empty());
    // The username must not appear, case-insensitively.
    CHECK_FALSE(policy.validate("MyAYUSHpass123", "ayush").empty());
}

// ============================================================================
//  Registration and login
// ============================================================================

DAEDALUS_TEST(Auth, registration_and_login_round_trip) {
    AuthService service(fastConfig());

    const auto registered = service.registerUser("ayush", "ayush@example.com", kGoodPassword,
                                                 Role::Admin);
    CHECK_TRUE(registered.success);
    CHECK_EQ(service.userCount(), 1u);

    const auto signedIn = service.login("ayush", kGoodPassword);
    CHECK_TRUE(signedIn.success);
    CHECK_EQ(signedIn.token.size(), 64u);
    CHECK_TRUE(signedIn.role == Role::Admin);

    const auto session = service.validate(signedIn.token);
    CHECK_TRUE(session.has_value());
    CHECK_EQ(session->username, std::string("ayush"));
    CHECK_EQ(service.activeSessionCount(), 1u);
}

DAEDALUS_TEST(Auth, passwords_are_never_stored_in_the_clear) {
    AuthService service(fastConfig());
    (void)service.registerUser("ayush", "a@b.com", kGoodPassword);

    const auto stored = service.findUser("ayush").value();
    CHECK_NE(stored.passwordHashHex, kGoodPassword);
    CHECK_EQ(stored.passwordHashHex.size(), 64u);
    CHECK_EQ(stored.saltHex.size(), 32u);
    CHECK_TRUE(stored.passwordHashHex.find(kGoodPassword) == std::string::npos);
}

DAEDALUS_TEST(Auth, the_same_password_hashes_differently_for_two_users) {
    AuthService service(fastConfig());
    (void)service.registerUser("one", "one@example.com", kGoodPassword);
    (void)service.registerUser("two", "two@example.com", kGoodPassword);

    // Different random salts, so identical passwords must not collide -- this
    // is what makes a stolen table non-precomputable.
    CHECK_NE(service.findUser("one")->passwordHashHex,
             service.findUser("two")->passwordHashHex);
    CHECK_NE(service.findUser("one")->saltHex, service.findUser("two")->saltHex);
}

DAEDALUS_TEST(Auth, registration_rejects_bad_input) {
    AuthService service(fastConfig());
    CHECK_FALSE(service.registerUser("", "a@b.com", kGoodPassword).success);
    CHECK_FALSE(service.registerUser("ayush", "not-an-email", kGoodPassword).success);
    CHECK_FALSE(service.registerUser("ayush", "a@b.com", "weak").success);

    CHECK_TRUE(service.registerUser("ayush", "a@b.com", kGoodPassword).success);
    const auto duplicate = service.registerUser("ayush", "other@b.com", kGoodPassword);
    CHECK_FALSE(duplicate.success);
    CHECK_TRUE(duplicate.message.find("taken") != std::string::npos);
    CHECK_EQ(service.userCount(), 1u);
}

DAEDALUS_TEST(Auth, login_does_not_reveal_whether_a_username_exists) {
    AuthService service(fastConfig());
    (void)service.registerUser("ayush", "a@b.com", kGoodPassword);

    const auto wrongPassword = service.login("ayush", "WrongPassword123");
    const auto noSuchUser = service.login("nobody", "WrongPassword123");
    CHECK_FALSE(wrongPassword.success);
    CHECK_FALSE(noSuchUser.success);
    // Identical message: the endpoint is not a username oracle.
    CHECK_EQ(wrongPassword.message, noSuchUser.message);
    CHECK_TRUE(wrongPassword.token.empty());
}

DAEDALUS_TEST(Auth, account_locks_after_repeated_failures) {
    AuthConfig config = fastConfig();
    config.maximumFailedAttempts = 3;
    config.rateLimitBurst = 100.0;   // isolate the lockout from the rate limiter
    AuthService service(config);
    (void)service.registerUser("ayush", "a@b.com", kGoodPassword);

    for (int attempt = 0; attempt < 3; ++attempt) {
        CHECK_FALSE(service.login("ayush", "WrongPassword123").success);
    }
    // Even the CORRECT password is refused while locked.
    const auto locked = service.login("ayush", kGoodPassword);
    CHECK_FALSE(locked.success);
    CHECK_TRUE(locked.message.find("locked") != std::string::npos);

    CHECK_TRUE(service.unlock("ayush"));
    CHECK_TRUE(service.login("ayush", kGoodPassword).success);
}

DAEDALUS_TEST(Auth, a_successful_login_resets_the_failure_counter) {
    AuthConfig config = fastConfig();
    config.maximumFailedAttempts = 3;
    config.rateLimitBurst = 100.0;
    AuthService service(config);
    (void)service.registerUser("ayush", "a@b.com", kGoodPassword);

    CHECK_FALSE(service.login("ayush", "WrongPassword123").success);
    CHECK_FALSE(service.login("ayush", "WrongPassword123").success);
    CHECK_TRUE(service.login("ayush", kGoodPassword).success);
    CHECK_EQ(service.findUser("ayush")->failedAttempts, 0u);

    // Two more failures must not lock, because the counter restarted.
    CHECK_FALSE(service.login("ayush", "WrongPassword123").success);
    CHECK_FALSE(service.login("ayush", "WrongPassword123").success);
    CHECK_TRUE(service.login("ayush", kGoodPassword).success);
}

DAEDALUS_TEST(Auth, disabled_accounts_cannot_sign_in) {
    AuthService service(fastConfig());
    (void)service.registerUser("ayush", "a@b.com", kGoodPassword);
    CHECK_TRUE(service.setActive("ayush", false));
    CHECK_FALSE(service.login("ayush", kGoodPassword).success);
    CHECK_TRUE(service.setActive("ayush", true));
    CHECK_TRUE(service.login("ayush", kGoodPassword).success);
    CHECK_FALSE(service.setActive("ghost", false));
}

// ============================================================================
//  Sessions
// ============================================================================

DAEDALUS_TEST(Auth, sessions_expire) {
    AuthConfig config = fastConfig();
    config.sessionLifetime = std::chrono::seconds{0};   // expires immediately
    AuthService service(config);
    (void)service.registerUser("ayush", "a@b.com", kGoodPassword);

    const auto signedIn = service.login("ayush", kGoodPassword);
    CHECK_TRUE(signedIn.success);
    CHECK_FALSE(service.validate(signedIn.token).has_value());
    // The expired token is dropped, so it cannot be replayed.
    CHECK_EQ(service.activeSessionCount(), 0u);
}

DAEDALUS_TEST(Auth, logout_invalidates_the_token) {
    AuthService service(fastConfig());
    (void)service.registerUser("ayush", "a@b.com", kGoodPassword);
    const auto signedIn = service.login("ayush", kGoodPassword);

    CHECK_TRUE(service.validate(signedIn.token).has_value());
    CHECK_TRUE(service.logout(signedIn.token));
    CHECK_FALSE(service.validate(signedIn.token).has_value());
    CHECK_FALSE(service.logout(signedIn.token));   // already gone
}

DAEDALUS_TEST(Auth, unknown_and_empty_tokens_are_rejected) {
    AuthService service(fastConfig());
    CHECK_FALSE(service.validate("").has_value());
    CHECK_FALSE(service.validate("not-a-real-token").has_value());
    CHECK_FALSE(service.validate(std::string(64, 'a')).has_value());
}

DAEDALUS_TEST(Auth, the_session_table_is_bounded) {
    AuthConfig config = fastConfig();
    config.maximumSessions = 4;
    AuthService service(config);
    (void)service.registerUser("ayush", "a@b.com", kGoodPassword);

    std::vector<std::string> tokens;
    for (int i = 0; i < 10; ++i) {
        const auto signedIn = service.login("ayush", kGoodPassword);
        CHECK_TRUE(signedIn.success);
        tokens.push_back(signedIn.token);
    }
    // The LRU cache caps the table: a login flood evicts, it does not grow.
    CHECK_EQ(service.activeSessionCount(), 4u);
    CHECK_TRUE(service.validate(tokens.back()).has_value());
    CHECK_FALSE(service.validate(tokens.front()).has_value());
}

// ============================================================================
//  Authorisation
// ============================================================================

DAEDALUS_TEST(Auth, roles_are_ordered_and_enforced) {
    AuthService service(fastConfig());
    (void)service.registerUser("viewer", "v@b.com", kGoodPassword, Role::Viewer);
    (void)service.registerUser("operator", "o@b.com", kGoodPassword, Role::Operator);
    (void)service.registerUser("admin", "a@b.com", kGoodPassword, Role::Admin);

    const std::string viewerToken = service.login("viewer", kGoodPassword).token;
    const std::string operatorToken = service.login("operator", kGoodPassword).token;
    const std::string adminToken = service.login("admin", kGoodPassword).token;

    CHECK_TRUE(service.authorize(viewerToken, Role::Viewer));
    CHECK_FALSE(service.authorize(viewerToken, Role::Operator));
    CHECK_FALSE(service.authorize(viewerToken, Role::Admin));

    CHECK_TRUE(service.authorize(operatorToken, Role::Viewer));
    CHECK_TRUE(service.authorize(operatorToken, Role::Operator));
    CHECK_FALSE(service.authorize(operatorToken, Role::Admin));

    CHECK_TRUE(service.authorize(adminToken, Role::Admin));
    CHECK_FALSE(service.authorize("bogus", Role::Viewer));
}

DAEDALUS_TEST(Auth, role_changes_take_effect_on_the_next_login) {
    AuthService service(fastConfig());
    (void)service.registerUser("ayush", "a@b.com", kGoodPassword, Role::Viewer);
    CHECK_TRUE(service.setRole("ayush", Role::Admin));
    const auto signedIn = service.login("ayush", kGoodPassword);
    CHECK_TRUE(service.authorize(signedIn.token, Role::Admin));
    CHECK_FALSE(service.setRole("ghost", Role::Admin));
}

DAEDALUS_TEST(Auth, role_names_round_trip) {
    for (Role role : {Role::Viewer, Role::Operator, Role::Admin}) {
        CHECK_TRUE(roleFromString(toString(role)).value() == role);
    }
    CHECK_FALSE(roleFromString("superuser").has_value());
}

// ============================================================================
//  Rate limiting and audit
// ============================================================================

DAEDALUS_TEST(Auth, rate_limiter_allows_a_burst_then_throttles) {
    RateLimiter limiter(5.0, 1.0);
    const auto now = Clock::now();

    for (int i = 0; i < 5; ++i) CHECK_TRUE(limiter.allow("10.0.0.1", now));
    CHECK_FALSE(limiter.allow("10.0.0.1", now));           // burst exhausted
    CHECK_TRUE(limiter.allow("10.0.0.2", now));            // a different client is unaffected

    // Two seconds later, two tokens have been refilled.
    const auto later = now + std::chrono::seconds{2};
    CHECK_TRUE(limiter.allow("10.0.0.1", later));
    CHECK_TRUE(limiter.allow("10.0.0.1", later));
    CHECK_FALSE(limiter.allow("10.0.0.1", later));

    CHECK_THROWS_AS(RateLimiter(0.0, 1.0), InvalidArgument);
    CHECK_THROWS_AS(RateLimiter(1.0, 0.0), InvalidArgument);
}

DAEDALUS_TEST(Auth, login_is_rate_limited_per_client) {
    AuthConfig config = fastConfig();
    config.rateLimitBurst = 3.0;
    config.rateLimitPerSecond = 0.001;   // effectively no refill during the test
    config.maximumFailedAttempts = 100;  // isolate from the lockout path
    AuthService service(config);
    (void)service.registerUser("ayush", "a@b.com", kGoodPassword, Role::Viewer, "setup");

    for (int i = 0; i < 3; ++i) {
        (void)service.login("ayush", "WrongPassword123", "10.1.1.1");
    }
    const auto throttled = service.login("ayush", kGoodPassword, "10.1.1.1");
    CHECK_FALSE(throttled.success);
    CHECK_TRUE(throttled.message.find("too many") != std::string::npos);

    // A different address still gets through.
    CHECK_TRUE(service.login("ayush", kGoodPassword, "10.2.2.2").success);
}

DAEDALUS_TEST(Auth, audit_trail_records_successes_and_failures) {
    AuthService service(fastConfig());
    (void)service.registerUser("ayush", "a@b.com", kGoodPassword, Role::Viewer, "10.0.0.5");
    (void)service.login("ayush", "WrongPassword123", "10.0.0.5");
    (void)service.login("ayush", kGoodPassword, "10.0.0.5");

    const auto trail = service.auditTrail();
    CHECK_EQ(trail.size(), 3u);
    CHECK_EQ(trail[0].action, std::string("register"));
    CHECK_TRUE(trail[0].succeeded);
    CHECK_FALSE(trail[1].succeeded);
    CHECK_TRUE(trail[2].succeeded);
    CHECK_EQ(trail[2].clientAddress, std::string("10.0.0.5"));
}

DAEDALUS_TEST(Auth, audit_trail_is_bounded) {
    AuthConfig config = fastConfig();
    config.auditLogCapacity = 8;
    config.rateLimitBurst = 1000.0;
    AuthService service(config);
    (void)service.registerUser("ayush", "a@b.com", kGoodPassword);

    for (int i = 0; i < 50; ++i) (void)service.login("ayush", "WrongPassword123");
    // The ring buffer keeps the most recent entries and drops the rest.
    CHECK_EQ(service.auditTrail().size(), 8u);
}

DAEDALUS_TEST(Auth, usernames_are_enumerable_for_admin_views) {
    AuthService service(fastConfig());
    (void)service.registerUser("alpha", "a@b.com", kGoodPassword);
    (void)service.registerUser("beta", "b@b.com", kGoodPassword);
    auto names = service.usernames();
    std::sort(names.begin(), names.end());
    CHECK_EQ(names, (std::vector<std::string>{"alpha", "beta"}));
}
