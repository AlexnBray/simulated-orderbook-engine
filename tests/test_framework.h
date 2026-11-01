#pragma once
// Minimal, dependency-free test harness (no network / package manager needed).
//
//   TEST_CASE(name) { CHECK(cond); CHECK_EQ(a, b); REQUIRE(cond); }
//
// CHECK*   records a failure and continues.
// REQUIRE* records a failure and aborts the current test case.
// Run a test binary with a substring argument to filter cases, e.g. ./test_matching priority
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace tf {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
inline int& currentFailures() {
    static int n = 0;
    return n;
}
struct RequireFailed {};

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void reportFailure(const char* file, int line, const std::string& msg) {
    ++currentFailures();
    std::cerr << "    " << file << ':' << line << ": FAILED: " << msg << '\n';
}

template <typename T>
std::string show(const T& v) {
    std::ostringstream ss;
    ss << v;
    return ss.str();
}

inline int runAll(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int passed = 0, failed = 0;
    for (const auto& tc : registry()) {
        if (!filter.empty() && std::string(tc.name).find(filter) == std::string::npos) continue;
        currentFailures() = 0;
        try {
            tc.fn();
        } catch (const RequireFailed&) {
        } catch (const std::exception& e) {
            reportFailure(__FILE__, __LINE__, std::string("unexpected exception: ") + e.what());
        }
        if (currentFailures() == 0) {
            ++passed;
            std::cout << "[PASS] " << tc.name << '\n';
        } else {
            ++failed;
            std::cout << "[FAIL] " << tc.name << '\n';
        }
    }
    std::cout << passed << " passed, " << failed << " failed\n";
    if (passed + failed == 0) {
        std::cerr << "no tests matched filter '" << filter << "'\n";
        return 1;
    }
    return failed == 0 ? 0 : 1;
}

} // namespace tf

#define TF_CAT2(a, b) a##b
#define TF_CAT(a, b) TF_CAT2(a, b)

#define TEST_CASE(name)                                                \
    static void name();                                                \
    static ::tf::Registrar TF_CAT(tf_registrar_, name)(#name, &name);  \
    static void name()

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) ::tf::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define REQUIRE(cond)                                                               \
    do {                                                                            \
        if (!(cond)) {                                                              \
            ::tf::reportFailure(__FILE__, __LINE__, "REQUIRE(" #cond ")");          \
            throw ::tf::RequireFailed{};                                            \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                                              \
    do {                                                                                            \
        const auto tf_a = (a);                                                                      \
        const auto tf_b = (b);                                                                      \
        if (!(tf_a == tf_b))                                                                        \
            ::tf::reportFailure(__FILE__, __LINE__,                                                 \
                                "CHECK_EQ(" #a ", " #b ") -> " + ::tf::show(tf_a) + " != " + ::tf::show(tf_b)); \
    } while (0)

#define TEST_MAIN() \
    int main(int argc, char** argv) { return ::tf::runAll(argc, argv); }
