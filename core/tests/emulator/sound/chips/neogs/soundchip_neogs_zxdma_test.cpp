// NeoGS ZX-DMA on an emulated Pentagon (neogs-zxdma-design.md §8.2-§8.3).
//
// A program on the card's Z80 sets the ZX module up; a program on the
// Pentagon's Z80 moves blocks with LDIR through #0000-#3FFF. Every case runs
// in fast and in debug mode, and must give the same result in both. The
// model's arithmetic (waits, dropped bytes, bursts) is in neogszxdma_test.cpp.

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"

namespace
{
constexpr uint32_t kCardBase = 0x012000; // card RAM address of the transfers
constexpr uint16_t kHostBuffer = 0x9000;
constexpr uint16_t kHostCode = 0x8000;
constexpr uint16_t kHostIdle = 0x8100;

/// Card program: select the ZX module, set the address, then either start at
/// once or wait for any host command first (the documented handshake flow)
std::vector<uint8_t> cardProgram(bool waitForCommand)
{
    std::vector<uint8_t> p = {
        0xF3,                                           // DI
        0x3E, 0x01, 0xD3, 0x1B,                         // DMA_MOD = 1 (ZX)
        0x3E, uint8_t(kCardBase >> 16), 0xD3, 0x1C,     // HAD
        0x3E, uint8_t(kCardBase >> 8), 0xD3, 0x1D,      // MAD
        0x3E, uint8_t(kCardBase), 0xD3, 0x1E,           // LAD
    };
    if (waitForCommand)
    {
        const size_t poll = p.size();
        p.insert(p.end(), {0xDB, 0x04,        // poll: IN A,(ZXSTAT)
                           0x1F,              // RRA: bit 0 = command
                           0x30, 0x00});      // JR NC,poll
        p.back() = static_cast<uint8_t>(0x100 - (p.size() - poll));
        p.insert(p.end(), {0xD3, 0x05});      // OUT (CLRCBIT),A
    }
    p.insert(p.end(), {0x3E, 0x80, 0xD3, 0x1F, // CST = #80: start
                       0x18, 0xFE});           // JR $
    return p;
}

struct Result
{
    std::vector<uint8_t> hostBuffer;
    std::vector<uint8_t> cardRam;
    uint64_t hostRamHash = 0;
    uint32_t tStates = 0;
    uint64_t bytesRead = 0, bytesWritten = 0, lateStarts = 0;
    uint32_t cardAddress = 0;
};

class NeoGSZxDma_Test : public ::testing::TestWithParam<bool> // debug mode
{
protected:
    SoundCardScope _gs{TestSound::GeneralSound};
    Emulator* _emulator = nullptr;
    EmulatorContext* _ctx = nullptr;
    Z80* _z80 = nullptr;
    SoundChip_NeoGS* _card = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON");
        ASSERT_NE(_emulator, nullptr);
        _ctx = _emulator->GetContext();
        _z80 = _ctx->pCore->GetZ80();
        ASSERT_TRUE(FitGeneralSoundCard(_ctx->pSoundManager, GSTypeKind::NGS));
        _card = dynamic_cast<SoundChip_NeoGS*>(_ctx->pSoundManager->getGeneralSound());
        ASSERT_NE(_card, nullptr);
        _ctx->pFeatureManager->setFeature(Features::kDebugMode, GetParam());
        _ctx->pMemory->UpdateFeatureCache();
    }

    /// One frame through the main loop: host, card and every device, as the
    /// running emulator does
    void frame() { reinterpret_cast<MainLoop_CUT*>(_ctx->pMainLoop)->RunFrame(); }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// Card program in flash page 0, cold boot, known card RAM
    void bootCard(bool waitForCommand)
    {
        const std::vector<uint8_t> program = cardProgram(waitForCommand);
        _card->flash().load(program.data(), program.size());
        _card->reset(); // a cold boot clears the RAM: fill it after
        for (int i = 0; i < 1024; i++)
            _card->memory().ram()[kCardBase + i] = static_cast<uint8_t>(i * 7 + 3);
    }

    void hostWrite(uint16_t at, std::initializer_list<uint8_t> bytes)
    {
        for (uint8_t b : bytes)
            _z80->DirectWrite(at++, b);
    }

    /// Host idles in RAM with interrupts off while the card sets up. Two
    /// frames: a card fitted mid-frame takes its first frame base at the next
    /// frame start, so it runs from the second frame on
    void idleFrame()
    {
        hostWrite(kHostIdle, {0xF3, 0x18, 0xFE}); // DI : JR $
        _z80->pc = kHostIdle;
        _z80->iff1 = _z80->iff2 = 0;
        frame();
        frame();
    }

    /// Runs the host from kHostCode, whole frames, until it parks at its
    /// closing JR $ at `end`
    void runHost(uint16_t end)
    {
        _z80->pc = kHostCode;
        _z80->iff1 = _z80->iff2 = 0;
        for (int guard = 0; guard < 50 && _z80->pc != end; guard++)
            frame();
        ASSERT_EQ(_z80->pc, end);
    }

    Result observe(size_t bufferBytes)
    {
        frame(); // let the last byte complete on the card
        Result r;
        for (size_t i = 0; i < bufferBytes; i++)
            r.hostBuffer.push_back(_z80->DirectRead(static_cast<uint16_t>(kHostBuffer + i)));
        r.cardRam.assign(_card->memory().ram() + kCardBase, _card->memory().ram() + kCardBase + 1024);
        r.hostRamHash = ttd::HashBytes(_ctx->pMemory->RAMBase(), static_cast<size_t>(_ctx->config.ramsize) * 1024u);
        r.tStates = _z80->t;
        r.bytesRead = _card->zxDma().bytesRead();
        r.bytesWritten = _card->zxDma().bytesWritten();
        r.lateStarts = _card->zxDma().lateStarts();
        r.cardAddress = _card->dma().address(NeoGSDma::ZX);
        return r;
    }
};
} // namespace

TEST_P(NeoGSZxDma_Test, HostReadsABlockOfCardRam)
{
    bootCard(false);
    idleFrame();
    ASSERT_EQ(_card->zxDma().mode(), NeoGSZxDma::Mode::Divert) << "the card started the module";
    ASSERT_EQ(_ctx->pMemory->GetBusOverlay(), static_cast<const HostBusOverlay*>(&_card->zxDma()));
    EXPECT_EQ(_z80->MemIf, GetParam() ? _z80->OverlayDbgMemIf : _z80->OverlayFastMemIf);

    constexpr uint16_t n = 512;
    hostWrite(kHostCode, {0x21, 0x00, 0x00,                          // LD HL,#0000
                          0x11, uint8_t(kHostBuffer), uint8_t(kHostBuffer >> 8), // LD DE,buffer
                          0x01, uint8_t(n + 1), uint8_t((n + 1) >> 8),           // LD BC,n+1 (a dummy read first)
                          0xED, 0xB0,                                  // LDIR
                          0x18, 0xFE});                                // JR $
    _card->startPortTrace();
    runHost(kHostCode + 11);
    const Result r = observe(n + 1);

    // The port trace shows every byte: the card address and what the host got
    std::vector<GSTraceEvent> dmaEvents;
    for (const GSTraceEvent& e : _card->getPortTraceEvents())
        if (e.side == GSTraceSide::ZxDma)
            dmaEvents.push_back(e);
    ASSERT_EQ(dmaEvents.size(), n + 1u);
    EXPECT_EQ((static_cast<uint32_t>(dmaEvents[0].channel) << 16) | dmaEvents[0].port, kCardBase);
    EXPECT_EQ((static_cast<uint32_t>(dmaEvents[5].channel) << 16) | dmaEvents[5].port, kCardBase + 5);
    EXPECT_EQ(dmaEvents[5].value, static_cast<uint8_t>(4 * 7 + 3)) << "read 5 returns the byte fetched by read 4";
    EXPECT_FALSE(dmaEvents[5].isOut());
    EXPECT_EQ(dmaEvents[5].pc, kHostCode + 9) << "the host PC of the LDIR";

    // What automation shows (CLI / WebAPI / MCP / Lua / Python read this)
    NeoGSStateInfo info;
    ASSERT_TRUE(_card->neogsState(info));
    EXPECT_STREQ(info.zxMode, "divert");
    EXPECT_TRUE(info.zxOverlayInstalled);
    EXPECT_EQ(info.dmaAddress[0], kCardBase + n + 1);
    EXPECT_EQ(info.zxBytesRead, n + 1u);
    EXPECT_STREQ(info.zxPending, "none");
    EXPECT_EQ(info.zxReadLatch, static_cast<uint8_t>(n * 7 + 3)) << "the next read gets byte n";
    EXPECT_STREQ(info.zxWatchSetting, "selected");
    EXPECT_EQ(info.zxLateStarts, 0u);

    EXPECT_EQ(r.hostBuffer[0], 0xFF) << "the first read is the old latch";
    for (int i = 0; i < n; i++)
        ASSERT_EQ(r.hostBuffer[i + 1], static_cast<uint8_t>(i * 7 + 3)) << "byte " << i;
    EXPECT_EQ(r.bytesRead, n + 1u);
    EXPECT_EQ(r.cardAddress, kCardBase + n + 1) << "every fetch moved the address";
    EXPECT_EQ(r.lateStarts, 0u);
    EXPECT_EQ(_card->zxDma().waitTStates(), 0u) << "LDIR at 3.5 MHz never waits";
}

TEST_P(NeoGSZxDma_Test, HostWritesABlockIntoCardRamNotIntoRom)
{
    bootCard(false);
    idleFrame();
    const uint8_t rom0 = _z80->DirectRead(0x0000);
    constexpr uint16_t n = 256;
    for (int i = 0; i < n; i++)
        _z80->DirectWrite(static_cast<uint16_t>(kHostBuffer + i), static_cast<uint8_t>(0xFF - i));
    hostWrite(kHostCode, {0x21, uint8_t(kHostBuffer), uint8_t(kHostBuffer >> 8), // LD HL,buffer
                          0x11, 0x00, 0x00,                                      // LD DE,#0000
                          0x01, uint8_t(n), uint8_t(n >> 8),                     // LD BC,n
                          0xED, 0xB0, 0x18, 0xFE});                              // LDIR : JR $
    runHost(kHostCode + 11);
    const Result r = observe(0);

    for (int i = 0; i < n; i++)
        ASSERT_EQ(r.cardRam[i], static_cast<uint8_t>(0xFF - i)) << "byte " << i;
    EXPECT_EQ(r.cardRam[n], static_cast<uint8_t>(n * 7 + 3)) << "nothing past the block";
    EXPECT_EQ(r.bytesWritten, n);
    EXPECT_EQ(r.cardAddress, kCardBase + n);
    EXPECT_EQ(_z80->DirectRead(0x0000), rom0) << "the ROM is not written";
}

TEST_P(NeoGSZxDma_Test, StartAfterAHandshakeIsSeenAtTheFirstAccess)
{
    // The card selects the module, then waits for a host command before it
    // starts. Long after, the host sends the command, pauses in RAM (no port
    // access) and reads: the start happens during the pause. The watch window
    // opened by the selection has closed by then; the command's port access
    // opens it again, and the first read must already be diverted
    bootCard(true);
    idleFrame();
    ASSERT_EQ(_card->zxDma().mode(), NeoGSZxDma::Mode::Watch) << "selecting the module opens the window";
    for (uint32_t i = 0; i <= _card->zxDma().watchFrames(); i++)
        frame();
    ASSERT_EQ(_card->zxDma().mode(), NeoGSZxDma::Mode::Off) << "the window closed";

    hostWrite(kHostCode, {0x3E, 0x42, 0xD3, 0xBB,              // LD A,#42 : OUT (#BB),A - the command
                          0x06, 0x00, 0x10, 0xFE, 0x10, 0xFE,  // LD B,0 : DJNZ $ : DJNZ $ (~6.6k T)
                          0x3A, 0x00, 0x00,                    // LD A,(#0000) - dummy
                          0x3A, 0x00, 0x00,                    // LD A,(#0000) - card byte 0
                          0x32, uint8_t(kHostBuffer), uint8_t(kHostBuffer >> 8), // LD (buffer),A
                          0x18, 0xFE});
    runHost(kHostCode + 19);
    const Result r = observe(1);
    EXPECT_EQ(r.hostBuffer[0], 3) << "card byte 0 (i*7+3 for i = 0)";
    EXPECT_EQ(r.lateStarts, 0u) << "the port access re-armed the watch window";
    EXPECT_EQ(r.bytesRead, 2u);
}

TEST_P(NeoGSZxDma_Test, LeavingPathsRemoveTheOverlay)
{
    bootCard(false);
    idleFrame();
    ASSERT_TRUE(_card->zxDma().installed());

    _card->resetCard(); // host #33 reset: FPGA registers, the run bit clears
    EXPECT_EQ(_ctx->pMemory->GetBusOverlay(), nullptr);
    EXPECT_EQ(_z80->MemIf, GetParam() ? _z80->DbgMemIf : _z80->FastMemIf);

    bootCard(false);
    idleFrame();
    ASSERT_TRUE(_card->zxDma().installed());
    _emulator->Reset(); // host reset: a cold boot of the card
    EXPECT_EQ(_ctx->pMemory->GetBusOverlay(), nullptr);

    bootCard(false);
    idleFrame();
    ASSERT_NE(_ctx->pMemory->GetBusOverlay(), nullptr);
    ASSERT_TRUE(_ctx->pSoundManager->switchGeneralSoundCard(GSTypeKind::Z80)); // the card is destroyed
    EXPECT_EQ(_ctx->pMemory->GetBusOverlay(), nullptr);
    EXPECT_EQ(_z80->MemIf, GetParam() ? _z80->DbgMemIf : _z80->FastMemIf);
    frame(); // the host runs on the plain path
    frame();
}

TEST_P(NeoGSZxDma_Test, WatchWindowClosesAndTheOverlayGoes)
{
    // Module selected, not started (the card waits for a command that never
    // comes): Watch for ZxDmaWatchFrames frames, then Off - the host is back
    // on the plain memory path
    bootCard(true);
    idleFrame();
    ASSERT_EQ(_card->zxDma().mode(), NeoGSZxDma::Mode::Watch);
    ASSERT_NE(_ctx->pMemory->GetBusOverlay(), nullptr);
    const uint32_t frames = _card->zxDma().watchFrames();
    for (uint32_t i = 0; i + 1 < frames; i++)
        frame();
    EXPECT_EQ(_card->zxDma().mode(), NeoGSZxDma::Mode::Watch) << "still inside the window";
    frame();
    frame();
    EXPECT_EQ(_card->zxDma().mode(), NeoGSZxDma::Mode::Off);
    EXPECT_EQ(_ctx->pMemory->GetBusOverlay(), nullptr);
    EXPECT_EQ(_z80->MemIf, GetParam() ? _z80->DbgMemIf : _z80->FastMemIf);
}

INSTANTIATE_TEST_SUITE_P(Modes, NeoGSZxDma_Test, ::testing::Values(false, true),
                         [](const ::testing::TestParamInfo<bool>& info) { return info.param ? std::string("Debug") : std::string("Fast"); });

/// Fast and debug mode move the same bytes at the same moments
TEST(NeoGSZxDma_Determinism, FastAndDebugModeAgree)
{
    // Each mode in its own machine: the parametrised cases above run the
    // scenario; this compares their end states
    Result results[2];
    for (int debug = 0; debug < 2; debug++)
    {
        SoundCardScope gs{TestSound::GeneralSound};
        // Zeroed power-on RAM: the same contents in both machines
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(emulator, nullptr);
        EmulatorContext* ctx = emulator->GetContext();
        ASSERT_TRUE(FitGeneralSoundCard(ctx->pSoundManager, GSTypeKind::NGS));
        auto* card = dynamic_cast<SoundChip_NeoGS*>(ctx->pSoundManager->getGeneralSound());
        ctx->pFeatureManager->setFeature(Features::kDebugMode, debug != 0);
        ctx->pMemory->UpdateFeatureCache();
        Z80* z80 = ctx->pCore->GetZ80();

        const std::vector<uint8_t> program = cardProgram(false);
        card->flash().load(program.data(), program.size());
        card->reset();
        for (int i = 0; i < 1024; i++)
            card->memory().ram()[kCardBase + i] = static_cast<uint8_t>(i * 13);
        for (uint16_t a = kHostIdle; uint8_t b : {0xF3, 0x18, 0xFE})
            z80->DirectWrite(a++, b);
        z80->pc = kHostIdle;
        z80->iff1 = z80->iff2 = 0;
        auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(ctx->pMainLoop);
        mainLoop->RunFrame(); // the card's first frame base
        mainLoop->RunFrame();
        // Read 300, write 200 back, read 100
        const uint8_t code[] = {0x21, 0x00, 0x00, 0x11, 0x00, 0x90, 0x01, 0x2C, 0x01, 0xED, 0xB0, // read 300
                                0x21, 0x00, 0x90, 0x11, 0x00, 0x00, 0x01, 0xC8, 0x00, 0xED, 0xB0, // write 200
                                0x21, 0x00, 0x00, 0x11, 0x00, 0xA0, 0x01, 0x64, 0x00, 0xED, 0xB0, // read 100
                                0x18, 0xFE};
        for (size_t i = 0; i < sizeof code; i++)
            z80->DirectWrite(static_cast<uint16_t>(kHostCode + i), code[i]);
        z80->pc = kHostCode;
        const uint16_t end = static_cast<uint16_t>(kHostCode + sizeof code - 2);
        for (int guard = 0; guard < 50 && z80->pc != end; guard++)
            mainLoop->RunFrame();
        ASSERT_EQ(z80->pc, end);
        mainLoop->RunFrame();

        Result& r = results[debug];
        r.hostRamHash = ttd::HashBytes(ctx->pMemory->RAMBase(), static_cast<size_t>(ctx->config.ramsize) * 1024u);
        r.cardRam.assign(card->memory().ram() + kCardBase, card->memory().ram() + kCardBase + 1024);
        r.tStates = z80->t;
        r.cardAddress = card->dma().address(NeoGSDma::ZX);
        r.bytesRead = card->zxDma().bytesRead();
        r.bytesWritten = card->zxDma().bytesWritten();
        EXPECT_EQ(r.bytesRead, 400u);
        EXPECT_EQ(r.bytesWritten, 200u);
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
    EXPECT_EQ(results[0].hostRamHash, results[1].hostRamHash);
    EXPECT_EQ(results[0].cardRam, results[1].cardRam);
    EXPECT_EQ(results[0].tStates, results[1].tStates);
    EXPECT_EQ(results[0].cardAddress, results[1].cardAddress);
}

/// A long stream: 30 frames of endless LDIR reads count the same bytes and
/// end in the same machine state in fast and debug mode
TEST(NeoGSZxDma_Determinism, LongStreamAgreesInFastAndDebugMode)
{
    uint64_t bytes[2] = {};
    uint32_t tStates[2] = {};
    uint64_t hostHash[2] = {};
    for (int debug = 0; debug < 2; debug++)
    {
        SoundCardScope gs{TestSound::GeneralSound};
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(emulator, nullptr);
        EmulatorContext* ctx = emulator->GetContext();
        ASSERT_TRUE(FitGeneralSoundCard(ctx->pSoundManager, GSTypeKind::NGS));
        auto* card = dynamic_cast<SoundChip_NeoGS*>(ctx->pSoundManager->getGeneralSound());
        ctx->pFeatureManager->setFeature(Features::kDebugMode, debug != 0);
        ctx->pMemory->UpdateFeatureCache();
        Z80* z80 = ctx->pCore->GetZ80();
        auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(ctx->pMainLoop);

        const std::vector<uint8_t> program = cardProgram(false);
        card->flash().load(program.data(), program.size());
        card->reset();
        const uint8_t host[] = {0xF3, 0x21, 0x00, 0x00, 0x11, 0x00, 0x90, 0x01, 0x00, 0x10, 0xED, 0xB0, 0x18, 0xF3};
        for (size_t i = 0; i < sizeof host; i++)
            z80->DirectWrite(static_cast<uint16_t>(kHostCode + i), host[i]);
        z80->pc = kHostCode;
        z80->iff1 = z80->iff2 = 0;
        for (int f = 0; f < 32; f++)
            mainLoop->RunFrame();

        bytes[debug] = card->zxDma().bytesRead();
        tStates[debug] = z80->t;
        hostHash[debug] = ttd::HashBytes(ctx->pMemory->RAMBase(), static_cast<size_t>(ctx->config.ramsize) * 1024u);
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
    EXPECT_GT(bytes[0], 30u * 3000u) << "about 3,400 LDIR reads a frame";
    EXPECT_EQ(bytes[0], bytes[1]);
    EXPECT_EQ(tStates[0], tStates[1]);
    EXPECT_EQ(hostHash[0], hostHash[1]);
}

