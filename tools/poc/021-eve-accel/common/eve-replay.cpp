// eve-replay - replays an .evr bus capture into the library and checks it.
//
// eve-accel copy: taken from eve-emu (branch aa-threads, tools/replay/eve-replay.cpp) with
// the POC options --threads N, --stats and --snap DIR FROM TO (see common/eve-poc.h).
//
// The capture (unreal-ng docs/inprogress/2026-10-01-tsconf-vdac2/vdac2-test-corpus.md §4)
// holds every chip select change and every byte on the FT812's bus with the chip's answer,
// stamped with system clocks, and a record at each frame end with a hash of the picture.
// Replaying it drives the chip alone: every answer byte and every frame count must match,
// and every drawn frame's hash. The run also measures how fast the library is against the
// chip's real time - the reference workload for performance work.
//
//   eve-replay <capture.evr> [--rom <ft81x.rom>] [--frames N] [--no-draw] [--no-hash] [--quiet]
//              [--dump FROM TO DIR]
//
//   --no-draw  the chip draws nothing (timing only); --no-hash  draws, but skips the picture
//   check (its hashing is the harness's cost, not the library's)
//   --dump     for frames FROM..TO: print the coprocessor state, write DIR/frame-N-WxH.argb
//              (the raw ARGB8888 picture) and DIR/frame-N-dl.txt (the active display list,
//              disassembled) - what a picture mismatch or a flicker is investigated with
//
// The output buffer follows what the capturing host did: HSIZE x VSIZE ARGB8888, filled
// with opaque black when (re)sized, resized after a frame end when the mode changed.

#include <eve/eve.h>
#include "eve-poc.h"

#include <chrono>
#include <ctime>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr uint8_t kSelect = 1;
constexpr uint8_t kByte = 2;
constexpr uint8_t kFrame = 3;
constexpr uint8_t kPowerOn = 4;
constexpr uint8_t kEnd = 5;
constexpr uint8_t kState = 6;
constexpr size_t kHeaderSize = 64;
constexpr uint32_t kOpaqueBlack = 0xFF000000u;

bool ReadFile(const char* path, std::vector<uint8_t>& out)
{
    FILE* f = std::fopen(path, "rb");
    if (!f)
        return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(static_cast<size_t>(size));
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

uint64_t HashPicture(const uint32_t* pixels, size_t count)
{
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < count; i++)
    {
        const uint32_t pixel = pixels[i];
        for (int b = 0; b < 4; b++)
        {
            hash ^= static_cast<uint8_t>(pixel >> (8 * b));
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

struct Output
{
    std::vector<uint32_t> pixels;
    uint16_t width = 0;
    uint16_t height = 0;
    bool draw = true;
    uint64_t drawEvery = 1;  // eve-accel 11: draw 1 frame in N (the others keep the old picture)
    uint64_t nextFrame = 1;  // the frame drawn after the next Configure

    /// Size for the chip's mode and hand it over; true when the size changed
    bool Configure(EveChip* chip)
    {
        EveTiming timing{};
        EveGetTiming(chip, &timing);
        const uint16_t w = timing.hsize ? timing.hsize : 1;
        const uint16_t h = timing.vsize ? timing.vsize : 1;
        const bool resized = w != width || h != height || pixels.empty();
        if (resized)
        {
            width = w;
            height = h;
            pixels.assign(static_cast<size_t>(w) * h, kOpaqueBlack);
        }
        EveSetOutput(chip, pixels.data(), w, w, h, draw && (nextFrame % drawEvery == 0) ? 1 : 0);
        return resized;
    }
};

} // namespace

int main(int argc, char** argv)
{
    const char* capturePath = nullptr;
    const char* romPath = nullptr;
    uint64_t frameLimit = 0;
    bool draw = true;
    bool quiet = false;
    bool hashes = true;
    uint64_t dumpFrom = 0, dumpTo = 0;
    const char* dumpDir = nullptr;
    int threads = 1;
    bool stats = false;
    bool costs = false;   // --costs: per-line cost (commands + fill clocks) against the line budget
    uint64_t drawEvery = 1; // --draw-every N: the chip draws 1 frame in N (eve-accel 11, cheap profile)
    const char* snapDir = nullptr;
    uint64_t snapFrom = 0, snapTo = 0;
    for (int i = 1; i < argc; i++)
    {
        if (!std::strcmp(argv[i], "--rom") && i + 1 < argc)
            romPath = argv[++i];
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc)
            frameLimit = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--no-draw"))
            draw = false;
        else if (!std::strcmp(argv[i], "--quiet"))
            quiet = true;
        else if (!std::strcmp(argv[i], "--no-hash"))
            hashes = false;
        else if (!std::strcmp(argv[i], "--dump") && i + 3 < argc)
        {
            dumpFrom = std::strtoull(argv[++i], nullptr, 10);
            dumpTo = std::strtoull(argv[++i], nullptr, 10);
            dumpDir = argv[++i];
        }
        else if (!std::strcmp(argv[i], "--threads") && i + 1 < argc)
            threads = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--stats"))
            stats = true;
        else if (!std::strcmp(argv[i], "--costs"))
            costs = true;
        else if (!std::strcmp(argv[i], "--draw-every") && i + 1 < argc)
            drawEvery = std::strtoull(argv[++i], nullptr, 10);
        else if (!std::strcmp(argv[i], "--snap") && i + 3 < argc)
        {
            snapDir = argv[++i];
            snapFrom = std::strtoull(argv[++i], nullptr, 10);
            snapTo = std::strtoull(argv[++i], nullptr, 10);
        }
        else
            capturePath = argv[i];
    }
    if (!capturePath)
    {
        std::fprintf(stderr, "usage: eve-replay <capture.evr> [--rom ft81x.rom] [--frames N] [--no-draw] [--no-hash] [--quiet]"
                             " [--dump FROM TO DIR]\n");
        return 2;
    }

    std::vector<uint8_t> file;
    if (!ReadFile(capturePath, file) || file.size() < kHeaderSize || std::memcmp(file.data(), "EVR1", 4) != 0)
    {
        std::fprintf(stderr, "eve-replay: '%s' is not an .evr capture\n", capturePath);
        return 2;
    }
    auto le = [&](size_t at, int bytes) {
        uint64_t v = 0;
        for (int b = 0; b < bytes; b++)
            v |= static_cast<uint64_t>(file[at + static_cast<size_t>(b)]) << (8 * b);
        return v;
    };
    const uint32_t headerSize = static_cast<uint32_t>(le(4, 4));
    const uint32_t externalClock = static_cast<uint32_t>(le(12, 4));
    const uint32_t romSize = static_cast<uint32_t>(le(36, 4));

    std::vector<uint8_t> rom;
    if (romPath && !ReadFile(romPath, rom))
    {
        std::fprintf(stderr, "eve-replay: cannot read the ROM '%s'\n", romPath);
        return 2;
    }
    if (romSize && rom.size() != romSize)
        std::fprintf(stderr, "eve-replay: warning: the capture ran with a %u-byte ROM, replaying with %zu bytes\n",
                     romSize, rom.size());

    EveConfig config{};
    config.structSize = sizeof(EveConfig);
    config.model = EVE_MODEL_FT812;
    config.externalClockHz = externalClock;
    config.romImage = rom.empty() ? nullptr : rom.data();
    config.romImageSize = rom.size();
    EveChip* chip = EveCreate(&config);
    if (!chip)
    {
        std::fprintf(stderr, "eve-replay: EveCreate failed: %s\n", EveLastError());
        return 2;
    }

    const int threadsInUse = EvePocSetThreads(chip, threads);
    if (snapDir)
        EvePocSetSnapshot(chip, snapDir, snapFrom, snapTo);

    Output output;
    output.draw = draw;
    output.drawEvery = drawEvery ? drawEvery : 1;
    output.Configure(chip);

    uint64_t clock = 0;
    uint64_t base = 0;  // chip clock of the stream's clock 0 (a state record sets it)
    uint64_t bytes = 0, selects = 0, frames = 0, drawnCompared = 0;
    uint64_t misoMismatches = 0, frameCountMismatches = 0, hashMismatches = 0;
    uint64_t firstBadClock = 0;
    bool ended = false;
    size_t pos = headerSize;
    // --costs: line cost / budget, in buckets of 1/8 up to 2 and over
    uint64_t costHist[18] = {}, costLines = 0, costOverflow = 0, costFrames = 0, costFramesOverflow = 0;
    uint64_t costCommandsSum = 0, costFillSum = 0;
    uint32_t costMax = 0, costBudget = 0, costMaxCommands = 0;
    const auto start = std::chrono::steady_clock::now();
    const std::clock_t cpuStart = std::clock();
    double advanceSeconds = 0;

    auto advanceTo = [&](uint64_t target) {
        const uint64_t now = EveTotalClocks(chip) - base;
        if (target > now)
        {
            const auto t0 = std::chrono::steady_clock::now();
            EveAdvance(chip, target - now);
            advanceSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        }
    };
    auto noteBad = [&]() {
        if (!firstBadClock)
            firstBadClock = clock;
    };

    while (pos < file.size() && !ended)
    {
        const uint8_t kind = file[pos++];
        uint64_t delta = 0;
        for (int shift = 0;; shift += 7)
        {
            const uint8_t b = file[pos++];
            delta |= static_cast<uint64_t>(b & 0x7F) << shift;
            if (!(b & 0x80))
                break;
        }
        clock += delta;
        if (kind != kState)
            advanceTo(clock);
        switch (kind)
        {
        case kSelect:
            EveSelect(chip, file[pos++]);
            selects++;
            break;
        case kByte:
        {
            const uint8_t mosi = file[pos];
            const uint8_t miso = file[pos + 1];
            pos += 2;
            if (EveExchange(chip, mosi) != miso)
            {
                if (!misoMismatches && !quiet)
                    std::fprintf(stderr, "first answer mismatch: byte %llu at clock %llu\n",
                                 static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(clock));
                misoMismatches++;
                noteBad();
            }
            bytes++;
            break;
        }
        case kFrame:
        {
            const uint64_t count = le(pos, 8);
            const bool drawn = file[pos + 8] != 0;
            const uint16_t w = static_cast<uint16_t>(le(pos + 9, 2));
            const uint16_t h = static_cast<uint16_t>(le(pos + 11, 2));
            const uint64_t hash = le(pos + 13, 8);
            pos += 21;
            frames++;
            if (EveCompletedFrames(chip) != count)
            {
                frameCountMismatches++;
                noteBad();
            }
            const bool drewThis = output.nextFrame % output.drawEvery == 0;
            if (hashes && draw && drawn && drewThis && w == output.width && h == output.height)
            {
                drawnCompared++;
                if (HashPicture(output.pixels.data(), output.pixels.size()) != hash)
                {
                    if (!hashMismatches && !quiet)
                        std::fprintf(stderr, "first picture mismatch: frame %llu at clock %llu\n",
                                     static_cast<unsigned long long>(count), static_cast<unsigned long long>(clock));
                    hashMismatches++;
                    noteBad();
                }
            }
            if (costs && draw && drawn)
            {
                ++costFrames;
                bool any = false;
                for (uint32_t line = 0; line < output.height; ++line)
                {
                    EveLineCost c{};
                    EveGetLineCost(chip, line, &c);
                    if (!c.valid || c.budget == 0)
                        continue;
                    ++costLines;
                    costCommandsSum += c.commands;
                    costFillSum += c.fillClocks;
                    const uint32_t bucket = static_cast<uint32_t>(8.0 * c.totalClocks / c.budget);
                    ++costHist[bucket < 17 ? bucket : 17];
                    if (c.overflow)
                    {
                        ++costOverflow;
                        any = true;
                    }
                    if (c.totalClocks > costMax)
                    {
                        costMax = c.totalClocks;
                        costBudget = c.budget;
                    }
                    costMaxCommands = c.commands > costMaxCommands ? c.commands : costMaxCommands;
                }
                costFramesOverflow += any;
            }
            if (dumpDir && frames >= dumpFrom && frames <= dumpTo)
            {
                EveCoproView view{};
                EveGetCoprocessor(chip, &view);
                std::printf("frame %llu: copro phase %d command %08X fault %08X '%s' rd %u wr %u dl %u\n",
                            static_cast<unsigned long long>(frames), static_cast<int>(view.phase), view.command,
                            view.faultCommand, view.faultReason, view.readPtr, view.writePtr, view.cmdDl);
                // Raw ARGB8888 of the frame and its display list words
                const std::string prefix = std::string(dumpDir) + "/frame-" + std::to_string(frames);
                if (FILE* f = std::fopen((prefix + "-" + std::to_string(output.width) + "x" + std::to_string(output.height) + ".argb").c_str(), "wb"))
                {
                    std::fwrite(output.pixels.data(), 4, output.pixels.size(), f);
                    std::fclose(f);
                }
                std::vector<uint32_t> dl(2048);
                const size_t words = EveGetDisplayList(chip, 1, dl.data(), dl.size());
                if (FILE* f = std::fopen((prefix + "-dl.txt").c_str(), "w"))
                {
                    for (size_t k = 0; k < words; ++k)
                    {
                        char text[128];
                        EveDisassemble(dl[k], text, sizeof text);
                        std::fprintf(f, "%4zu %08X %s\n", k, dl[k], text);
                    }
                    std::fclose(f);
                }
            }
            ++output.nextFrame;
            output.Configure(chip);
            if (frameLimit && frames >= frameLimit)
                ended = true;
            break;
        }
        case kPowerOn:
            EveReset(chip);
            output.Configure(chip);
            break;
        case kState:
        {
            const size_t stateSize = static_cast<size_t>(le(pos, 4));
            pos += 4;
            if (EveLoadState(chip, file.data() + pos, stateSize) != 0)
            {
                std::fprintf(stderr, "eve-replay: the state record does not load: %s\n", EveLastError());
                return 1;
            }
            pos += stateSize;
            const size_t regions = static_cast<size_t>(le(pos, 4));
            pos += 4;
            for (size_t r = 0; r < regions; r++)
            {
                const size_t nameLength = file[pos++];
                pos += nameLength;
                const size_t size = static_cast<size_t>(le(pos, 4));
                pos += 4;
                EveRegion region{};
                EveGetRegion(chip, r, &region);
                std::memcpy(region.base, file.data() + pos, size < region.size ? size : region.size);
                pos += size;
            }
            EveMemoryRestored(chip);
            base = EveTotalClocks(chip) - clock;
            output.Configure(chip);
            break;
        }
        case kEnd:
            ended = true;
            break;
        default:
            std::fprintf(stderr, "eve-replay: unknown record %u at offset %zu\n", kind, pos);
            return 1;
        }
    }

    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const double cpu = static_cast<double>(std::clock() - cpuStart) / CLOCKS_PER_SEC;
    const double chipSeconds = static_cast<double>(EveTotalClocks(chip) - base) /
                               static_cast<double>(EveSystemClockHz(chip) ? EveSystemClockHz(chip) : 1);
    std::printf("replayed: %llu bytes, %llu selects, %llu frames (%llu pictures compared)\n",
                static_cast<unsigned long long>(bytes), static_cast<unsigned long long>(selects),
                static_cast<unsigned long long>(frames), static_cast<unsigned long long>(drawnCompared));
    std::printf("checks:   %llu answer, %llu frame-count, %llu picture mismatches%s\n",
                static_cast<unsigned long long>(misoMismatches), static_cast<unsigned long long>(frameCountMismatches),
                static_cast<unsigned long long>(hashMismatches), firstBadClock ? " (see first above)" : "");
    std::printf("time:     %.3f s wall (%.3f s in EveAdvance) for %.3f s of chip time: %.2fx real time, %.1f frames/s\n",
                wall, advanceSeconds, chipSeconds, wall > 0 ? chipSeconds / wall : 0.0,
                wall > 0 ? static_cast<double>(frames) / wall : 0.0);
    std::printf("cpu:      %.3f s of this process: %.2fx real time (the figure to compare on a loaded machine)\n", cpu,
                cpu > 0 ? chipSeconds / cpu : 0.0);
    std::printf("variant:  %s, %d thread(s)\n", EvePocVariant(), threadsInUse);
    if (costs && costLines)
    {
        std::printf("line cost: %llu lines of %llu frames; mean %.0f commands + %.0f fill clocks; worst line %u clocks "
                    "(budget %u), most commands on a line %u\n",
                    (unsigned long long)costLines, (unsigned long long)costFrames,
                    (double)costCommandsSum / (double)costLines, (double)costFillSum / (double)costLines, costMax,
                    costBudget, costMaxCommands);
        std::printf("overflowing lines %llu, frames with an overflowing line %llu\n", (unsigned long long)costOverflow,
                    (unsigned long long)costFramesOverflow);
        std::printf("cost / budget histogram:");
        for (int b = 0; b < 18; ++b)
            if (costHist[b])
                std::printf(" [%.3f-%s): %llu", b / 8.0, b < 17 ? std::to_string((b + 1) / 8.0).substr(0, 5).c_str() : "inf",
                            (unsigned long long)costHist[b]);
        std::printf("\n");
    }
    if (stats)
        EvePocPrintStats(chip, stdout);
    EveDestroy(chip);
    return (misoMismatches || frameCountMismatches || hashMismatches) ? 1 : 0;
}
