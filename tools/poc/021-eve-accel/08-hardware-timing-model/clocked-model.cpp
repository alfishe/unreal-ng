// eve-accel 08: what stepping the FT812's line pipeline clock by clock costs on a CPU, next
// to computing the same line's timing analytically (what eve-emu's line cost does).
//
// The public documentation describes the engine as: one display list command fetched per
// system clock; each primitive's span filled at 16 (or 8, 4, 2) pixels per clock; the line
// has HCYCLE x PCLK clocks (at least 2048) before its buffer is shifted out [PG §2.5.7].
// A clock-stepped model walks that schedule one clock at a time, so it can stop exactly at
// the clock where the budget runs out (an overflowing line). This micro-benchmark measures
// the cost of the stepping itself (the pixel work is the same in both models and is left
// out): a state machine per clock vs the closed form per command and span.
//
//   clocked-model [lines]
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace
{

struct Command
{
    uint32_t pixels;          // span pixels on this line (0: a state command)
    uint32_t pixelsPerClock;  // 16, 8, 4 or 2
};

// One line, clock by clock: returns the clocks used and how many pixels of the last
// command were filled when the budget ran out (pixel-exact truncation point)
uint32_t StepLine(const std::vector<Command>& list, uint32_t budget, uint32_t& cutCommand, uint32_t& cutPixels)
{
    enum State { Fetch, Fill } state = Fetch;
    uint32_t clock = 0, pc = 0, left = 0;
    cutCommand = UINT32_MAX;
    cutPixels = 0;
    while (pc < list.size())
    {
        if (clock == budget)
        {
            cutCommand = pc;
            cutPixels = state == Fill ? list[pc].pixels - left : 0;
            return clock;
        }
        ++clock;
        switch (state)
        {
        case Fetch:
            if (list[pc].pixels == 0)
                ++pc;
            else
            {
                left = list[pc].pixels;
                state = Fill;
            }
            break;
        case Fill:
            left = left > list[pc].pixelsPerClock ? left - list[pc].pixelsPerClock : 0;
            if (left == 0)
            {
                state = Fetch;
                ++pc;
            }
            break;
        }
    }
    return clock;
}

// The same schedule in closed form: one step per command
uint32_t ComputeLine(const std::vector<Command>& list, uint32_t budget, uint32_t& cutCommand, uint32_t& cutPixels)
{
    uint32_t clock = 0;
    cutCommand = UINT32_MAX;
    cutPixels = 0;
    for (uint32_t pc = 0; pc < list.size(); ++pc)
    {
        const Command& c = list[pc];
        const uint32_t cost = 1 + (c.pixels + c.pixelsPerClock - 1) / c.pixelsPerClock;
        if (clock + cost > budget)
        {
            cutCommand = pc;
            cutPixels = clock + 1 < budget ? (budget - clock - 1) * c.pixelsPerClock : 0;
            if (cutPixels > c.pixels)
                cutPixels = c.pixels;
            return budget;
        }
        clock += c.pixels ? cost : 1;
    }
    return clock;
}

} // namespace

int main(int argc, char** argv)
{
    const uint32_t lines = argc > 1 ? static_cast<uint32_t>(std::atoi(argv[1])) : 200000;
    // An R-Type-like line (02/08 census: ~124 commands, ~160 fill clocks): 120 state
    // commands, a full-width background (1024 px at 16/clk), 30 sprites of 48 px at 8/clk
    std::vector<Command> list;
    for (int i = 0; i < 60; ++i)
        list.push_back({0, 16});
    list.push_back({1024, 16});
    for (int i = 0; i < 30; ++i)
    {
        list.push_back({0, 16});
        list.push_back({48, 8});
    }
    for (int i = 0; i < 30; ++i)
        list.push_back({0, 16});
    const uint32_t budget = 1344; // HCYCLE 1344 x PCLK 1 (VDAC2 mode 7)
    uint64_t sink = 0;
    uint32_t cutCommand = 0, cutPixels = 0;
    auto time = [&](auto&& fn) {
        const auto t0 = std::chrono::steady_clock::now();
        for (uint32_t l = 0; l < lines; ++l)
            sink += fn(list, budget - (l & 7), cutCommand, cutPixels) + cutPixels;
        return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / lines;
    };
    const double stepped = time(StepLine);
    const double computed = time(ComputeLine);
    uint32_t clocks = StepLine(list, 100000, cutCommand, cutPixels);
    const double linesPerSecond = 806.0 * 59.08; // VCYCLE 806 x 59 Hz: lines the chip scans per second
    std::printf("line: %zu commands, %u clocks of %u budget\n", list.size(), clocks, budget);
    std::printf("clock-stepped: %.0f ns per line (%.2f ns per clock) -> %.1f %% of one core at %.0f lines/s\n", stepped,
                stepped / clocks, 100.0 * stepped * linesPerSecond / 1e9, linesPerSecond);
    std::printf("closed form:   %.0f ns per line -> %.2f %% of one core\n", computed,
                100.0 * computed * linesPerSecond / 1e9);
    std::printf("(sink %llu)\n", static_cast<unsigned long long>(sink));
    return 0;
}
