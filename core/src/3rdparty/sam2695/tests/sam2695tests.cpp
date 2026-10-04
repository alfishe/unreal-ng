// libsam2695 test runner: runs every registered test (or those whose name contains argv[1]).
#include "testfw.h"

#include <chrono>
#include <cstring>

namespace sam2695test
{
int gChecks = 0;
int gFailed = 0;
} // namespace sam2695test

int main(int argc, char** argv)
{
    using namespace sam2695test;
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedTests = 0;
    for (const TestCase& t : Registry())
    {
        if (filter != nullptr && std::strstr(t.name, filter) == nullptr)
            continue;
        const int before = gFailed;
        const auto t0 = std::chrono::steady_clock::now();
        t.fn();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        run++;
        const bool ok = gFailed == before;
        if (!ok)
            failedTests++;
        std::printf("%s %-44s %8.2f ms\n", ok ? "  ok  " : "FAILED", t.name, ms);
    }
    std::printf("\nsam2695tests: %d tests, %d checks, %d failed checks, %d failed tests\n", run, gChecks, gFailed,
                failedTests);
    return gFailed == 0 ? 0 : 1;
}
