// Minimal check macros shared by the test files (one binary, sdb_tests).
#pragma once
#include <cstdio>

inline int g_fail = 0, g_checks = 0;
#define CHECK(...)                                                               \
    do                                                                           \
    {                                                                            \
        ++g_checks;                                                              \
        if (!(__VA_ARGS__))                                                      \
        {                                                                        \
            ++g_fail;                                                            \
            std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
        }                                                                        \
    } while (0)
#define CHECK_EQ(a, b)                                                                                         \
    do                                                                                                         \
    {                                                                                                          \
        ++g_checks;                                                                                            \
        auto va = (a);                                                                                         \
        auto vb = (b);                                                                                         \
        if (!(va == vb))                                                                                       \
        {                                                                                                      \
            ++g_fail;                                                                                          \
            std::printf("  FAIL %s:%d  %s == %s  (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, (long long)va, \
                        (long long)vb);                                                                        \
        }                                                                                                      \
    } while (0)

void TestAutomation();
void TestScenario();
void TestOps();
void TestScenarioRender();
void TestActions();
void TestRuntime();
void TestIo();
void TestTagsAndPages();
void TestSubtrees();
void TestWebSocket();
void TestDeckHub();
void TestDaemonDecks();
void TestHotplug();
