// minitest.h - minimal self-contained test framework for PoC 017.
//
// Deliberately dependency-free (the PoC must build standalone): TEST blocks
// auto-register, CHECK/CHECK_EQ report file:line, RUN_TESTS() returns the
// number of failures (0 = success).

#ifndef Z80POC_MINITEST_H
#define Z80POC_MINITEST_H

#include <cstdio>
#include <string>
#include <vector>

namespace minitest
{
struct TestCase
{
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> registry;
    return registry;
}

inline int& Failures()
{
    static int failures = 0;
    return failures;
}

inline bool& CurrentFailed()
{
    static bool currentFailed = false;
    return currentFailed;
}

struct Registrar
{
    Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

inline void ReportCheck(const char* file, int line, const char* expr)
{
    std::printf("    FAIL %s:%d: %s\n", file, line, expr);
    ++Failures();
    CurrentFailed() = true;
}

template <typename A, typename B>
bool CompareEq(const A& a, const B& b)
{
    return a == b;
}

template <typename A, typename B>
void ReportEq(const char* file, int line, const char* expr, const A& actual, const B& expected)
{
    std::printf("    FAIL %s:%d: %s\n      actual:   %lld (0x%llX)\n      expected: %lld (0x%llX)\n",
                file, line, expr, static_cast<long long>(actual), static_cast<unsigned long long>(actual),
                static_cast<long long>(expected), static_cast<unsigned long long>(expected));
    ++Failures();
    CurrentFailed() = true;
}
}  // namespace minitest

#define MINITEST_CAT2(a, b) a##b
#define MINITEST_CAT(a, b) MINITEST_CAT2(a, b)

#define TEST(name)                                                                        \
    static void MINITEST_CAT(minitest_fn_, __LINE__)();                                   \
    static ::minitest::Registrar MINITEST_CAT(minitest_reg_, __LINE__)(#name,             \
        &MINITEST_CAT(minitest_fn_, __LINE__));                                           \
    static void MINITEST_CAT(minitest_fn_, __LINE__)()

#define CHECK(expr)                                                                       \
    do                                                                                    \
    {                                                                                     \
        if (!(expr))                                                                      \
            ::minitest::ReportCheck(__FILE__, __LINE__, #expr);                           \
    } while (0)

// Integer-friendly equality with actual/expected dump.
#define CHECK_EQ(actual, expected)                                                        \
    do                                                                                    \
    {                                                                                     \
        auto&& mt_a_ = (actual);                                                          \
        auto&& mt_e_ = (expected);                                                        \
        if (!::minitest::CompareEq(mt_a_, mt_e_))                                         \
            ::minitest::ReportEq(__FILE__, __LINE__, #actual " == " #expected, mt_a_,     \
                                 mt_e_);                                                  \
    } while (0)

namespace minitest
{
inline int RunAll()
{
    int run = 0, failedTests = 0;
    for (const TestCase& tc : Registry())
    {
        CurrentFailed() = false;
        std::printf("[ RUN  ] %s\n", tc.name);
        tc.fn();
        ++run;
        if (CurrentFailed())
        {
            ++failedTests;
            std::printf("[ FAIL ] %s\n", tc.name);
        }
        else
        {
            std::printf("[  OK  ] %s\n", tc.name);
        }
    }
    std::printf("\n%d test(s), %d failure(s)\n", run, failedTests);
    return failedTests == 0 ? 0 : 1;
}
}  // namespace minitest

#define RUN_TESTS() (::minitest::RunAll())

#endif  // Z80POC_MINITEST_H
