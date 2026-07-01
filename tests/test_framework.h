#pragma once

// Minimal, zero-dependency test harness (spec Section 16: no third-party libs).
// Each test file exposes a run_*_tests() function; test_main.cpp calls them and
// returns non-zero if any check failed.

#include <cstdio>

namespace qqtest {

inline int g_checks = 0;
inline int g_failures = 0;

inline void report(bool ok, const char* expr, const char* file, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s  (%s:%d)\n", expr, file, line);
    }
}

} // namespace qqtest

#define QQ_CHECK(cond) \
    ::qqtest::report((cond), #cond, __FILE__, __LINE__)

#define QQ_CHECK_EQ(a, b) \
    ::qqtest::report((a) == (b), #a " == " #b, __FILE__, __LINE__)
