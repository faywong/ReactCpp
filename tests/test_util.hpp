#pragma once

#include <cstdio>
#include <cstdlib>

namespace reactcpp::test {

[[noreturn]] inline void fail(const char* expr, const char* file, int line) {
    std::fprintf(stderr, "TEST ASSERTION FAILED: %s\n  at %s:%d\n", expr, file, line);
    std::fflush(stderr);
    std::abort();
}

}

#define REACTCPP_TEST_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            ::reactcpp::test::fail(#expr, __FILE__, __LINE__); \
        } \
    } while (0)
