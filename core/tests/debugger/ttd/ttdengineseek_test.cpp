/// @file ttdengineseek_test.cpp
/// @brief Phase 3: a seek inside a frame from the engine's data alone (its
/// checkpoint - CPU, chipset, every memory region, every device - its event
/// log and its bus journals) lands on exactly the machine the recording had
/// there live: CPU, all RAM and every device's state, at several points of
/// several frames, kept while the machine ran. Scenarios: a live recording
/// with key presses mid-frame, a tool edit, sector reads, interrupt vectors,
/// the ROM boot of several models; the recorded fixtures with keys (Dizzy X)
/// and a tape load (Green Beret) seek to the same machine from any position.
/// (Until Phase 6 a seek from v1's TimeTravelManager data was the reference.)
///
/// Boots machines and replays tens of frames: slower than the 50 ms
/// guideline, one acceptance check per scenario.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_atm3.h"
#include "_helpers/zcsdtesthelper.h"

namespace fs = std::filesystem;

namespace
{
/// Everything a seek must reproduce
struct MachineState
{
    ttd::TTDCpuState cpu;
    uint64_t frame = 0;
    uint64_t t = 0;
    std::vector<uint8_t> ram;
    std::map<uint8_t, std::vector<uint8_t>> devices;
};
}  // namespace

class TTDEngineSeek_Test : public ::testing::Test
{
protected:
    SoundCardScope _soundCards;   // the fixtures were recorded on the shipped configs, cards included
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    size_t _busPlayed = 0;   ///< seeks whose CPU read the engine's IN journal

    /// A position the machine was at live, and what it was there
    struct LivePoint
    {
        ttd::TTDTimePoint at;
        MachineState state;
    };
    std::vector<LivePoint> _live;

    void StartMachine(const std::string& model, GSTypeKind generalSound, bool fitGs)
    {
        // The card the session was recorded with, fitted at creation: the shipped 48K / 128K / +2 / +2A / +3, Profi
        // and Sprinter configs have no GS since 2026-10-04 (owner decision), and a switch cannot fill an empty slot
        {
            GeneralSoundFitScope fit(fitGs ? generalSound : GSTypeKind::NONE);
            _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        }
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        // The shipped ts-conf config carries a Z-Controller card (wc-zifi.img, 2026-10-05): these seeks are about the
        // CPU and the journals, not the SD card, so the slot is left empty
        if (_context->pMediaManager)
            _context->pMediaManager->Eject("sd.zc");
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        if (fitGs)
        {
            ASSERT_TRUE(FitGeneralSoundCard(_context->pSoundManager, generalSound));
        }
    }

    void StopMachine()
    {
        if (!_emulator)
            return;
        _ttd->StopRecording();
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
        _ttd = nullptr;
        _live.clear();
    }

    void TearDown() override { StopMachine(); }

    ttd::TimeTravelEngine& Engine() const { return _ttd->GetEngine(); }
    uint32_t Span() const { return static_cast<uint32_t>(_ttd->FrameSpan()); }

    MachineState Capture() const
    {
        MachineState s;
        Z80* z80 = _context->pCore->GetZ80();
        s.cpu = ttd::CaptureCpuState(*z80);
        s.frame = _context->emulatorState.frame_counter;
        s.t = z80->t;
        const size_t pages = _ttd->GetModelRamPages();
        s.ram.assign(_context->pMemory->RAMPageAddress(0), _context->pMemory->RAMPageAddress(0) + pages * 0x4000);
        for (const ttd::TTDRegistryEntry& e : _ttd->GetPeripheralRegistry().Entries())
            if (e.device && e.device->TTDStateSize() != 0 && e.legacyId != ttd::PeripheralId::None)
                e.device->TTDSaveStateTo(s.devices[static_cast<uint8_t>(e.legacyId)]);
        return s;
    }

    /// Keep what the machine is now
    void KeepLive() { _live.push_back({_ttd->CurrentPosition(), Capture()}); }

    /// Records @p frames frames. Kept along the way: @p perFrame points spread over frames 1, n/3, n/2 and n-2.
    /// @p during runs at the start of every frame (it may run into the frame and keep points itself)
    void RecordKeeping(int frames, size_t perFrame = 3, const std::function<void(int)>& during = {})
    {
        ASSERT_TRUE(_ttd->StartRecording());
        for (int f = 0; f < frames; ++f)
        {
            if (during)
                ASSERT_NO_FATAL_FAILURE(during(f));
            if (f == 1 || f == frames / 3 || f == frames / 2 || f == frames - 2)
            {
                const uint32_t step = Span() / static_cast<uint32_t>(perFrame + 1);
                for (size_t k = 0; k < perFrame; ++k)
                {
                    _emulator->RunTStates(step, /*skipBreakpoints=*/true);
                    KeepLive();
                }
            }
            _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        }
        _ttd->StopRecording();
    }

    /// The machine is @p expected
    void ExpectState(const MachineState& expected)
    {
        const MachineState seen = Capture();
        EXPECT_EQ(std::memcmp(&seen.cpu, &expected.cpu, sizeof(seen.cpu)), 0) << "CPU";
        EXPECT_EQ(seen.frame, expected.frame);
        EXPECT_EQ(seen.t, expected.t);
        ASSERT_EQ(seen.ram.size(), expected.ram.size());
        for (size_t i = 0; i < seen.ram.size(); ++i)
            if (seen.ram[i] != expected.ram[i])
            {
                ADD_FAILURE() << "RAM differs at " << i << " (page " << i / 0x4000 << ")";
                break;
            }
        for (const auto& [id, bytes] : expected.devices)
            EXPECT_TRUE(seen.devices.count(id) && seen.devices.at(id) == bytes) << "device " << int(id);
    }

    /// Seek to @p target; counts the seeks whose CPU read the engine's bus journal (played past the checkpoint's
    /// cursor)
    void Seek(const ttd::TTDTimePoint& target)
    {
        ttd::TTDSeekResult r;
        ASSERT_TRUE(_ttd->SeekTo(target, &r));
        EXPECT_EQ(r.arrivedAt, target);
        const int64_t cp = Engine().CheckpointIndexOf({0, target.frame, 0});
        ASSERT_GE(cp, 0);
        if (Engine().BusReads().Cursor() > Engine().Checkpoint(size_t(cp))->busReadCursor)
            ++_busPlayed;
    }

    /// Every kept point, newest first (each seek starts from another position), shows the live machine
    void ExpectEverySeekShowsLive()
    {
        ASSERT_FALSE(_live.empty());
        for (auto it = _live.rbegin(); it != _live.rend(); ++it)
        {
            SCOPED_TRACE("frame " + std::to_string(it->at.frame) + " T " + std::to_string(it->at.tInFrame));
            ASSERT_NO_FATAL_FAILURE(Seek(it->at));
            ExpectState(it->state);
            if (HasFailure())
                return;   // the first divergence is the useful one
        }
    }

    /// Points spread over the session: inside frames, at several offsets
    std::vector<ttd::TTDTimePoint> Targets(size_t perFrame) const
    {
        std::vector<ttd::TTDTimePoint> out;
        const size_t count = _ttd->GetCheckpointCount();
        for (size_t i : {size_t(1), count / 3, count / 2, count - 2})
        {
            if (i >= count)
                continue;
            const uint64_t frame = _ttd->GetCheckpoint(i)->time.frame;
            for (size_t k = 1; k <= perFrame; ++k)
                out.push_back({frame, static_cast<uint32_t>(Span() * k / (perFrame + 1))});
        }
        return out;
    }
};

TEST_F(TTDEngineSeek_Test, LiveRecordingWithKeys_SeeksLandOnTheLiveMachine)
{
    // The classic GS card: with NeoGS no port journals are kept (its ZX-DMA is not isolated)
    ASSERT_NO_FATAL_FAILURE(StartMachine("PENTAGON", GSTypeKind::Z80, true));
    // A program whose memory follows the keyboard: DI; LD HL,#C000;
    // loop: LD A,#7F; IN A,(#FE); LD (HL),A; INC L; JR loop
    const uint8_t program[] = {0xF3, 0x21, 0x00, 0xC0, 0x3E, 0x7F, 0xDB, 0xFE, 0x77, 0x2C, 0x18, 0xF8};
    for (size_t i = 0; i < sizeof(program); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    _context->pCore->GetZ80()->pc = 0x8000;

    // Space pressed and released mid-frame; kept just before and after each
    ASSERT_NO_FATAL_FAILURE(RecordKeeping(24, 3, [&](int f) {
        if (f != 5 && f != 13)
            return;
        _emulator->RunTStates(19500, /*skipBreakpoints=*/true);
        KeepLive();
        _emulator->RunTStates(500, /*skipBreakpoints=*/true);
        ttd::TTDInputEvent key;
        key.kind = ttd::TTDInputKind::Key;
        key.key = ZXKEY_SPACE;
        key.pressed = f == 5;
        ASSERT_TRUE(_ttd->SubmitLiveInput(key));
        _emulator->RunTStates(1, /*skipBreakpoints=*/true);
        KeepLive();
        _emulator->RunTStates(900, /*skipBreakpoints=*/true);
        KeepLive();
    }));
    ASSERT_EQ(_live.size(), 18u);
    ASSERT_EQ(_ttd->GetInputJournal().Events().size(), 2u);

    ASSERT_NO_FATAL_FAILURE(ExpectEverySeekShowsLive());
    ASSERT_GT(_ttd->GetPortReadJournal().Size(), 1000u) << "port journals on";
    EXPECT_GT(_busPlayed, 0u) << "the replays read the engine's IN journal";
    EXPECT_EQ(Engine().BusReads().ValueMismatches(), 0u) << "the devices answered as recorded";
}

/// The recorded fixtures: a seek lands on the same machine whichever position it starts from (in order, then in
/// reverse), it reads the session's IN journal, and with the session's medium in the deck the live devices answer
/// as recorded: a tape restored mid-block plays on from there
TEST_F(TTDEngineSeek_Test, RecordedFixtures_SeeksLandOnTheSameMachineFromAnywhere)
{
    const fs::path root = TestPathHelper::FindProjectRoot() / "testdata/ttd/port-journals/engine";
    struct Fixture
    {
        const char* name;
        const char* tape;   ///< the medium the session played (a session does not carry it)
    };
    for (const Fixture& fixture : {Fixture{"dizzyx.ttd", nullptr}, Fixture{"greenberet-load.ttd", "greenberet.tap"}})
    {
        const char* name = fixture.name;
        SCOPED_TRACE(name);
        ttd::TTDFileInfo info;
        std::string err;
        ASSERT_TRUE(ttd::ReadTTDFileInfo((root / name).string(), info, err)) << err;
        ASSERT_NO_FATAL_FAILURE(StartMachine(info.machine.model, info.machine.generalSound, true));
        if (fixture.tape)
        {
            ASSERT_TRUE(_emulator->LoadTape(
                (TestPathHelper::FindProjectRoot() / "testdata/loaders/tap" / fixture.tape).string()));
        }
        std::ifstream in(root / name, std::ios::binary);
        ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;

        _busPlayed = 0;
        const std::vector<ttd::TTDTimePoint> targets = Targets(2);
        std::vector<MachineState> forward;
        for (const ttd::TTDTimePoint& target : targets)
        {
            ASSERT_NO_FATAL_FAILURE(Seek(target));
            forward.push_back(Capture());
        }
        for (size_t n = targets.size(); n-- > 0;)
        {
            SCOPED_TRACE("frame " + std::to_string(targets[n].frame) + " T " + std::to_string(targets[n].tInFrame));
            ASSERT_NO_FATAL_FAILURE(Seek(targets[n]));
            ExpectState(forward[n]);
        }
        EXPECT_GT(_busPlayed, 0u) << "the replays read the engine's IN journal";
        EXPECT_EQ(Engine().BusReads().ValueMismatches(), 0u) << "the engine's replays";
        StopMachine();
    }
}

/// A tool edit while recording (Emulator::EditMemoryFromTool, mid-frame):
/// the engine has its bytes and replays through it, landing on the machine
/// the recording had at the target
TEST_F(TTDEngineSeek_Test, AToolEditReplaysWithItsBytes)
{
    ASSERT_NO_FATAL_FAILURE(StartMachine("PENTAGON", GSTypeKind::Z80, true));
    // loop: LD A,(#C000); LD (#C001),A; OUT (#FE),A; JR loop
    const uint8_t program[] = {0xF3, 0x3A, 0x00, 0xC0, 0x32, 0x01, 0xC0, 0xD3, 0xFE, 0x18, 0xF6};
    for (size_t i = 0; i < sizeof(program); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    _context->pMemory->DirectWriteToZ80Memory(0xC000, 0x01);
    _context->pCore->GetZ80()->pc = 0x8000;

    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(5, /*skipBreakpoints=*/true);
    _emulator->RunTStates(20000, /*skipBreakpoints=*/true);
    _emulator->EditMemoryFromTool("test edit", [&] { _context->pMemory->ToolWriteToZ80Memory(0xC000, 0x5A); });
    _emulator->RunTStates(10000, /*skipBreakpoints=*/true);
    KeepLive();
    ASSERT_EQ(_context->pMemory->DirectReadFromZ80Memory(0xC001), 0x5A) << "the program copied the edited byte";
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    _ttd->StopRecording();

    ASSERT_NO_FATAL_FAILURE(ExpectEverySeekShowsLive()) << "the engine replays through the edit";
}

/// The media read journal (Phase 3, sector reads): a ZX-Evo program reads a
/// sector from its SD card while recording; the image file is then changed.
/// A seek still lands on the machine the original image gave (the card's
/// buffer, RAM, CPU), because the sector comes from the session; a live run
/// from the same start now reads the changed sector (the test can see one)
TEST_F(TTDEngineSeek_Test, SectorReadsComeFromTheSessionNotTheImage)
{
    ASSERT_NO_FATAL_FAILURE(StartMachine("ATM3", GSTypeKind::Z80, true));
    auto* decoder = dynamic_cast<PortDecoder_ATM3*>(_context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    const std::string image = TestPathHelper::GetUniqueTestScratchPath("ttd-media-reads.img");
    {
        std::ofstream out(image, std::ios::binary | std::ios::trunc);
        for (uint32_t i = 0; i < 64 * 512; ++i)
            out.put(static_cast<char>(i / 512 + i % 7));
    }
    ASSERT_TRUE(decoder->InsertSdCard(image, SdCardSpi::WriteMode::Session, false));
    _context->emulatorState.atm.aFF77 = PortDecoder_ATM3::ATM_AFF77_PEN | PortDecoder_ATM3::ATM_AFF77_CPM;
    _context->emulatorState.flags &= ~CF_TRDOS;
    _context->emulatorState.evo.pBF = 0x00;
    _context->pMemory->UpdateZ80Banks();
    decoder->DecodePortOut(0x0077, 0x00, 0);
    ASSERT_TRUE(zcsdtest::SdInit(decoder, 0x0057));

    // Wait about two frames, then CMD17 (byte address #400: sector 2) through
    // #57 and the 512 data bytes into #C000
    const uint8_t program[] = {
        0xF3, 0x11, 0x00, 0x16, 0x1B, 0x7A, 0xB3, 0x20, 0xFB,                  // DI; LD DE,#1600; delay
        0x01, 0x57, 0x00, 0x21, 0x3B, 0x80, 0x1E, 0x06,                        // LD BC,#57; LD HL,cmd; LD E,6
        0x7E, 0xED, 0x79, 0x23, 0x1D, 0x20, 0xF9,                              // send the command
        0xED, 0x78,                                                            // NCR
        0xED, 0x78, 0xFE, 0xFF, 0x28, 0xFA,                                    // R1
        0xED, 0x78, 0xFE, 0xFE, 0x20, 0xFA,                                    // data token
        0x21, 0x00, 0xC0, 0x11, 0x00, 0x02,                                    // LD HL,#C000; LD DE,512
        0xED, 0x78, 0x77, 0x23, 0x1B, 0x7A, 0xB3, 0x20, 0xF7,                  // the 512 bytes
        0xED, 0x78, 0xED, 0x78, 0x18, 0xFE,                                    // CRC; spin
        0x51, 0x00, 0x00, 0x04, 0x00, 0xFF};                                   // cmd at #803B
    for (size_t i = 0; i < sizeof(program); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    _context->pCore->GetZ80()->pc = 0x8000;

    // Points inside every frame but the first and the last (1-4), with the image intact
    ASSERT_NO_FATAL_FAILURE(RecordKeeping(6, 3));
    ASSERT_GT(Engine().MediaReads().Size(), 0u) << "the card read a sector while recording";
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0xC000), 2 + (2 * 512) % 7) << "sector 2's first byte";

    // The image changes on disk
    {
        std::fstream f(image, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(2 * 512);
        for (int i = 0; i < 512; ++i)
            f.put(static_cast<char>(0xE5));
    }
    ASSERT_NO_FATAL_FAILURE(ExpectEverySeekShowsLive());

    // From the session's start, live: the card reads the image again
    ASSERT_TRUE(_ttd->ResumeRecordingFrom(_ttd->GetCheckpoint(0)->time));
    _emulator->RunNFrames(6, /*skipBreakpoints=*/true);
    _ttd->StopRecording();
#ifdef _WIN32
    // The Windows C runtime answers a seek inside the buffer of a stream opened for reading from that buffer: the
    // image the card holds open does not show a change made behind its back. What a seek restores is checked above
    // on every platform
#else
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0xC000), 0xE5) << "a live run reads the changed image";
#endif
    std::remove(image.c_str());
}

/// Machines whose outside world reaches the CPU not only through IN: the
/// engine has their bus data, vectors and media reads, and its seeks land on
/// the live machine. Each boots its ROM under recording; the shipped
/// configuration fits its cards (Pentagon: NeoGS)
class TTDEngineSeekModels_Test : public TTDEngineSeek_Test, public ::testing::WithParamInterface<const char*>
{
};

TEST_P(TTDEngineSeekModels_Test, SeeksLandOnTheLiveMachine)
{
    ASSERT_NO_FATAL_FAILURE(StartMachine(GetParam(), GSTypeKind::Z80, false));
    ASSERT_NO_FATAL_FAILURE(RecordKeeping(150, 2));
    if (std::string(GetParam()) != "SPRINTER")   // its BIOS reads no port in its first 150 frames
        EXPECT_GT(Engine().BusReads().Size(), 0u) << "every IN, on every machine";

    ASSERT_NO_FATAL_FAILURE(ExpectEverySeekShowsLive());
    if (Engine().BusReads().Size() > 0)
        EXPECT_GT(_busPlayed, 0u) << "the replays read the engine's IN journal";
}

/// The interrupt-vector journal: a TS-Conf program in IM 2 (its own vector
/// table) takes an interrupt every frame; each vector the CPU took is
/// recorded, and seeks (vectors handed back) land on the live machine
TEST_F(TTDEngineSeek_Test, InterruptVectorsAreRecordedAndPlayedBack)
{
    ASSERT_NO_FATAL_FAILURE(StartMachine("TSL", GSTypeKind::Z80, false));
    // DI; LD A,#81; LD I,A; IM 2; EI; loop: HALT; INC (IX+0)... kept simple:
    // loop: HALT; JR loop. Table #8100-#8200 = #82, ISR at #8282: EI; RETI
    const uint8_t program[] = {0xF3, 0x3E, 0x81, 0xED, 0x47, 0xED, 0x5E, 0xFB, 0x76, 0x18, 0xFD};
    for (size_t i = 0; i < sizeof(program); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    for (uint16_t a = 0x8100; a <= 0x8200; ++a)
        _context->pMemory->DirectWriteToZ80Memory(a, 0x82);
    const uint8_t isr[] = {0xFB, 0xED, 0x4D};
    for (size_t i = 0; i < sizeof(isr); ++i)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8282 + i), isr[i]);
    _context->pCore->GetZ80()->pc = 0x8000;

    ASSERT_NO_FATAL_FAILURE(RecordKeeping(20, 2));
    ASSERT_GE(Engine().BusVectors().Size(), 15u) << "about one interrupt per frame";
    ttd::TTDPortRecord r;
    ASSERT_TRUE(Engine().BusVectors().Get(0, r));
    EXPECT_EQ(r.port, Z80::kTtdVectorPort);

    ASSERT_NO_FATAL_FAILURE(ExpectEverySeekShowsLive());
    EXPECT_GT(Engine().BusVectors().Cursor(), 0u) << "the replays took their vectors from the engine";
}

/// Phase 3, Step 4 (FR-14): the session keeps the settings it was recorded
/// with. Seeking on a machine set differently - another decimator, another
/// ROM set - is not refused: a frame-aligned restore is exact, a seek inside a
/// frame (a replay) runs and reports NotBitExact naming exactly those settings
TEST_F(TTDEngineSeek_Test, AnotherConfigurationIsReportedNotRefused)
{
    ASSERT_NO_FATAL_FAILURE(StartMachine("PENTAGON", GSTypeKind::Z80, false));
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(8, /*skipBreakpoints=*/true);
    _ttd->StopRecording();
    ASSERT_EQ(Engine().Configurations().size(), 1u) << "the settings did not change while recording";
    const ttd::TTDConfigFingerprint& recorded = Engine().Configurations()[0].fingerprint;
    ASSERT_NE(recorded.Find("rom.signature"), nullptr);
    ASSERT_NE(recorded.Find("timing.frame"), nullptr);

    const uint64_t frame = _ttd->GetCheckpoint(3)->time.frame;
    const uint32_t mid = static_cast<uint32_t>(_ttd->FrameSpan() / 2);

    // The same settings: nothing to report
    ASSERT_TRUE(_ttd->SeekTo({frame, mid}));
    EXPECT_EQ(_ttd->LastEngineCheck().status, ttd::TTDRestoreStatus::Exact) << _ttd->LastEngineCheck().message;

    // Another decimator, and one byte of the ROM set changed (past the pages
    // the machine runs: the replay itself stays the same)
    CONFIG& config = _context->config;
    config.sound.decimatorHighFidelity = !config.sound.decimatorHighFidelity;
    uint8_t* romEnd = _context->pMemory->ROMBase() + size_t(MAX_ROM_PAGES) * PAGE_SIZE - 1;
    *romEnd ^= 0xFF;
    _ttd->SetReplaySource(nullptr);   // the machine's ROM set is read when the replay source is set (D39)

    ASSERT_TRUE(_ttd->SeekTo({frame, mid})) << "a replay runs on other settings";
    const ttd::TTDRestoreResult check = _ttd->LastEngineCheck();
    ttd::TTDSeekResult aligned;
    ASSERT_TRUE(_ttd->SeekTo(_ttd->GetCheckpoint(3)->time, &aligned));   // the checkpoint itself: a restore
    const ttd::TTDRestoreResult alignedCheck = _ttd->LastEngineCheck();
    *romEnd ^= 0xFF;
    config.sound.decimatorHighFidelity = !config.sound.decimatorHighFidelity;
    _ttd->SetReplaySource(nullptr);

    EXPECT_EQ(check.status, ttd::TTDRestoreStatus::NotBitExact) << check.message;
    std::vector<std::string> named;
    for (const ttd::TTDRestoreIssue& issue : check.issues)
    {
        EXPECT_EQ(issue.kind, ttd::TTDRestoreIssueKind::ConfigurationDiffers);
        named.push_back(issue.detail.substr(0, issue.detail.find(':')));
    }
    EXPECT_EQ(named, (std::vector<std::string>{"sound.decimator_high_fidelity", "rom.signature"}))
        << "exactly the settings that differ";
    EXPECT_EQ(alignedCheck.status, ttd::TTDRestoreStatus::Exact)
        << "a frame-aligned restore does not depend on them: " << alignedCheck.message;
}

/// Phase 3, Step 4: a setting changed while recording is a ConfigChange cut
/// at the next frame boundary, with the new settings from there on
TEST_F(TTDEngineSeek_Test, ASettingChangedWhileRecordingIsACut)
{
    ASSERT_NO_FATAL_FAILURE(StartMachine("PENTAGON", GSTypeKind::Z80, false));
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    CONFIG& config = _context->config;
    config.sound.decimatorHighFidelity = !config.sound.decimatorHighFidelity;
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    config.sound.decimatorHighFidelity = !config.sound.decimatorHighFidelity;
    _ttd->StopRecording();

    ASSERT_EQ(Engine().Configurations().size(), 2u);
    const auto diffs = ttd::Compare(Engine().Configurations()[0].fingerprint, Engine().Configurations()[1].fingerprint);
    ASSERT_EQ(diffs.size(), 1u);
    EXPECT_EQ(diffs[0].field, "sound.decimator_high_fidelity");
    size_t cuts = 0;
    for (const ttd::TTDEvent& ev : Engine().Events().Events())
        cuts += ev.kind == ttd::TTDEventKind::ConfigChange ? 1 : 0;
    EXPECT_EQ(cuts, 1u);
    const uint64_t changedAt = Engine().Configurations()[1].frame;
    for (size_t i = 0; i < Engine().CheckpointCount(); ++i)
        EXPECT_TRUE(*Engine().ConfigurationAt(i) ==
                    Engine().Configurations()[Engine().Checkpoint(i)->position.frame < changedAt ? 0 : 1].fingerprint)
            << "checkpoint " << i;
}

/// Phase 3, Step 4 and the media undo log: a medium written after a checkpoint
/// goes back with the seek. A replay from before the write sets the medium's
/// head to the recorded version and is exact; from after the write nothing
/// differs either. (A write beyond what the undo log keeps is named instead.)
TEST_F(TTDEngineSeek_Test, AMediumWrittenSinceTheCheckpointGoesBack)
{
    ASSERT_NO_FATAL_FAILURE(StartMachine("ATM3", GSTypeKind::Z80, true));
    auto* decoder = dynamic_cast<PortDecoder_ATM3*>(_context->pPortDecoder);
    ASSERT_NE(decoder, nullptr);
    const std::string image = TestPathHelper::GetUniqueTestScratchPath("ttd-media-versions.img");
    {
        std::ofstream out(image, std::ios::binary | std::ios::trunc);
        out << std::string(64 * 512, '\0');
    }
    ASSERT_TRUE(decoder->InsertSdCard(image, SdCardSpi::WriteMode::Session, false));

    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    // The card's controller writes the medium in frame 5 (what a sector write reports)
    std::string slot;
    for (const ttd::TTDMediaSlot& s : Engine().MediaSlots())
    {
        ttd::TTDMediaVersion v;
        if (Engine().MediaVersionAt(0, &s - Engine().MediaSlots().data(), v) && v.contentId != 0)
            slot = s.slot;
    }
    ASSERT_FALSE(slot.empty()) << "the inserted card is in the session's media table";
    _context->pMediaManager->NoteWrite(slot, "test write");
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    _ttd->StopRecording();

    auto headOf = [&]() {
        std::vector<MediaVersionInfo> live;
        _context->pMediaManager->CurrentVersions(live);
        for (const MediaVersionInfo& v : live)
            if (v.slot == slot)
                return v.version;
        return uint64_t(UINT64_MAX);
    };
    const size_t slotIndex = static_cast<size_t>(
        std::find_if(Engine().MediaSlots().begin(), Engine().MediaSlots().end(),
                     [&](const ttd::TTDMediaSlot& s) { return s.slot == slot; }) -
        Engine().MediaSlots().begin());
    const uint64_t written = headOf();
    ttd::TTDMediaVersion recorded;
    ASSERT_TRUE(Engine().MediaVersionAt(1, slotIndex, recorded));
    ASSERT_NE(recorded.version, written) << "precondition: the write is a new version";

    const uint32_t mid = static_cast<uint32_t>(_ttd->FrameSpan() / 2);
    ASSERT_TRUE(_ttd->SeekTo({_ttd->GetCheckpoint(1)->time.frame, mid})) << "no barrier at the write";
    const ttd::TTDRestoreResult before = _ttd->LastEngineCheck();
    EXPECT_EQ(headOf(), recorded.version) << "the medium went back to the recorded version";
    ASSERT_TRUE(_ttd->SeekTo({_ttd->GetCheckpoint(_ttd->GetCheckpointCount() - 2)->time.frame, mid}));
    const ttd::TTDRestoreResult after = _ttd->LastEngineCheck();
    EXPECT_EQ(headOf(), written) << "and forward again";

    EXPECT_EQ(before.status, ttd::TTDRestoreStatus::Exact) << before.message;
    EXPECT_EQ(after.status, ttd::TTDRestoreStatus::Exact) << after.message;
    std::remove(image.c_str());
}

INSTANTIATE_TEST_SUITE_P(Machines, TTDEngineSeekModels_Test,
                         ::testing::Values("PENTAGON", "TSL", "SPRINTER", "SCORPION", "PROFI", "ATM3"),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string(info.param); });
