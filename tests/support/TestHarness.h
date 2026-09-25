#pragma once
// A tiny, dependency-free test runner: TEST(name) { ... } with non-fatal
// CHECK macros. On failure the plugin's own debug log for that test is
// dumped so you can see exactly which transition went wrong.

#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "TestLogger.h"

namespace harness {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failuresInCurrentTest() {
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> body) {
        registry().push_back({name, std::move(body)});
    }
};

inline void reportFailure(const char* file, int line, const std::string& message) {
    ++failuresInCurrentTest();
    std::cerr << "    FAIL " << file << ":" << line << "\n      " << message << "\n";
}

/// Runs every registered test whose name contains argv[1] (all if absent).
inline int runAll(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : "";
    int ran = 0;
    int failed = 0;
    for (const auto& test : registry()) {
        if (test.name.find(filter) == std::string::npos) {
            continue;
        }
        ++ran;
        failuresInCurrentTest() = 0;
        testlog::clear();
        std::cerr << "[ RUN  ] " << test.name << "\n";
        try {
            test.body();
        } catch (const std::exception& ex) {
            reportFailure(__FILE__, __LINE__, std::string("uncaught exception: ") + ex.what());
        }
        if (failuresInCurrentTest() > 0) {
            ++failed;
            std::cerr << "    --- plugin debug log for this test ---\n";
            for (const auto& line : testlog::lines()) {
                std::cerr << "    " << line << "\n";
            }
            std::cerr << "[ FAIL ] " << test.name << "\n";
        } else {
            std::cerr << "[  OK  ] " << test.name << "\n";
        }
    }
    std::cerr << "\n" << (ran - failed) << "/" << ran << " tests passed\n";
    return failed == 0 ? 0 : 1;
}

} // namespace harness

#define TEST(name)                                                                       \
    static void test_##name();                                                           \
    static harness::Registrar registrar_##name(#name, test_##name);                      \
    static void test_##name()

#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) harness::reportFailure(__FILE__, __LINE__, "CHECK(" #cond ")");     \
    } while (0)

#define CHECK_MSG(cond, msg)                                                             \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::ostringstream os_;                                                      \
            os_ << msg;                                                                  \
            harness::reportFailure(__FILE__, __LINE__, os_.str());                       \
        }                                                                                \
    } while (0)

#define CHECK_EQ(a, b)                                                                   \
    do {                                                                                 \
        const auto va_ = (a);                                                            \
        const auto vb_ = (b);                                                            \
        if (!(va_ == vb_)) {                                                             \
            std::ostringstream os_;                                                      \
            os_ << #a " == " #b "\n      left:  " << va_ << "\n      right: " << vb_;    \
            harness::reportFailure(__FILE__, __LINE__, os_.str());                       \
        }                                                                                \
    } while (0)
