// libsam2695 test runner: runs every registered test (or those whose name contains argv[1]).
#include "testfw.h"

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
        t.fn();
        run++;
        const bool ok = gFailed == before;
        if (!ok)
            failedTests++;
        std::printf("%s %s\n", ok ? "  ok  " : "FAILED", t.name);
    }
    std::printf("\nsam2695tests: %d tests, %d checks, %d failed checks, %d failed tests\n", run, gChecks, gFailed,
                failedTests);
    return gFailed == 0 ? 0 : 1;
}
