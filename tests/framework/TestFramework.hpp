// ============================================================================
//  Daedalus :: tests/framework/TestFramework.hpp
//
//  A small self-contained xUnit runner. Deliberately dependency-free: the
//  library itself pulls in nothing outside the standard library, and the test
//  suite keeps that promise so a clean checkout builds and tests offline with
//  nothing but a compiler and CMake.
//
//  Output is pure ASCII on purpose -- a Windows console in the cp1252 code
//  page throws on non-ASCII writes, which would fail the suite for the wrong
//  reason.
//
//  Usage:
//      DAEDALUS_TEST(DynamicArray, push_back_grows) {
//          daedalus::DynamicArray<int> a;
//          a.pushBack(1);
//          CHECK_EQ(a.size(), 1u);
//      }
//
//  Flags: --list  --filter=<substring>  --verbose
// ============================================================================
#ifndef DAEDALUS_TEST_FRAMEWORK_HPP
#define DAEDALUS_TEST_FRAMEWORK_HPP

#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace daedalus::testing {

// --- value rendering ---------------------------------------------------------

template <typename T, typename = void>
struct IsStreamable : std::false_type {};

template <typename T>
struct IsStreamable<
    T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<const T&>())>>
    : std::true_type {};

template <typename T, typename = void>
struct IsIterable : std::false_type {};

template <typename T>
struct IsIterable<T, std::void_t<decltype(std::begin(std::declval<const T&>())),
                                 decltype(std::end(std::declval<const T&>()))>> : std::true_type {};

template <typename T>
std::string stringify(const T& value);

inline std::string stringify(const std::string& value) {
    return "\"" + value + "\"";
}
inline std::string stringify(const char* value) {
    return std::string("\"") + value + "\"";
}
inline std::string stringify(bool value) {
    return value ? "true" : "false";
}

template <typename A, typename B>
std::string stringify(const std::pair<A, B>& value) {
    return "(" + stringify(value.first) + ", " + stringify(value.second) + ")";
}

template <typename T>
std::string stringify(const T& value) {
    if constexpr (IsStreamable<T>::value) {
        std::ostringstream os;
        os << value;
        return os.str();
    } else if constexpr (IsIterable<T>::value) {
        std::ostringstream os;
        os << "[";
        bool first = true;
        for (const auto& element : value) {
            if (!first) os << ", ";
            os << stringify(element);
            first = false;
        }
        os << "]";
        return os.str();
    } else {
        return "<unprintable>";
    }
}

// --- failure signalling ------------------------------------------------------

class AssertionFailure : public std::exception {
public:
    AssertionFailure(std::string file, int line, std::string message) {
        rendered_ =
            std::move(file) + ":" + std::to_string(line) + "\n           " + std::move(message);
    }
    [[nodiscard]] const char* what() const noexcept override { return rendered_.c_str(); }

private:
    std::string rendered_;
};

// --- registry ----------------------------------------------------------------

using TestBody = void (*)();

struct TestCase {
    std::string suite;
    std::string name;
    TestBody body;
    [[nodiscard]] std::string fullName() const { return suite + "." + name; }
};

class Registry {
public:
    static Registry& instance() {
        static Registry registry;
        return registry;
    }

    void add(TestCase test) { tests_.push_back(std::move(test)); }

    [[nodiscard]] const std::vector<TestCase>& tests() const noexcept { return tests_; }

    int run(int argc, char** argv) const {
        std::string filter;
        bool listOnly = false;
        bool verbose = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--list") {
                listOnly = true;
            } else if (arg == "--verbose" || arg == "-v") {
                verbose = true;
            } else if (arg.rfind("--filter=", 0) == 0) {
                filter = arg.substr(9);
            } else {
                std::cerr << "unknown option: " << arg << "\n"
                          << "usage: " << argv[0] << " [--list] [--filter=<substr>] [--verbose]\n";
                return 2;
            }
        }

        if (listOnly) {
            for (const auto& test : tests_) std::cout << test.fullName() << "\n";
            return 0;
        }

        std::size_t passed = 0;
        std::vector<std::string> failures;
        const auto started = std::chrono::steady_clock::now();

        for (const auto& test : tests_) {
            const std::string label = test.fullName();
            if (!filter.empty() && label.find(filter) == std::string::npos) continue;

            if (verbose) std::cout << "[ RUN      ] " << label << "\n";
            const auto caseStart = std::chrono::steady_clock::now();
            std::string error;
            try {
                test.body();
            } catch (const AssertionFailure& failure) {
                error = failure.what();
            } catch (const std::exception& ex) {
                error = std::string("unexpected exception: ") + ex.what();
            } catch (...) {
                error = "unexpected non-standard exception";
            }
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - caseStart)
                                       .count();

            if (error.empty()) {
                ++passed;
                if (verbose) {
                    std::cout << "[       OK ] " << label << " (" << elapsedMs << " ms)\n";
                }
            } else {
                failures.push_back(label);
                std::cout << "[  FAILED  ] " << label << "\n           " << error << "\n";
            }
        }

        const auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();

        const std::size_t executed = passed + failures.size();
        std::cout << "-----------------------------------------------------------\n";
        std::cout << "  ran " << executed << " test(s) in " << totalMs << " ms"
                  << "   passed=" << passed << "   failed=" << failures.size() << "\n";
        if (!failures.empty()) {
            std::cout << "  failing tests:\n";
            for (const auto& name : failures) std::cout << "    - " << name << "\n";
        }
        std::cout << (failures.empty() ? "  RESULT: PASS\n" : "  RESULT: FAIL\n");
        std::cout << "-----------------------------------------------------------\n";

        if (executed == 0) {
            std::cerr << "  no test matched the requested filter\n";
            return 3;
        }
        return failures.empty() ? 0 : 1;
    }

private:
    std::vector<TestCase> tests_;
};

struct Registrar {
    Registrar(const char* suite, const char* name, TestBody body) {
        Registry::instance().add(TestCase{suite, name, body});
    }
};

}   // namespace daedalus::testing

// --- macros ------------------------------------------------------------------

#define DAEDALUS_TEST(suite, name)                                             \
    static void daedalus_test_##suite##_##name();                              \
    static const ::daedalus::testing::Registrar daedalus_reg_##suite##_##name( \
        #suite, #name, &daedalus_test_##suite##_##name);                       \
    static void daedalus_test_##suite##_##name()

#define DAEDALUS_FAIL(message) \
    throw ::daedalus::testing::AssertionFailure(__FILE__, __LINE__, (message))

#define CHECK_TRUE(expr)                                                  \
    do {                                                                  \
        if (!(expr)) DAEDALUS_FAIL(std::string("expected true: " #expr)); \
    } while (false)

#define CHECK_FALSE(expr)                                                 \
    do {                                                                  \
        if ((expr)) DAEDALUS_FAIL(std::string("expected false: " #expr)); \
    } while (false)

#define CHECK_EQ(lhs, rhs)                                                                         \
    do {                                                                                           \
        const auto daedalus_lhs = (lhs);                                                           \
        const auto daedalus_rhs = (rhs);                                                           \
        if (!(daedalus_lhs == daedalus_rhs))                                                       \
            DAEDALUS_FAIL(std::string("expected " #lhs " == " #rhs "\n") +                         \
                          "             actual: " + ::daedalus::testing::stringify(daedalus_lhs) + \
                          "\n" +                                                                   \
                          "           expected: " + ::daedalus::testing::stringify(daedalus_rhs)); \
    } while (false)

#define CHECK_NE(lhs, rhs)                                                          \
    do {                                                                            \
        const auto daedalus_lhs = (lhs);                                            \
        const auto daedalus_rhs = (rhs);                                            \
        if ((daedalus_lhs == daedalus_rhs))                                         \
            DAEDALUS_FAIL(std::string("expected " #lhs " != " #rhs ", both are ") + \
                          ::daedalus::testing::stringify(daedalus_lhs));            \
    } while (false)

#define CHECK_LT(lhs, rhs)                                                         \
    do {                                                                           \
        const auto daedalus_lhs = (lhs);                                           \
        const auto daedalus_rhs = (rhs);                                           \
        if (!(daedalus_lhs < daedalus_rhs))                                        \
            DAEDALUS_FAIL(std::string("expected " #lhs " < " #rhs ", got ") +      \
                          ::daedalus::testing::stringify(daedalus_lhs) + " and " + \
                          ::daedalus::testing::stringify(daedalus_rhs));           \
    } while (false)

#define CHECK_LE(lhs, rhs)                                                         \
    do {                                                                           \
        const auto daedalus_lhs = (lhs);                                           \
        const auto daedalus_rhs = (rhs);                                           \
        if (!(daedalus_lhs <= daedalus_rhs))                                       \
            DAEDALUS_FAIL(std::string("expected " #lhs " <= " #rhs ", got ") +     \
                          ::daedalus::testing::stringify(daedalus_lhs) + " and " + \
                          ::daedalus::testing::stringify(daedalus_rhs));           \
    } while (false)

#define CHECK_NEAR(lhs, rhs, tolerance)                                                   \
    do {                                                                                  \
        const double daedalus_diff = static_cast<double>(lhs) - static_cast<double>(rhs); \
        const double daedalus_abs = daedalus_diff < 0 ? -daedalus_diff : daedalus_diff;   \
        if (!(daedalus_abs <= static_cast<double>(tolerance)))                            \
            DAEDALUS_FAIL(std::string("expected " #lhs " ~= " #rhs " within " #tolerance  \
                                      ", difference was ") +                              \
                          std::to_string(daedalus_abs));                                  \
    } while (false)

#define CHECK_THROWS_AS(expr, ExceptionType)                                                       \
    do {                                                                                           \
        bool daedalus_threw = false;                                                               \
        try {                                                                                      \
            (void)(expr);                                                                          \
        } catch (const ExceptionType&) {                                                           \
            daedalus_threw = true;                                                                 \
        } catch (const std::exception& daedalus_ex) {                                              \
            DAEDALUS_FAIL(std::string("expected " #ExceptionType " from " #expr                    \
                                      " but got a different exception: ") +                        \
                          daedalus_ex.what());                                                     \
        }                                                                                          \
        if (!daedalus_threw)                                                                       \
            DAEDALUS_FAIL(                                                                         \
                std::string("expected " #ExceptionType " from " #expr " but nothing was thrown")); \
    } while (false)

#define CHECK_NO_THROW(expr)                                                                \
    do {                                                                                    \
        try {                                                                               \
            (void)(expr);                                                                   \
        } catch (const std::exception& daedalus_ex) {                                       \
            DAEDALUS_FAIL(std::string(#expr " threw unexpectedly: ") + daedalus_ex.what()); \
        }                                                                                   \
    } while (false)

#endif   // DAEDALUS_TEST_FRAMEWORK_HPP
