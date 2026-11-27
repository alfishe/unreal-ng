// eve-emu drawing on its renderer thread (EveSetAsyncDrawing): two chips get the same bus traffic, one draws on
// the caller's thread, the other on the renderer. Every frame, REG_TAG read, the saved state, the frame metrics
// and the line costs must be the same - with RAM_G and the display list changing in the middle of frames, a
// frame passed without drawing and the renderer switched off and on again on the way.

#include "eve/eve.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace
{

constexpr uint32_t kCrystalHz = 12000000;
constexpr uint32_t kRegHcycle = 0x30202C;
constexpr uint32_t kRegHoffset = 0x302030;
constexpr uint32_t kRegHsize = 0x302034;
constexpr uint32_t kRegHsync0 = 0x302038;
constexpr uint32_t kRegHsync1 = 0x30203C;
constexpr uint32_t kRegVcycle = 0x302040;
constexpr uint32_t kRegVoffset = 0x302044;
constexpr uint32_t kRegVsize = 0x302048;
constexpr uint32_t kRegVsync0 = 0x30204C;
constexpr uint32_t kRegVsync1 = 0x302050;
constexpr uint32_t kRegDlswap = 0x302054;
constexpr uint32_t kRegPclk = 0x302070;
constexpr uint32_t kRegTagX = 0x302074;
constexpr uint32_t kRegTagY = 0x302078;
constexpr uint32_t kRegTag = 0x30207C;
constexpr uint32_t kRamDl = 0x300000;
constexpr uint32_t kWidth = 64;
constexpr uint32_t kHeight = 40;
constexpr uint32_t kBitmapBytes = kWidth * kHeight * 2; // RGB565

uint64_t Hash(const uint32_t* pixels, size_t count)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < count; ++i)
        h = (h ^ pixels[i]) * 0x100000001b3ull;
    return h;
}

class Chip
{
public:
    Chip()
    {
        EveConfig config{};
        config.structSize = sizeof(EveConfig);
        config.model = EVE_MODEL_FT812;
        config.externalClockHz = kCrystalHz;
        chip = EveCreate(&config);
    }
    ~Chip()
    {
        if (chip)
            EveSetAsyncDrawing(chip, 0, nullptr, nullptr);
        EveDestroy(chip);
    }
    Chip(const Chip&) = delete;
    Chip& operator=(const Chip&) = delete;

    void HostCommand(uint8_t command)
    {
        EveSelect(chip, 1);
        EveExchange(chip, command);
        EveExchange(chip, 0);
        EveExchange(chip, 0);
        EveSelect(chip, 0);
    }

    void Write(uint32_t address, const uint8_t* data, size_t size)
    {
        EveSelect(chip, 1);
        EveExchange(chip, static_cast<uint8_t>(0x80 | ((address >> 16) & 0x3F)));
        EveExchange(chip, static_cast<uint8_t>(address >> 8));
        EveExchange(chip, static_cast<uint8_t>(address));
        for (size_t i = 0; i < size; ++i)
            EveExchange(chip, data[i]);
        EveSelect(chip, 0);
    }

    void Write32(uint32_t address, uint32_t value)
    {
        const uint8_t bytes[4] = {static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
                                  static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
        Write(address, bytes, sizeof(bytes));
    }

    uint32_t Read32(uint32_t address)
    {
        EveSelect(chip, 1);
        EveExchange(chip, static_cast<uint8_t>((address >> 16) & 0x3F));
        EveExchange(chip, static_cast<uint8_t>(address >> 8));
        EveExchange(chip, static_cast<uint8_t>(address));
        EveExchange(chip, 0); // dummy
        uint32_t value = 0;
        for (uint32_t i = 0; i < 4; ++i)
            value |= static_cast<uint32_t>(EveExchange(chip, 0)) << (8 * i);
        EveSelect(chip, 0);
        return value;
    }

    EveChip* chip = nullptr;
    std::vector<uint32_t> frame = std::vector<uint32_t>(kWidth * kHeight, 0);
};

// What the renderer presented (on its thread)
struct Presented
{
    std::mutex lock;
    std::vector<uint64_t> hashes;
    std::thread::id thread;
    const Chip* chip = nullptr;
};

void OnFrame(void* user, uint64_t /*frame*/, int drawn)
{
    auto* p = static_cast<Presented*>(user);
    std::lock_guard<std::mutex> guard(p->lock);
    p->thread = std::this_thread::get_id();
    if (drawn)
        p->hashes.push_back(Hash(p->chip->frame.data(), p->chip->frame.size()));
}

// The same deterministic traffic to both chips
class Traffic
{
public:
    explicit Traffic(std::vector<Chip*> chips) : _chips(std::move(chips)) {}

    void Write32(uint32_t address, uint32_t value)
    {
        for (Chip* c : _chips)
            c->Write32(address, value);
    }
    void Write(uint32_t address, const std::vector<uint8_t>& data)
    {
        for (Chip* c : _chips)
            c->Write(address, data.data(), data.size());
    }
    uint32_t Next()
    {
        _seed = _seed * 1664525u + 1013904223u;
        return _seed >> 8;
    }

    void DisplayList(uint32_t clearColor, uint32_t tag)
    {
        const uint32_t words[] = {
            0x02000000u | clearColor,                                  // CLEAR_COLOR_RGB
            0x26000007u,                                               // CLEAR(1, 1, 1)
            0x03000000u | tag,                                         // TAG
            0x01000000u,                                               // BITMAP_SOURCE(0)
            0x07000000u | (7u << 19) | ((kWidth * 2) << 9) | kHeight,  // BITMAP_LAYOUT(RGB565)
            0x08000000u | (kWidth << 9) | kHeight,                     // BITMAP_SIZE(NEAREST, BORDER, BORDER)
            0x1F000001u,                                               // BEGIN(BITMAPS)
            0x80000000u | (4u << 21) | (2u << 12),                     // VERTEX2II(4, 2, 0, 0)
            0x21000000u,                                               // END
            0x00000000u,                                               // DISPLAY
        };
        for (uint32_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i)
            Write32(kRamDl + 4 * i, words[i]);
        Write32(kRegDlswap, 2); // at the frame end
    }

private:
    std::vector<Chip*> _chips;
    uint32_t _seed = 12345;
};

} // namespace

TEST(EveAsyncDrawing_Test, TheRendererDrawsWhatTheChipWouldHaveDrawn)
{
    Chip sync;
    Chip async;
    ASSERT_NE(sync.chip, nullptr);
    ASSERT_NE(async.chip, nullptr);
    Presented presented;
    presented.chip = &async;
    Traffic bus({&sync, &async});

    for (Chip* c : {&sync, &async})
    {
        c->HostCommand(0x44); // CLKEXT
        c->HostCommand(0x00); // ACTIVE
        EveAdvance(c->chip, 1000000);
    }
    const std::pair<uint32_t, uint32_t> timing[] = {
        {kRegHcycle, 100}, {kRegHoffset, 10}, {kRegHsize, kWidth}, {kRegHsync0, 0}, {kRegHsync1, 5},
        {kRegVcycle, 60},  {kRegVoffset, 4},  {kRegVsize, kHeight}, {kRegVsync0, 0}, {kRegVsync1, 2},
        {kRegTagX, 10},    {kRegTagY, 20},    {kRegPclk, 1}};
    for (const auto& [address, value] : timing)
        bus.Write32(address, value);
    std::vector<uint8_t> bitmap(kBitmapBytes);
    for (uint8_t& b : bitmap)
        b = static_cast<uint8_t>(bus.Next());
    bus.Write(0, bitmap);
    bus.DisplayList(0x0A141E, 5);
    for (Chip* c : {&sync, &async})
        EveSetOutput(c->chip, c->frame.data(), kWidth, kWidth, kHeight, 1);
    ASSERT_EQ(EveSetAsyncDrawing(async.chip, 1, &OnFrame, &presented), 0) << EveLastError();
    ASSERT_EQ(EveAsyncDrawing(async.chip), 1);

    std::vector<uint64_t> syncHashes;
    bool drawing = true;
    uint64_t frames = EveCompletedFrames(sync.chip);
    constexpr uint32_t kFrames = 24;
    while (EveCompletedFrames(sync.chip) < frames + kFrames)
    {
        // A step of up to a third of a frame, never past a chip event (the host's pattern)
        const uint64_t step = std::max<uint64_t>(1, std::min<uint64_t>(bus.Next() % 2000, EveClocksToNextEvent(sync.chip)));
        const uint64_t before = EveCompletedFrames(sync.chip);
        EveAdvance(sync.chip, step);
        EveAdvance(async.chip, step);
        if (EveCompletedFrames(sync.chip) != before && drawing)
            syncHashes.push_back(Hash(sync.frame.data(), sync.frame.size()));
        const uint64_t frame = EveCompletedFrames(sync.chip) - frames;

        // Mid-frame traffic: bitmap bytes, a new list every few frames, REG_TAG reads
        const uint32_t what = bus.Next() % 16;
        if (what < 6)
        {
            std::vector<uint8_t> bytes(1 + bus.Next() % 64);
            for (uint8_t& b : bytes)
                b = static_cast<uint8_t>(bus.Next());
            bus.Write(bus.Next() % (kBitmapBytes - 64), bytes);
        }
        else if (what == 6)
            bus.DisplayList(bus.Next() & 0xFFFFFF, 1 + bus.Next() % 200);
        else if (what == 7)
        {
            ASSERT_EQ(async.Read32(kRegTag), sync.Read32(kRegTag)) << "frame " << frame;
        }

        // A frame passed without drawing (turbo), then the renderer off and on again
        if (frame == 8 && drawing)
        {
            drawing = false;
            for (Chip* c : {&sync, &async})
                EveSetOutput(c->chip, c->frame.data(), kWidth, kWidth, kHeight, 0);
        }
        if (frame == 10 && !drawing)
        {
            drawing = true;
            for (Chip* c : {&sync, &async})
                EveSetOutput(c->chip, c->frame.data(), kWidth, kWidth, kHeight, 1);
        }
        if (frame == 14 && EveAsyncDrawing(async.chip))
        {
            EveDrawingBarrier(async.chip);
            const size_t presentedSoFar = presented.hashes.size();
            ASSERT_EQ(EveSetAsyncDrawing(async.chip, 0, nullptr, nullptr), 0);
            // Off: the chip draws again; its frames go to the list directly
            presented.chip = nullptr;
            ASSERT_EQ(presentedSoFar, syncHashes.size());
        }
        if (!EveAsyncDrawing(async.chip) && frame >= 15 && frame <= 17 && EveCompletedFrames(sync.chip) != before)
        {
            std::lock_guard<std::mutex> guard(presented.lock);
            presented.hashes.push_back(Hash(async.frame.data(), async.frame.size()));
        }
        if (frame == 17 && !EveAsyncDrawing(async.chip))
        {
            presented.chip = &async;
            ASSERT_EQ(EveSetAsyncDrawing(async.chip, 1, &OnFrame, &presented), 0);
        }
    }
    EveDrawingBarrier(async.chip);

    // Every frame the same, drawn on the renderer thread
    {
        std::lock_guard<std::mutex> guard(presented.lock);
        ASSERT_GE(syncHashes.size(), kFrames - 3u);
        EXPECT_EQ(presented.hashes, syncHashes);
        EXPECT_NE(presented.thread, std::this_thread::get_id());
        EXPECT_NE(presented.thread, std::thread::id());
    }
    // The saved state (handles, context, metrics: the renderer's), the metrics and the line costs
    std::vector<uint8_t> a(EveStateSize(sync.chip)), b(EveStateSize(async.chip));
    EveSaveState(sync.chip, a.data());
    EveSaveState(async.chip, b.data());
    EXPECT_EQ(a, b);
    EveFrameMetrics ma{}, mb{};
    std::vector<uint16_t> la(kHeight), lb(kHeight);
    ASSERT_EQ(EveGetFrameMetrics(sync.chip, &ma, la.data(), la.size()),
              EveGetFrameMetrics(async.chip, &mb, lb.data(), lb.size()));
    EXPECT_EQ(std::memcmp(&ma, &mb, sizeof(ma)), 0);
    EXPECT_EQ(la, lb);
    EXPECT_EQ(ma.valid, 1u);
    for (uint32_t line = 0; line < kHeight; ++line)
    {
        EveLineCost ca{}, cb{};
        EveGetLineCost(sync.chip, line, &ca);
        EveGetLineCost(async.chip, line, &cb);
        EXPECT_EQ(std::memcmp(&ca, &cb, sizeof(ca)), 0) << "line " << line;
    }
    EXPECT_EQ(sync.frame, async.frame);
}

// EveFramesBarrier at every frame end (the host's machine frame end): the frames finished so far are presented,
// and REG_TAG reads as the drawing on the caller's thread leaves it at the barrier, the lines of the next frame
// still drawing or not
TEST(EveAsyncDrawing_Test, FramesBarrierPresentsTheFramesAndKeepsTheTagOfItsPoint)
{
    Chip sync;
    Chip async;
    ASSERT_NE(sync.chip, nullptr);
    ASSERT_NE(async.chip, nullptr);
    Presented presented;
    presented.chip = &async;
    Traffic bus({&sync, &async});

    for (Chip* c : {&sync, &async})
    {
        c->HostCommand(0x44); // CLKEXT
        c->HostCommand(0x00); // ACTIVE
        EveAdvance(c->chip, 1000000);
    }
    const std::pair<uint32_t, uint32_t> timing[] = {
        {kRegHcycle, 100}, {kRegHoffset, 10}, {kRegHsize, kWidth}, {kRegHsync0, 0}, {kRegHsync1, 5},
        {kRegVcycle, 60},  {kRegVoffset, 4},  {kRegVsize, kHeight}, {kRegVsync0, 0}, {kRegVsync1, 2},
        {kRegTagX, 10},    {kRegTagY, 20},    {kRegPclk, 1}};
    for (const auto& [address, value] : timing)
        bus.Write32(address, value);
    std::vector<uint8_t> bitmap(kBitmapBytes);
    for (uint8_t& b : bitmap)
        b = static_cast<uint8_t>(bus.Next());
    bus.Write(0, bitmap);
    bus.DisplayList(0x0A141E, 5);
    for (Chip* c : {&sync, &async})
        EveSetOutput(c->chip, c->frame.data(), kWidth, kWidth, kHeight, 1);
    ASSERT_EQ(EveSetAsyncDrawing(async.chip, 1, &OnFrame, &presented), 0) << EveLastError();

    auto peekTag = [](const Chip& c) { return EvePeek(c.chip, kRegTag); };
    std::vector<uint64_t> syncHashes;
    const uint64_t frames = EveCompletedFrames(sync.chip);
    constexpr uint32_t kFrames = 16;
    // Barriers at "machine frames" of 5000 clocks, not locked to the chip's 6000-clock frames: some fall in the
    // middle of the chip's lines
    uint64_t sinceBarrier = 0;
    while (EveCompletedFrames(sync.chip) < frames + kFrames)
    {
        const uint64_t step = std::max<uint64_t>(1, std::min<uint64_t>(bus.Next() % 1500, EveClocksToNextEvent(sync.chip)));
        const uint64_t before = EveCompletedFrames(sync.chip);
        EveAdvance(sync.chip, step);
        EveAdvance(async.chip, step);
        sinceBarrier += step;
        if (EveCompletedFrames(sync.chip) != before)
            syncHashes.push_back(Hash(sync.frame.data(), sync.frame.size()));
        const uint64_t frame = EveCompletedFrames(sync.chip) - frames;
        if (bus.Next() % 4 == 0)
            bus.DisplayList(bus.Next() & 0xFFFFFF, 1 + bus.Next() % 200);  // the tag changes from frame to frame
        if (sinceBarrier >= 5000)
        {
            sinceBarrier = 0;
            EveFramesBarrier(async.chip);
            {
                std::lock_guard<std::mutex> guard(presented.lock);
                ASSERT_EQ(presented.hashes, syncHashes) << "frame " << frame;
            }
            // Read now, or after more lines went to the renderer: the tag of the barrier's point
            if (bus.Next() % 2 == 0)
            {
                ASSERT_EQ(peekTag(async), peekTag(sync)) << "frame " << frame;
            }
        }
        else if (bus.Next() % 8 == 0)
        {
            ASSERT_EQ(async.Read32(kRegTag), sync.Read32(kRegTag)) << "frame " << frame;
        }
    }
    EveDrawingBarrier(async.chip);
    EXPECT_EQ(peekTag(async), peekTag(sync));
    std::lock_guard<std::mutex> guard(presented.lock);
    EXPECT_EQ(presented.hashes, syncHashes);
}

TEST(EveAsyncDrawing_Test, FramesBarrierToLetsTheRendererLagByOneHostFrame)
{
    // The host's frame-end pattern (EveHostCard::OnFrameEnd): wait for the frames queued by the previous barrier
    // only. Every frame is still presented in order, at most one host frame late, and REG_TAG reads as the chip's
    Chip sync;
    Chip async;
    ASSERT_NE(sync.chip, nullptr);
    ASSERT_NE(async.chip, nullptr);
    Presented presented;
    presented.chip = &async;
    Traffic bus({&sync, &async});

    for (Chip* c : {&sync, &async})
    {
        c->HostCommand(0x44); // CLKEXT
        c->HostCommand(0x00); // ACTIVE
        EveAdvance(c->chip, 1000000);
    }
    const std::pair<uint32_t, uint32_t> timing[] = {
        {kRegHcycle, 100}, {kRegHoffset, 10}, {kRegHsize, kWidth}, {kRegHsync0, 0}, {kRegHsync1, 5},
        {kRegVcycle, 60},  {kRegVoffset, 4},  {kRegVsize, kHeight}, {kRegVsync0, 0}, {kRegVsync1, 2},
        {kRegTagX, 10},    {kRegTagY, 20},    {kRegPclk, 1}};
    for (const auto& [address, value] : timing)
        bus.Write32(address, value);
    std::vector<uint8_t> bitmap(kBitmapBytes);
    for (uint8_t& b : bitmap)
        b = static_cast<uint8_t>(bus.Next());
    bus.Write(0, bitmap);
    bus.DisplayList(0x0A141E, 5);
    for (Chip* c : {&sync, &async})
        EveSetOutput(c->chip, c->frame.data(), kWidth, kWidth, kHeight, 1);
    ASSERT_EQ(EveQueuedFrameEnds(async.chip), 0u);  // not drawing asynchronously yet
    ASSERT_EQ(EveSetAsyncDrawing(async.chip, 1, &OnFrame, &presented), 0) << EveLastError();

    std::vector<uint64_t> syncHashes;
    const uint64_t frames = EveCompletedFrames(sync.chip);
    constexpr uint32_t kFrames = 16;
    uint64_t sinceBarrier = 0;
    uint64_t lastQueued = 0;
    size_t hashesAtLastBarrier = 0;
    while (EveCompletedFrames(sync.chip) < frames + kFrames)
    {
        const uint64_t step = std::max<uint64_t>(1, std::min<uint64_t>(bus.Next() % 1500, EveClocksToNextEvent(sync.chip)));
        const uint64_t before = EveCompletedFrames(sync.chip);
        EveAdvance(sync.chip, step);
        EveAdvance(async.chip, step);
        sinceBarrier += step;
        if (EveCompletedFrames(sync.chip) != before)
            syncHashes.push_back(Hash(sync.frame.data(), sync.frame.size()));
        const uint64_t frame = EveCompletedFrames(sync.chip) - frames;
        if (bus.Next() % 4 == 0)
            bus.DisplayList(bus.Next() & 0xFFFFFF, 1 + bus.Next() % 200);
        if (sinceBarrier >= 5000)
        {
            sinceBarrier = 0;
            const uint64_t queued = EveQueuedFrameEnds(async.chip);
            EveFramesBarrierTo(async.chip, lastQueued);
            {
                std::lock_guard<std::mutex> guard(presented.lock);
                // At least what the previous barrier saw queued; never more than drawn by the synchronous chip,
                // and the same pictures in the same order
                ASSERT_GE(presented.hashes.size(), hashesAtLastBarrier) << "frame " << frame;
                ASSERT_LE(presented.hashes.size(), syncHashes.size()) << "frame " << frame;
                ASSERT_TRUE(std::equal(presented.hashes.begin(), presented.hashes.end(), syncHashes.begin()))
                    << "frame " << frame;
            }
            hashesAtLastBarrier = syncHashes.size();
            lastQueued = queued;
            ASSERT_EQ(EvePeek(async.chip, kRegTag), EvePeek(sync.chip, kRegTag)) << "frame " << frame;
        }
    }
    EveFramesBarrier(async.chip);
    std::lock_guard<std::mutex> guard(presented.lock);
    EXPECT_EQ(presented.hashes, syncHashes);
}
