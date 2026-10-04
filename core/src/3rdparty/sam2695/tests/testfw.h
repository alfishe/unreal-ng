// libsam2695 test framework - shared by every test translation unit.
//
// The libopl4 CHECK macros with exact-value reporting, plus named test registration: TEST(Group, Name)
// registers "Group.Name" (the names the conformance table in README.md refers to). main() lives in
// sam2695tests.cpp; `sam2695tests <substring>` runs only the tests whose name contains it.
#ifndef SAM2695_TESTFW_H
#define SAM2695_TESTFW_H

#include <cmath>
#include <cstdio>
#include <vector>

namespace sam2695test
{

extern int gChecks;
extern int gFailed;

inline void Fail(const char* what, const char* file, int line)
{
    std::printf("  FAIL %s:%d: %s\n", file, line, what);
    gFailed++;
}

struct TestCase
{
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& Registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar
{
    Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

} // namespace sam2695test

#define SAM_CONCAT2(a, b) a##b
#define SAM_CONCAT(a, b) SAM_CONCAT2(a, b)

#define TEST(group, name)                                                                     \
    static void SAM_CONCAT(Test, SAM_CONCAT(group, name))();                                  \
    static const sam2695test::Registrar SAM_CONCAT(gReg, SAM_CONCAT(group, name))(            \
        #group "." #name, &SAM_CONCAT(Test, SAM_CONCAT(group, name)));                        \
    static void SAM_CONCAT(Test, SAM_CONCAT(group, name))()

#define CHECK(cond)                                                                           \
    do                                                                                        \
    {                                                                                         \
        sam2695test::gChecks++;                                                               \
        if (!(cond))                                                                          \
            sam2695test::Fail(#cond, __FILE__, __LINE__);                                     \
    } while (0)

#define CHECK_EQ_I(a, b)                                                                      \
    do                                                                                        \
    {                                                                                         \
        sam2695test::gChecks++;                                                               \
        const long long va_ = static_cast<long long>(a);                                      \
        const long long vb_ = static_cast<long long>(b);                                      \
        if (va_ != vb_)                                                                       \
        {                                                                                     \
            std::printf("  FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, \
                        va_, vb_);                                                            \
            sam2695test::gFailed++;                                                           \
        }                                                                                     \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                                 \
    do                                                                                        \
    {                                                                                         \
        sam2695test::gChecks++;                                                               \
        const double va_ = static_cast<double>(a);                                            \
        const double vb_ = static_cast<double>(b);                                            \
        if (!(std::fabs(va_ - vb_) <= (tol)))                                                 \
        {                                                                                     \
            std::printf("  FAIL %s:%d: %s ~= %s (%.9g vs %.9g, tol %.3g)\n", __FILE__,         \
                        __LINE__, #a, #b, va_, vb_, static_cast<double>(tol));                \
            sam2695test::gFailed++;                                                           \
        }                                                                                     \
    } while (0)

#endif // SAM2695_TESTFW_H
