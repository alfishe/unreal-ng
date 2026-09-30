#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/neogstestsdcard.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdexternalevents.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/hostbusoverlay.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"

/// NeoGS under the TTD engine (neogs-tdd.md §7.4). TTD v1 checkpoints carry
/// the card's registers and device state but not its RAM (2-4 MB) or flash
/// (512 KB): large memories wait for TTD v2 memory regions. The card boots its
/// firmware from an SD card image while the recording runs - SD protocol, SPI
/// masters, card CPU and RAM all busy.
///
/// Runtime justification: the SD boot spans tens of frames.
namespace
{
/// ATM710 fits a General Sound card by default (data/configs/atm710/unreal.ini)
constexpr const char* kGsCapableModel = "ATM710";
constexpr uint8_t kNeoGS = static_cast<uint8_t>(ttd::PeripheralId::NeoGS);

struct Point
{
    uint64_t frame = 0;
    uint32_t tInFrame = 0;
    uint64_t ramHash = 0;
    std::vector<uint8_t> card;
};
} // namespace

class TTD_NeoGS_Test : public ::testing::Test
{
protected:
    SoundCardScope _soundCards; // first: the GS slot must be fitted when the machine is created
    std::unique_ptr<ScratchFatImage> _sdImage; // outlives the emulator (removed after TearDown)

    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    SoundManager* _sm = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(kGsCapableModel, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelManager;
        _sm = _context->pSoundManager;
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();

        _sdImage = MakeNeoGSTestSd(NeoGSTestSd::Fat16Mbr);
        ASSERT_TRUE(_sdImage->ok()) << _sdImage->error();
        ASSERT_TRUE(FitNeoGSWithSd(_context, _sdImage->path()));
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    SoundChip_NeoGS* card() const { return static_cast<SoundChip_NeoGS*>(_sm->getGeneralSound()); }

    Point Observe() const
    {
        Point p;
        p.frame = _context->emulatorState.frame_counter;
        p.tInFrame = _context->pCore->GetZ80()->t;
        p.ramHash = ttd::HashBytes(_context->pMemory->RAMBase(), static_cast<size_t>(_context->config.ramsize) * 1024u);
        p.card.resize(card()->TTDStateSize());
        card()->TTDSaveState(p.card.data());
        return p;
    }

    void RunTo(const Point& target)
    {
        const EmulatorState* state = &_context->emulatorState;
        const uint64_t frame = target.frame;
        const uint32_t t = target.tInFrame;
        _emulator->RunUntilCondition([state, frame, t](const Z80State& z80)
                                     { return state->frame_counter > frame || (state->frame_counter == frame && z80.t >= t); });
    }
};

/// The recording is re-executed from a session start, a per-frame checkpoint
/// and a mid-frame seek; each run must reach the recorded end with the card's
/// blob identical byte for byte. Needs the card RAM restored with the
/// checkpoint - kept for TTD v2 memory regions
TEST_F(TTD_NeoGS_Test, SdBootReplaysExactlyFromEveryKindOfRestorePoint)
{
    GTEST_SKIP() << "TTD v2: NeoGS card RAM and flash are not in v1 checkpoints, so the firmware re-executes over "
                    "the live (later) RAM after a seek";
    ASSERT_TRUE(card()->sdCardPresent());
    _emulator->RunNFrames(1);
    _emulator->RunNCPUCycles(2345); // the baseline lands mid-frame
    ASSERT_FALSE(card()->isReadyForCommands()) << "the recording must cover the SD boot";

    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t startFrame = _ttd->GetCheckpoint(0)->time.frame;
    _emulator->RunNFrames(30);
    _emulator->RunNCPUCycles(777);
    const Point recorded = Observe();
    _ttd->StopRecording();
    ASSERT_TRUE(card()->isReadyForCommands()) << "the main ROM, loaded from the SD card, must be running";
    ASSERT_GE(card()->sdCard()->blocksRead(), 64u);

    struct Start
    {
        std::string name;
        ttd::TTDTimePoint at;
    };
    const Start starts[] = {
        {"session start (mid-frame baseline)", {startFrame, 0}},
        {"per-frame checkpoint in the boot", {startFrame + 3, 0}},
        {"mid-frame seek", {startFrame + 7, 23456}},
    };
    for (const Start& start : starts)
    {
        SCOPED_TRACE(start.name);
        ASSERT_TRUE(_ttd->SeekTo(start.at));
        RunTo(recorded);
        const Point replayed = Observe();
        EXPECT_EQ(replayed.frame, recorded.frame);
        EXPECT_EQ(replayed.tInFrame, recorded.tInFrame);
        EXPECT_EQ(replayed.ramHash, recorded.ramHash);
        ASSERT_EQ(replayed.card.size(), recorded.card.size());
        size_t first = 0;
        while (first < recorded.card.size() && replayed.card[first] == recorded.card[first])
            first++;
        EXPECT_EQ(first, recorded.card.size()) << "the card differs from byte " << first;
    }
}

/// The SD card's sectors are not in the blob: a card write is a replay
/// barrier, one marker per frame however many blocks the frame writes
TEST_F(TTD_NeoGS_Test, SdWritesLeaveOneMarkerPerFrame)
{
    _emulator->RunNFrames(20);
    ASSERT_TRUE(card()->isReadyForCommands()) << "booted: the loader left the SD card initialised";
    SdCardSpi* sd = card()->sdCard();
    ASSERT_TRUE(sd->initialized());

    // CMD24 and a data block, through the protocol like the card's SD master
    const auto writeBlock = [sd](uint32_t block)
    {
        sd->select(true);
        const uint32_t arg = sd->isSdhc() ? block : block * 512;
        for (const uint8_t b : {uint8_t(0x58), uint8_t(arg >> 24), uint8_t(arg >> 16), uint8_t(arg >> 8), uint8_t(arg), uint8_t(0xFF)})
            sd->exchange(b);
        uint8_t r1 = 0xFF;
        for (int i = 0; i < 8 && r1 == 0xFF; i++)
            r1 = sd->exchange(0xFF);
        EXPECT_EQ(r1, 0x00);
        sd->exchange(0xFE);
        for (int i = 0; i < 514; i++)
            sd->exchange(0x5A);
        for (int i = 0; i < 80; i++)
            sd->exchange(0xFF); // data response and busy
        sd->select(false);
    };

    ASSERT_TRUE(_ttd->StartRecording());
    const auto diskWrites = [this]
    {
        size_t n = 0;
        for (const ttd::TTDExternalEvent& e : _ttd->GetExternalEvents().SnapshotEvents())
            n += e.kind == ttd::TTDExternalEventKind::DiskWrite ? 1 : 0;
        return n;
    };
    const uint64_t written = sd->blocksWritten();
    writeBlock(1000);
    writeBlock(1001);
    EXPECT_EQ(diskWrites(), 1u) << "two blocks in one frame";
    _emulator->RunNFrames(1);
    writeBlock(1002);
    EXPECT_EQ(diskWrites(), 2u);
    EXPECT_EQ(sd->blocksWritten(), written + 3);
    _ttd->StopRecording();
}

/// ZX-DMA under the TTD engine (neogs-zxdma-design.md §5.9): the host streams
/// card RAM in and out through #0000-#3FFF while the recording runs, so the
/// latch, the pending byte, the watch window and the installed overlay are
/// all live at every restore point. Each replay must reach the recorded end
/// with the card blob, host RAM and CPU identical.
class TTD_NeoGSZxDma_Test : public ::testing::Test
{
protected:
    SoundCardScope _soundCards;
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelManager;
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        ASSERT_TRUE(FitGeneralSoundCard(_context->pSoundManager, GSTypeKind::NGS));
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    SoundChip_NeoGS* card() const { return static_cast<SoundChip_NeoGS*>(_context->pSoundManager->getGeneralSound()); }

    Point Observe() const
    {
        Point p;
        p.frame = _context->emulatorState.frame_counter;
        p.tInFrame = _context->pCore->GetZ80()->t;
        p.ramHash = ttd::HashBytes(_context->pMemory->RAMBase(), static_cast<size_t>(_context->config.ramsize) * 1024u);
        p.card.resize(card()->TTDStateSize());
        card()->TTDSaveState(p.card.data());
        return p;
    }

    void RunTo(const Point& target)
    {
        const EmulatorState* state = &_context->emulatorState;
        const uint64_t frame = target.frame;
        const uint32_t t = target.tInFrame;
        _emulator->RunUntilCondition([state, frame, t](const Z80State& z80)
                                     { return state->frame_counter > frame || (state->frame_counter == frame && z80.t >= t); });
    }
};

TEST_F(TTD_NeoGSZxDma_Test, StreamingReplaysExactlyFromEveryKindOfRestorePoint)
{
    // Card: select the ZX module at #012000 and start it
    const uint8_t cardCode[] = {0xF3, 0x3E, 0x01, 0xD3, 0x1B, 0x3E, 0x01, 0xD3, 0x1C, 0x3E, 0x20, 0xD3, 0x1D,
                                0xAF, 0xD3, 0x1E, 0x3E, 0x80, 0xD3, 0x1F, 0x18, 0xFE};
    card()->flash().load(cardCode, sizeof cardCode);
    card()->reset();
    for (int i = 0; i < 0x3000; i++)
        card()->memory().ram()[0x012000 + i] = static_cast<uint8_t>(i * 5 + 1);

    // Host (DI): read 3000 bytes from #0000, write them back, forever - the
    // card's address runs on, so the data keeps changing
    Z80* z80 = _context->pCore->GetZ80();
    const uint8_t hostCode[] = {0xF3,                          // DI
                                0x21, 0x00, 0x00,              // loop: LD HL,#0000
                                0x11, 0x00, 0x90,              // LD DE,#9000
                                0x01, 0xB8, 0x0B,              // LD BC,3000
                                0xED, 0xB0,                    // LDIR  (read)
                                0x21, 0x00, 0x90,              // LD HL,#9000
                                0x11, 0x00, 0x00,              // LD DE,#0000
                                0x01, 0xB8, 0x0B,              // LD BC,3000
                                0xED, 0xB0,                    // LDIR  (write)
                                0x18, 0xE4};                   // JR loop
    for (size_t i = 0; i < sizeof hostCode; i++)
        z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), hostCode[i]);
    z80->pc = 0x8000;
    z80->iff1 = z80->iff2 = 0;

    _emulator->RunNFrames(2);        // the card's first frame base, then its setup
    ASSERT_EQ(card()->zxDma().mode(), NeoGSZxDma::Mode::Divert);
    _emulator->RunNCPUCycles(1234);  // the baseline lands mid-frame, mid-transfer

    ASSERT_TRUE(_ttd->StartRecording());
    const uint64_t startFrame = _ttd->GetCheckpoint(0)->time.frame;
    _emulator->RunNFrames(12);
    _emulator->RunNCPUCycles(555);
    const Point recorded = Observe();
    _ttd->StopRecording();
    ASSERT_GT(card()->zxDma().bytesRead(), 12u * 1000u) << "the host really streamed";
    ASSERT_GT(card()->zxDma().bytesWritten(), 1000u);

    struct Start
    {
        std::string name;
        ttd::TTDTimePoint at;
    };
    const Start starts[] = {
        {"session start (mid-frame, mid-transfer baseline)", {startFrame, 0}},
        {"per-frame checkpoint", {startFrame + 5, 0}},
        {"mid-frame seek", {startFrame + 8, 30001}},
    };
    for (const Start& start : starts)
    {
        SCOPED_TRACE(start.name);
        ASSERT_TRUE(_ttd->SeekTo(start.at));
        EXPECT_EQ(card()->zxDma().mode(), NeoGSZxDma::Mode::Divert);
        EXPECT_EQ(_context->pMemory->GetBusOverlay(), static_cast<const HostBusOverlay*>(&card()->zxDma()))
            << "the restore re-installed the overlay";
        RunTo(recorded);
        const Point replayed = Observe();
        EXPECT_EQ(replayed.frame, recorded.frame);
        EXPECT_EQ(replayed.tInFrame, recorded.tInFrame);
        EXPECT_EQ(replayed.ramHash, recorded.ramHash);
        ASSERT_EQ(replayed.card.size(), recorded.card.size());
        size_t first = 0;
        while (first < recorded.card.size() && replayed.card[first] == recorded.card[first])
            first++;
        EXPECT_EQ(first, recorded.card.size()) << "the card differs from byte " << first;
    }
}
