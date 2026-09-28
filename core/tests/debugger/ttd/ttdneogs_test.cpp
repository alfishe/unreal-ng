#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "_helpers/emulatortesthelper.h"
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
#include "emulator/memory/memory.h"
#include "emulator/sound/chips/neogs/soundchip_neogs.h"
#include "emulator/sound/soundmanager.h"

/// NeoGS under the TTD engine (neogs-tdd.md §7.4, TTD v1: the whole card in
/// every checkpoint). The card boots its firmware from an SD card image while
/// the recording runs - SD protocol, SPI masters, card CPU and RAM all busy -
/// and the recording is re-executed from a session start, a per-frame
/// checkpoint and a mid-frame seek. Each run must reach the recorded end with
/// the card's blob identical byte for byte.
///
/// Runtime justification: the SD boot spans tens of frames, and each
/// checkpoint compresses 4.5 MB of card state.
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
        strncpy(_context->config.ngs.sdCardPath, _sdImage->path().c_str(), sizeof _context->config.ngs.sdCardPath - 1);
        ASSERT_TRUE(_sm->switchGeneralSoundCard(GSTypeKind::NGS));
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

TEST_F(TTD_NeoGS_Test, SdBootReplaysExactlyFromEveryKindOfRestorePoint)
{
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
