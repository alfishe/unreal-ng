// eve-replay - replays an .evr bus capture into the library and checks it.
//
// The capture (unreal-ng docs/inprogress/2026-10-01-tsconf-vdac2/vdac2-test-corpus.md §4)
// holds every chip select change and every byte on the FT812's bus with the chip's answer,
// stamped with system clocks, and a record at each frame end with a hash of the picture.
// Replaying it drives the chip alone: every answer byte and every frame count must match,
// and every drawn frame's hash. The run also measures how fast the library is against the
// chip's real time - the reference workload for performance work.
//
//   eve-replay <capture.evr> [--rom <ft81x.rom>] [--frames N] [--threads N] [--hashes FILE] [--no-draw] [--no-hash] [--all-mismatches] [--quiet] [--profile FROM TO]
//
//   --no-draw  the chip draws nothing (timing only); --no-hash  draws, but skips the picture
//   check (its hashing is the harness's cost, not the library's)
//
//   eve-replay-profile (the same tool on a library built with EVE_PROFILE) also takes
//   --profile FROM TO: counts what the frames FROM..TO (capture frame records, 1-based)
//   cost - bitmap spans by format, filter, path, matrix and pipeline, other primitives,
//   display list lines - and prints the table at the end
//
// The output buffer follows what the capturing host did: HSIZE x VSIZE ARGB8888, filled
// with opaque black when (re)sized, resized after a frame end when the mode changed.

#include <eve/eve.h>

#ifdef EVE_PROFILE
#include <algorithm>
#include "eve-profile.h"
#endif

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

#ifdef EVE_PROFILE
const char* FormatName(uint8_t f)
{
    static const char* names[] = {"ARGB1555", "L1", "L4", "L8", "RGB332", "ARGB2", "ARGB4", "RGB565", "PALETTED",
                                  "TEXT8X8", "TEXTVGA", "BARGRAPH", "?12", "?13", "PALETTED565", "PALETTED4444",
                                  "PALETTED8", "L2"};
    return f < sizeof(names) / sizeof(names[0]) ? names[f] : "?";
}

const char* PrimitiveName(uint8_t p)
{
    static const char* names[] = {"CLEAR?", "BITMAPS", "POINTS", "LINES", "LINE_STRIP", "EDGE_STRIP_R", "EDGE_STRIP_L",
                                  "EDGE_STRIP_A", "EDGE_STRIP_B", "RECTS"};
    return p < sizeof(names) / sizeof(names[0]) ? names[p] : "?";
}

void PrintProfile(uint64_t from, uint64_t to, uint64_t frames)
{
    using namespace EveLib;
    const ProfileCounters& c = Profile();
    const double lineNs = static_cast<double>(c.lines.nanos);
    std::printf("\nprofile: frames %llu..%llu (%llu frames), %llu lines drawn, %.1f ms drawing (%.2f ms per frame), "
                "%.0f commands per line\n",
                static_cast<unsigned long long>(from), static_cast<unsigned long long>(to),
                static_cast<unsigned long long>(frames), static_cast<unsigned long long>(c.lines.calls), lineNs / 1e6,
                frames ? lineNs / 1e6 / static_cast<double>(frames) : 0.0,
                c.lines.calls ? static_cast<double>(c.lines.pixels) / static_cast<double>(c.lines.calls) : 0.0);
    std::vector<std::pair<ProfileBitmapKey, ProfileCell>> rows(c.bitmaps.begin(), c.bitmaps.end());
    std::sort(rows.begin(), rows.end(), [](const auto& x, const auto& y) { return x.second.nanos > y.second.nanos; });
    std::printf("%-12s %-8s %-5s %-8s %-8s %10s %12s %8s %7s\n", "format", "filter", "path", "matrix", "pipeline",
                "spans", "pixels", "ns/px", "share");
    uint64_t bitmapNs = 0;
    for (const auto& [k, cell] : rows)
    {
        bitmapNs += cell.nanos;
        std::printf("%-12s %-8s %-5s %-8s %-8s %10llu %12llu %8.2f %6.1f%%\n", FormatName(k.format),
                    k.filter ? "BILINEAR" : "NEAREST", k.fast ? "fast" : "slow",
                    k.matrix == ProfileMatrix::Identity ? "identity" : k.matrix == ProfileMatrix::Scaled ? "scaled" : "rotated",
                    k.defaultPipeline ? "default" : "other", static_cast<unsigned long long>(cell.calls),
                    static_cast<unsigned long long>(cell.pixels),
                    cell.pixels ? static_cast<double>(cell.nanos) / static_cast<double>(cell.pixels) : 0.0,
                    lineNs > 0 ? 100.0 * static_cast<double>(cell.nanos) / lineNs : 0.0);
    }
    uint64_t primitiveNs = 0;
    for (const auto& [p, cell] : c.primitives)
    {
        primitiveNs += cell.nanos;
        std::printf("%-12s %-8s %-5s %-8s %-8s %10llu %12s %8s %6.1f%%\n", PrimitiveName(p), "-", "-", "-", "-",
                    static_cast<unsigned long long>(cell.calls), "-", "-",
                    lineNs > 0 ? 100.0 * static_cast<double>(cell.nanos) / lineNs : 0.0);
    }
    {
        std::vector<std::pair<uint64_t, int>> ops;
        for (int op = 0; op < 256; ++op)
            if (c.opcodes[op])
                ops.push_back({c.opcodes[op], op});
        std::sort(ops.rbegin(), ops.rend());
        std::printf("display list words by opcode (per walked line):");
        for (size_t k = 0; k < ops.size() && k < 14; ++k)
            std::printf(" %02X:%.1f", ops[k].second,
                        c.lines.calls > c.planReplays ? static_cast<double>(ops[k].first) / static_cast<double>(c.lines.calls - c.planReplays) : 0.0);
        std::printf("\n");
        const double ln = c.lines.calls ? static_cast<double>(c.lines.calls) : 1.0;
        std::printf("fast span tails: %.1f %% of their pixels modulated, %.1f %% SIMD blend; fast NEAREST rows: decode %.0f ms, tail %.0f ms\n",
                    c.finishPixels ? 100.0 * static_cast<double>(c.modulatePixels) / static_cast<double>(c.finishPixels) : 0.0,
                    c.finishPixels ? 100.0 * static_cast<double>(c.simdBlendPixels) / static_cast<double>(c.finishPixels) : 0.0,
                    static_cast<double>(c.decodeNanos) / 1e6, static_cast<double>(c.finishNanos) / 1e6);
        {
            std::vector<std::pair<uint64_t, int64_t>> as;
            for (const auto& [a, px] : c.scaleAPixels)
                as.push_back({px, a});
            std::sort(as.rbegin(), as.rend());
            std::printf("axis-aligned NEAREST pixels by transform A:");
            for (size_t k = 0; k < as.size() && k < 8; ++k)
                std::printf(" %lld:%.0fM", static_cast<long long>(as[k].second), static_cast<double>(as[k].first) / 1e6);
            std::printf("\n");
        }
        std::printf("lines replayed from a recorded walk: %.1f %%, lines kept (not drawn again): %llu, by their own steps: %llu\n",
                    100.0 * static_cast<double>(c.planReplays) / ln, static_cast<unsigned long long>(c.linesKept),
                    static_cast<unsigned long long>(c.linesKeptBySteps));
        // A replayed line skips the bitmaps that miss it before DrawBitmap
        std::printf("bitmap vertices reaching DrawBitmap per drawn line: %.1f, missing the line: %.1f, outside the scissor: %.1f\n",
                    static_cast<double>(c.bitmapVertices) / ln, static_cast<double>(c.bitmapMissY) / ln,
                    static_cast<double>(c.bitmapMissX) / ln);
    }
    for (const auto& [pipeline, pixels] : c.generalBlends)
        std::printf("general blend: %-40s %llu pixels\n", pipeline.c_str(), static_cast<unsigned long long>(pixels));
    for (const auto& [why, count] : c.fastRejects)
        std::printf("fast path declined: %-36s %llu spans\n", why.c_str(), static_cast<unsigned long long>(count));
    const double rest = lineNs - static_cast<double>(bitmapNs + primitiveNs);
    std::printf("%-12s %70s %6.1f%%\n", "rest", "(display list walk, clear, line output)", lineNs > 0 ? 100.0 * rest / lineNs : 0.0);
}
#endif

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

    /// Size for the chip's mode and hand it over; true when the size changed
    bool Configure(EveChip* chip)
    {
        EveTiming timing{};
        EveGetTiming(chip, &timing);
        const uint16_t w = timing.hsize ? timing.hsize : 1;
        const uint16_t h = timing.vsize ? timing.vsize : 1;
        const bool resized = w != width || h != height || pixels.empty();
        // EveSetOutput first catches up the lines the chip owes into the buffer it has: the old one lives until then
        std::vector<uint32_t> old;
        if (resized)
        {
            width = w;
            height = h;
            old.swap(pixels);
            pixels.assign(static_cast<size_t>(w) * h, kOpaqueBlack);
        }
        EveSetOutput(chip, pixels.data(), w, w, h, draw ? 1 : 0);
        return resized;
    }
};

} // namespace

int main(int argc, char** argv)
{
#ifdef EVE_PROFILE
    uint64_t profileFrom = 1, profileTo = UINT64_MAX;
    bool profileReported = false;
    EveLib::ProfileReset();
#endif
    const char* capturePath = nullptr;
    const char* romPath = nullptr;
    uint64_t frameLimit = 0;
    bool draw = true;
    bool quiet = false;
    uint32_t threads = 1;       // --threads N: lines drawn by N threads (EveSetDrawThreads)
    const char* hashesPath = nullptr; // --hashes FILE: every drawn frame's picture hash, one per line
    bool allMismatches = false; // --all-mismatches: every picture mismatch, not only the first
    bool hashes = true;
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
#ifdef EVE_PROFILE
        else if (!std::strcmp(argv[i], "--profile") && i + 2 < argc)
        {
            profileFrom = std::strtoull(argv[++i], nullptr, 10);
            profileTo = std::strtoull(argv[++i], nullptr, 10);
        }
#endif
        else if (!std::strcmp(argv[i], "--all-mismatches"))
            allMismatches = true;
        else if (!std::strcmp(argv[i], "--hashes") && i + 1 < argc)
            hashesPath = argv[++i];
        else if (!std::strcmp(argv[i], "--threads") && i + 1 < argc)
            threads = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        else if (!std::strcmp(argv[i], "--no-hash"))
            hashes = false;
        else
            capturePath = argv[i];
    }
    if (!capturePath)
    {
        std::fprintf(stderr, "usage: eve-replay <capture.evr> [--rom ft81x.rom] [--frames N] [--threads N] [--hashes FILE] [--no-draw] [--no-hash] [--all-mismatches] [--quiet] [--profile FROM TO]\n");
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
    const uint32_t chipModel = static_cast<uint32_t>(le(8, 4));  // 812: FT812, 4: the VDAC4 board's FT812
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
    config.model = chipModel == 4 ? EVE_MODEL_VDAC4 : EVE_MODEL_FT812;
    config.externalClockHz = externalClock;
    config.romImage = rom.empty() ? nullptr : rom.data();
    config.romImageSize = rom.size();
    EveChip* chip = EveCreate(&config);
    if (!chip)
    {
        std::fprintf(stderr, "eve-replay: EveCreate failed: %s\n", EveLastError());
        return 2;
    }

    EveSetDrawThreads(chip, threads);
    FILE* hashesFile = hashesPath ? std::fopen(hashesPath, "w") : nullptr;
    Output output;
    output.draw = draw;
    output.Configure(chip);

    uint64_t clock = 0;
    uint64_t base = 0;  // chip clock of the stream's clock 0 (a state record sets it)
    uint64_t bytes = 0, selects = 0, frames = 0, drawnCompared = 0;
    uint64_t misoMismatches = 0, frameCountMismatches = 0, hashMismatches = 0;
    uint64_t firstBadClock = 0;
    bool ended = false;
    size_t pos = headerSize;
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
                if (!frameCountMismatches && !quiet)
                    std::fprintf(stderr, "first frame-count mismatch: the capture's frame %llu, the replay's %llu, at clock %llu\n",
                                 static_cast<unsigned long long>(count),
                                 static_cast<unsigned long long>(EveCompletedFrames(chip)),
                                 static_cast<unsigned long long>(clock));
                frameCountMismatches++;
                noteBad();
            }
            if (hashesFile && draw)
                std::fprintf(hashesFile, "%llu %016llx\n", static_cast<unsigned long long>(frames),
                             static_cast<unsigned long long>(HashPicture(output.pixels.data(), output.pixels.size())));
            if (hashes && draw && drawn && w == output.width && h == output.height)
            {
                drawnCompared++;
                if (HashPicture(output.pixels.data(), output.pixels.size()) != hash)
                {
                    if (allMismatches)
                        std::fprintf(stderr, "picture mismatch: frame %llu (#%llu of the capture) at clock %llu\n",
                                     static_cast<unsigned long long>(count), static_cast<unsigned long long>(frames),
                                     static_cast<unsigned long long>(clock));
                    if (!hashMismatches && !quiet)
                        std::fprintf(stderr, "first picture mismatch: frame %llu at clock %llu\n",
                                     static_cast<unsigned long long>(count), static_cast<unsigned long long>(clock));
                    hashMismatches++;
                    noteBad();
                }
            }
            output.Configure(chip);
#ifdef EVE_PROFILE
            if (frames + 1 == profileFrom)
                EveLib::ProfileReset();  // the next frame is the window's first
            if (frames == profileTo && !profileReported)
            {
                PrintProfile(profileFrom, profileTo, profileTo - profileFrom + 1);
                profileReported = true;
            }
#endif
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
#ifdef EVE_PROFILE
    if (!profileReported && frames >= profileFrom)
        PrintProfile(profileFrom, frames, frames - profileFrom + 1);
#endif
    if (hashesFile)
        std::fclose(hashesFile);
    EveDestroy(chip);
    return (misoMismatches || frameCountMismatches || hashMismatches) ? 1 : 0;
}
