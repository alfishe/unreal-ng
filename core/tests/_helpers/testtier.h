#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <vector>

/// @brief The long tier of core-tests: `UNREAL_LONG_TESTS=1`.
///
/// A test proves its claim in the default run with the shortest scenario that can fail: a few frames, one replay, a
/// sample of a corpus (budget: about 10 ms, an integration test under 1 s). Where the full scenario finds more (every
/// file of a corpus, a whole boot, thousands of frames), it runs that too when the variable is set. CI's full run sets
/// it (`build-gcc-latest`). One variable for every suite: no new per-suite switch (core/tests/README.md, Test Speed).
namespace TestTier
{
/// @brief true when `UNREAL_LONG_TESTS` is `1`: run the full variants
inline bool Long()
{
    static const bool on = [] {
        const char* v = std::getenv("UNREAL_LONG_TESTS");
        return v && v[0] == '1' && v[1] == '\0';
    }();
    return on;
}

/// @brief true when `UNREAL_FUNCTIONAL_TESTS` is `1`: run the functional tests.
///
/// A functional test needs what a unit test must not touch: a real clock or timer, real threads whose pace decides the
/// outcome, the kernel (uhid), a boot scenario bounded by a deadline. Its suite name ends in `_Functional`, its file lives
/// in core/tests/functional/ (`*_functional_test.cpp`), and it skips itself (SKIP_UNLESS_FUNCTIONAL) unless this variable
/// is set: `UNREAL_FUNCTIONAL_TESTS=1 core-tests --gtest_filter='*_Functional.*'` runs exactly them. Not part of the
/// default run, nor of CI's (core/tests/README.md, Functional tests).
inline bool Functional()
{
    static const bool on = [] {
        const char* v = std::getenv("UNREAL_FUNCTIONAL_TESTS");
        return v && v[0] == '1' && v[1] == '\0';
    }();
    return on;
}

/// @brief `full` in the long tier, `quick` otherwise
template <typename T>
T Pick(T quick, T full)
{
    return Long() ? full : quick;
}

/// @brief A corpus: every file in the long tier, otherwise those of @p files named in @p sample (a file name each)
inline std::vector<std::filesystem::path> Sample(const std::vector<std::filesystem::path>& files,
                                                 std::initializer_list<const char*> sample)
{
    if (Long())
        return files;
    std::vector<std::filesystem::path> picked;
    for (const std::filesystem::path& file : files)
        if (std::any_of(sample.begin(), sample.end(), [&](const char* name) { return file.filename() == name; }))
            picked.push_back(file);
    return picked;
}
}  // namespace TestTier

/// @brief First line of a functional test (and of its fixture's SetUp): skips unless `UNREAL_FUNCTIONAL_TESTS=1`
#define SKIP_UNLESS_FUNCTIONAL()                                                                              \
    do                                                                                                        \
    {                                                                                                         \
        if (!TestTier::Functional())                                                                          \
            GTEST_SKIP() << "functional test: set UNREAL_FUNCTIONAL_TESTS=1 and filter '*_Functional.*'";    \
    } while (0)
