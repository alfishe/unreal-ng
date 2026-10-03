// Sprinter Sp2000 time travel (Sprinter phase S7; debugger/ttd/sprinter/ttdsprinter.h).
//
// Two layers:
//   - TTDSprinter_Test: every Sprinter serializer round-trips its component on the synthetic
//     fixture machine (SprinterFixture): save, scramble the live state, load, save again - the
//     same bytes, and the live fields back;
//   - TTDSprinterMachine_Test: the real BIOS 3.04 under a TTD recording. A recording is the
//     uninterrupted run; a seek back to a checkpoint followed by running forward must arrive at
//     every later checkpoint with the same CPU, chipset, device blobs, RAM and picture. The
//     checkpoints the replays start from are chosen at awkward points: in the middle of the
//     PLD's configuration load, of a floppy sector, of an IDE sector, of a PS/2 byte on the
//     keyboard wire and of a serial-mouse packet.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "3rdparty/z84c15/z84c15.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/mouse/debugmousemanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/sprinter/ttdsprinter.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/ttdwd1793context.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/ide/ata/atachannel.h"
#include "emulator/io/ide/ata/atadevice.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/io/mouse/mousemanager.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/sprinter/sprintermemory.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/video/screen.h"
#include "emulator/video/sprinter/screensprinter.h"
#include "emulator/machines/sprinter/sprinterdssmedia.h"
#include "emulator/machines/sprinter/sprinterfixture.h"

namespace
{
std::vector<uint8_t> Save(const ttd::TTDSerializable& s)
{
    std::vector<uint8_t> blob(s.TTDStateSize());
    s.TTDSaveState(blob.data());
    return blob;
}

/// A PLD configuration module with 4 bytes of state of its own (T-PLDM-3)
class StateModule : public SprinterPldConfiguration
{
public:
    StateModule() { _descriptor.name = "TtdStateModule"; }
    const SprinterPldModuleDescriptor& Descriptor() const override { return _descriptor; }
    size_t StateSize() const override { return sizeof(state); }
    void SaveState(uint8_t* dst) const override { std::memcpy(dst, state, sizeof(state)); }
    void LoadState(const uint8_t* src) override { std::memcpy(state, src, sizeof(state)); }

    uint8_t state[4] = {};

private:
    SprinterPldModuleDescriptor _descriptor;
};
}  // namespace

/// region <Serializer round-trips (the synthetic machine)>

class TTDSprinter_Test : public SprinterFixture
{
};

TEST_F(TTDSprinter_Test, Pld_RoundTripsThePldTheDecoderAndTheIntSource)
{
    // Every field of SprinterPldState set to a value of its own, the decoder's latches, the INT source,
    // the WD1793's rate-retry search
    SprinterPldState& pld = Pld();
    for (size_t i = 0; i < sizeof(pld.cells); i++)
        pld.cells[i] = static_cast<uint8_t>(0x37 * i + 1);
    pld.romRg = 0x15;
    pld.portY = 0x42;
    pld.rgMod = 0x01;
    pld.hold = 0x34;
    pld.fdcHd = 1;
    pld.bitstreamCount = 0x00012345;
    pld.bitstreamHashFull = 0xDEADBEEF;
    pld.loadWatchdog = 77;
    _decoder->StandardWriteCode(SprinterCode::CovoxBlaster, 0x89, 0x5A);
    _decoder->GetIntSource().RestoreState(1, 312, 0x123456789ALL, true);

    // The accelerator: a mode armed, a length, a buffer, blocked by an INT acknowledge
    SprinterAccelState& acc = _decoder->StandardAccelerator().State();
    for (size_t i = 0; i < sizeof(acc.buffer); i++)
        acc.buffer[i] = static_cast<uint8_t>(i ^ 0x5A);
    acc.mode = 5;
    acc.dir = SprinterAccelerator::kDir[5];
    acc.length = 16;
    acc.fn = 2;
    acc.blocked = 1;
    acc.aagr = 0x2AB;
    acc.operations = 1234;

    ttd::TTDSprinterPld serializer(*_decoder);
    EXPECT_EQ(serializer.TTDStateSize(), ttd::TTDSprinterPld::kFixedSize + 2 + sizeof(SprinterAccelState))
        << "Standard brings no module state; the accelerator section";
    const std::vector<uint8_t> saved = Save(serializer);
    EXPECT_EQ(saved[0], ttd::TTDSprinterPld::kVersion);
    const SprinterPldState pldBefore = pld;
    const SprinterAccelState accBefore = acc;

    // Power-on scrambles all of it
    _decoder->PowerCycle();
    std::memset(&acc, 0, sizeof(acc));
    _decoder->GetIntSource().RestoreState(0, 320, -1, false);
    ASSERT_NE(Save(serializer), saved);

    serializer.TTDLoadState(saved.data());
    EXPECT_EQ(Save(serializer), saved);
    EXPECT_EQ(std::memcmp(&pld, &pldBefore, sizeof(pld)), 0);
    EXPECT_EQ(_decoder->CblControl(), 0x5A);
    EXPECT_EQ(_decoder->GetIntSource().ModePage(), 1);
    EXPECT_EQ(_decoder->GetIntSource().FrameLines(), 312);
    EXPECT_EQ(_decoder->GetIntSource().AckedPulse(), 0x123456789ALL);
    EXPECT_TRUE(_decoder->GetIntSource().KeyboardIntLatched());
    EXPECT_EQ(std::memcmp(&acc, &accBefore, sizeof(acc)), 0) << "the accelerator";
    EXPECT_TRUE(_decoder->StandardAccelerator().watchData) << "a mode on: the engine watches data accesses";
}

TEST_F(TTDSprinter_Test, Wd1793Context_RoundTripsTheRateRetrySearch)
{
    WD1793* fdc = _context->pBetaDisk;
    ASSERT_NE(fdc, nullptr);
    fdc->RestoreRateRetry(9, 0x0102030405060708ULL);
    ttd::TTDWd1793Context serializer(*fdc);
    EXPECT_EQ(serializer.TTDStateSize(), 1u + WD1793::kTransferContextSize);
    const std::vector<uint8_t> saved = Save(serializer);
    fdc->RestoreRateRetry(0, 0);
    serializer.TTDLoadState(saved.data());
    EXPECT_EQ(Save(serializer), saved);
    EXPECT_EQ(fdc->GetRateRetryState(), 9);
    EXPECT_EQ(fdc->GetRateRetryDeadline(), 0x0102030405060708ULL);
}

TEST_F(TTDSprinter_Test, Pld_AnotherVersionIsNotLoaded)
{
    ttd::TTDSprinterPld serializer(*_decoder);
    std::vector<uint8_t> blob = Save(serializer);
    Pld().portY = 0x11;
    const std::vector<uint8_t> live = Save(serializer);
    blob[0] = ttd::TTDSprinterPld::kVersion + 1;
    serializer.TTDLoadState(blob.data());
    EXPECT_EQ(Save(serializer), live) << "a blob of another layout must leave the device as it is";
}

TEST_F(TTDSprinter_Test, Pld_TheModuleTravelsByNameWithItsState)
{
    auto module = std::make_unique<StateModule>();
    StateModule* stub = module.get();
    const size_t index = _decoder->GetRegistry().Register(std::move(module));
    Pld().configModule = static_cast<uint8_t>(index);
    std::memcpy(stub->state, "\x11\x22\x33\x44", 4);

    ttd::TTDSprinterPld serializer(*_decoder);
    EXPECT_EQ(serializer.TTDStateSize(), ttd::TTDSprinterPld::kFixedSize + 4 + 2 + sizeof(SprinterAccelState))
        << "room for the largest module state";
    const std::vector<uint8_t> saved = Save(serializer);
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(saved.data()) + 139), "TtdStateModule");

    Pld().configModule = 0;
    std::memset(stub->state, 0, 4);
    serializer.TTDLoadState(saved.data());
    EXPECT_EQ(Pld().configModule, index);
    EXPECT_EQ(std::memcmp(stub->state, "\x11\x22\x33\x44", 4), 0);
    EXPECT_EQ(Save(serializer), saved);
}

TEST_F(TTDSprinter_Test, Z84_RoundTripsTheChipBesideItsRegisterFile)
{
    Z84Lib::Z84C15& chip = _decoder->GetZ84();
    chip.PowerOn();  // the power-on wait window open: WCR reads #FF for 15 M1 cycles
    chip.Write(0xEE, 0x01);
    chip.Write(0xEF, 0x8F);  // MWBR
    chip.Write(0xEE, 0x02);
    chip.Write(0xEF, 0x3E);  // CSBR
    chip.Write(0xF4, 0x03);  // daisy-chain order
    chip.Write(0x10, 0xA5);  // CTC 0: timer, prescaler 256, interrupt, constant follows
    chip.Write(0x10, 0x0A);
    chip.Write(0x10, 0x40);  // the vector
    chip.Write(0x19, 0x01);  // SIO A WR1: Rx INT on every character
    chip.Write(0x19, 0x18);
    chip.sio.Receive(0, 0x1C);  // two bytes in SIO A's FIFO, one in B's
    chip.sio.Receive(0, 0xF0);
    chip.sio.Receive(1, 0x40);
    chip.Write(0x1D, 0xCF);  // PIO A mode 3, all outputs, a POST code
    chip.Write(0x1D, 0x00);
    chip.Write(0x1C, 0xEA);
    chip.Write(0xF0, 0xFB);  // watchdog on

    ttd::TTDSprinterZ84 serializer(*_decoder);
    EXPECT_EQ(serializer.TTDStateSize(), 1u + Z84Lib::Z84C15::kStateSize);
    const std::vector<uint8_t> saved = Save(serializer);
    chip.Write(0xEE, 0x00);
    EXPECT_EQ(chip.Read(0xEF), 0xFF) << "WCR reads #FF inside the power-on window";

    // Programmed waits off, the FIFOs drained, everything reset
    chip.Write(0xEF, 0x00);
    chip.Reset();
    ASSERT_NE(Save(serializer), saved);

    serializer.TTDLoadState(saved.data());
    EXPECT_EQ(Save(serializer), saved);
    chip.Write(0xEE, 0x00);
    EXPECT_EQ(chip.Read(0xEF), 0xFF) << "the power-on window counter came back";
    EXPECT_EQ(chip.sio.GetChannel(0).fifoCount, 2);
    EXPECT_EQ(chip.Read(0x18), 0x1C) << "SIO A's FIFO came back in order";
    EXPECT_EQ(chip.Read(0x1A), 0x40);
    EXPECT_EQ(chip.Read(0x1C), 0xEA);
    EXPECT_EQ(chip.ctc.Vector(), 0x40);
    EXPECT_TRUE(chip.WatchdogRunning());
}

TEST_F(TTDSprinter_Test, Input_RoundTripsTheKeyboardWireAndTheMousePacket)
{
    SprinterInput& input = _decoder->GetInput();
    ASSERT_NE(_context->pMouseManager, nullptr);

    // A key: its make code is on the wire; the mouse moved: a packet started at the SIO B access
    input.OnPcKey(PcKey::Up, true);  // E0 75
    _context->pMouseManager->ApplyMotion(5, -3);
    input.BeforeChipAccess(0x1B);  // first sample: the reference
    _context->pMouseManager->ApplyMotion(7, 2);
    _context->pMouseManager->ApplyButtons(0xFD);
    input.BeforeChipAccess(0x1B);  // the move: a packet in flight
    ASSERT_TRUE(input.KeyboardStream().Busy());
    ASSERT_LT(input.SerialMouse().GetState().sent, 3);
    input.SetKeyboardOverruns(3);

    ttd::TTDSprinterInput serializer(*_decoder);
    EXPECT_EQ(serializer.TTDStateSize(), ttd::TTDSprinterInput::kSize);
    const std::vector<uint8_t> saved = Save(serializer);

    input.Clear();
    input.SetBoardMouse({0, 0, 0xFF});
    ASSERT_NE(Save(serializer), saved);
    serializer.TTDLoadState(saved.data());
    EXPECT_EQ(Save(serializer), saved);
    EXPECT_TRUE(input.KeyboardStream().IsHeld(PcKey::Up));
    EXPECT_EQ(input.KeyboardStream().GetState().count, 2) << "E0 75 still on the way";
    EXPECT_EQ(input.SerialMouse().GetState().sent, 0) << "the packet resumes at its first byte";
    EXPECT_EQ(input.KeyboardOverruns(), 3u);
    const SprinterInput::BoardMouse board = input.GetBoardMouse();
    EXPECT_EQ(board.x, 31 + 12) << "the board mouse counters are in the blob";
    EXPECT_EQ(board.y, 85 - 1);
    EXPECT_EQ(board.buttons, 0xFD);
}

TEST_F(TTDSprinter_Test, VideoRam_RoundTripsWithThePaletteAndTheIntList)
{
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    vram.Write(SprinterVideoRam::PenAddress(0x401), 0xA8);  // text paper 1: red #A8
    for (uint8_t a = 40; a < 46; a++)
        vram.Write(SprinterVideoRam::ModeAddress(a, 30, 0), 0xFD);  // blank + INT squares
    vram.Write(0x12345, 0x77);
    const std::vector<uint32_t> positions = _decoder->GetIntSource().Positions();
    ASSERT_FALSE(positions.empty());

    ttd::TTDSprinterVideoRam serializer(*_decoder);
    EXPECT_EQ(serializer.TTDStateSize(), 1u + SprinterVideoRam::kSize);
    const std::vector<uint8_t> saved = Save(serializer);

    vram.Clear();
    _decoder->GetIntSource().Invalidate();
    ASSERT_TRUE(_decoder->GetIntSource().Positions().empty());

    serializer.TTDLoadState(saved.data());
    EXPECT_EQ(Save(serializer), saved);
    EXPECT_EQ(vram.Pen(0x401), 0xFF0000A8u) << "the pen cache is rebuilt from the palette bytes";
    EXPECT_EQ(_decoder->GetIntSource().Positions(), positions) << "the INT list follows the mode table";
}

TEST_F(TTDSprinter_Test, FastRam_RoundTrips)
{
    uint8_t* fast = _sprinterMemory->FastRam();
    for (size_t i = 0; i < ttd::TTDSprinterFastRam::kFastRamSize; i++)
        fast[i] = static_cast<uint8_t>(i * 13 + (i >> 9));
    ttd::TTDSprinterFastRam serializer(*_decoder);
    const std::vector<uint8_t> saved = Save(serializer);
    std::memset(fast, 0, ttd::TTDSprinterFastRam::kFastRamSize);
    serializer.TTDLoadState(saved.data());
    EXPECT_EQ(Save(serializer), saved);
    EXPECT_EQ(fast[0x1234], static_cast<uint8_t>(0x1234 * 13 + (0x1234 >> 9)));
}

/// Through the registry, as a checkpoint carries them: every declared id has a blob, and a
/// restore of all of them is complete
TEST_F(TTDSprinter_Test, Registry_EveryDeclaredIdTravels)
{
    ttd::TTDPeripheralRegistry registry;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> owned = _decoder->CreateTTDSerializers();
    for (const auto& s : owned)
        registry.Register(s->TTDPeripheralId(), s.get());
    std::unordered_map<uint8_t, std::vector<uint8_t>> blobs;
    registry.CaptureAll(blobs);
    for (ttd::PeripheralId id : _decoder->GetTTDModelStateIds())
        EXPECT_EQ(blobs.count(static_cast<uint8_t>(id)), 1u) << "id " << int(id);
    const ttd::TTDRestoreReport report = registry.RestoreAll(blobs);
    EXPECT_TRUE(report.Complete());
    EXPECT_EQ(report.restored, owned.size());
}

/// endregion </Serializer round-trips>

/// region <Exact restore on the real BIOS>

/// The real machine (BIOS 3.04, the shipped config) with TTD on. Not in turbo mode: the
/// picture of every frame is compared
class TTDSprinterMachine_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    Z80* _z80 = nullptr;

    /// The recorded run: the picture at every frame boundary, by frame
    std::map<uint64_t, uint64_t> _screens;

    void SetUp() override
    {
        if (!SprinterFixture::Rom304Available())
            GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";
        _manager = EmulatorManager::GetInstance();
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
        _emulator = _manager->CreateEmulatorWithModelAndRAM("sprinter-ttd", "SPRINTER", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _z80 = _context->pCore->GetZ80();
        _decoder->GetRtc().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC

        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        // The picture drawn whole at each frame end (cheaper than the beam-exact catch-up, and as exact for a
        // comparison of one run against another); the low-quality sound path
        features->setFeature(Features::kScreenHQ, false);
        features->setFeature(Features::kSoundHQ, false);
        _context->pMemory->UpdateFeatureCache();
        _context->pSoundManager->UpdateFeatureCache();
    }

    void TearDown() override
    {
        _emulator.reset();
        if (_manager)
        {
            for (const auto& id : _manager->GetEmulatorIds())
                _manager->RemoveEmulator(id);
        }
    }

    void PowerOn(bool fastStart)
    {
        _context->config.sprinter.fast_start = fastStart ? 1 : 0;
        _emulator->Reset();
    }

    uint64_t Frame() const { return _context->emulatorState.frame_counter; }

    /// Run to the next frame boundary exactly (the frame's checkpoint is taken there)
    void RunToBoundary()
    {
        // RunTStates stops once the clock reaches the frame end; the frame closes with the instruction that crosses it
        const uint64_t frame = Frame();
        for (int guard = 0; guard < 4 && Frame() == frame; guard++)
            _emulator->RunTStates(_z80->t < _z80->_frameLimit ? _z80->_frameLimit - _z80->t : 1u, true);
        ASSERT_EQ(Frame(), frame + 1);
    }

    /// Frames without recording, the turbo mode on (nothing compared)
    void Skip(int frames)
    {
        _emulator->EnableTurboMode();
        _emulator->RunNFrames(static_cast<unsigned>(frames), true);
        _emulator->DisableTurboMode();
        RunToBoundary();
    }

    uint64_t ScreenHash() const
    {
        uint32_t* fb = nullptr;
        size_t size = 0;
        _context->pScreen->GetFramebufferData(&fb, &size);
        return fb ? ttd::HashBytes(reinterpret_cast<const uint8_t*>(fb), size) : 0;
    }

    std::string ScreenText() const
    {
        const SprinterVideoRam& vram = _decoder->GetVideoRam();
        std::string text;
        for (uint8_t page = 0; page < 2; page++)
            for (uint8_t b = 0; b < 32; b++)
            {
                for (uint8_t a = 0; a < 40; a++)
                    for (uint8_t half = 0; half < 2; half++)
                    {
                        const uint8_t c = vram.Read((1u + 2u * a + half + 0x80u * page) * 1024u + 0x301 + 4u * b);
                        text.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ');
                    }
                text.push_back('\n');
            }
        return text;
    }
    bool ScreenHas(const std::string& needle) const { return ScreenText().find(needle) != std::string::npos; }

    /// Start a recording at this boundary
    void StartRecording()
    {
        _screens.clear();
        ASSERT_TRUE(_ttd->StartRecording()) << "TTD refuses to record the Sprinter";
        _screens[Frame()] = ScreenHash();
    }

    /// Record `frames` frames; `beforeFrame(i)` may give input (inside the frame too, with RunTStates)
    /// and `atBoundary()` looks at every boundary
    void Record(int frames, const std::function<void(int)>& beforeFrame = {}, const std::function<void()>& atBoundary = {})
    {
        for (int i = 0; i < frames && !HasFatalFailure(); i++)
        {
            if (beforeFrame)
                beforeFrame(i);
            RunToBoundary();
            _screens[Frame()] = ScreenHash();
            if (atBoundary)
                atBoundary();
        }
    }

    size_t IndexOfFrame(uint64_t frame) const
    {
        for (size_t i = 0; i < _ttd->GetCheckpointCount(); i++)
            if (_ttd->GetCheckpoint(i)->time.frame == frame)
                return i;
        return SIZE_MAX;
    }

    /// The live machine against checkpoint `idx` of the recording: CPU, chipset, every device blob
    /// (decoded), every RAM sub-page it holds, and the picture of that frame
    void ExpectLiveMatchesCheckpoint(size_t idx, const std::string& where, bool allRam = false, bool picture = true)
    {
        const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(idx);
        ASSERT_NE(cp, nullptr);
        ASSERT_EQ(Frame(), cp->time.frame) << where;

        const ttd::TTDCpuState cpu = ttd::CaptureCpuState(*static_cast<const Z80State*>(_z80));
        EXPECT_EQ(std::memcmp(&cpu, &cp->cpu, sizeof(cpu)), 0)
            << where << ": CPU differs (PC " << std::hex << cpu.pc << " vs " << cp->cpu.pc << ")";
        const ttd::TTDChipsetState chipset = ttd::CaptureChipsetState(_context->emulatorState, static_cast<uint32_t>(_z80->t));
        EXPECT_EQ(std::memcmp(&chipset, &cp->chipset, sizeof(chipset)), 0)
            << where << ": chipset differs (t " << std::dec << ttd::GetChipsetCpuTInFrame(chipset) << " vs "
            << ttd::GetChipsetCpuTInFrame(cp->chipset) << ")";

        std::unordered_map<uint8_t, std::vector<uint8_t>> live;
        _ttd->GetPeripheralRegistry().CaptureAll(live);
        EXPECT_EQ(live.size(), cp->peripheralBlobs.size()) << where << ": device sets differ";
        for (const auto& [id, blob] : cp->peripheralBlobs)
        {
            const auto it = live.find(id);
            ASSERT_NE(it, live.end()) << where << ": device " << int(id) << " missing";
            const std::vector<uint8_t> expected = ttd::TTDPeripheralRegistry::DecodeBlob(id, blob);
            const std::vector<uint8_t> actual = ttd::TTDPeripheralRegistry::DecodeBlob(id, it->second);
            ASSERT_EQ(actual.size(), expected.size()) << where << ": device " << int(id);
            size_t first = 0;
            while (first < expected.size() && actual[first] == expected[first])
                first++;
            EXPECT_EQ(first, expected.size()) << where << ": device " << int(id) << " differs from byte " << first;
        }

        // RAM: the sub-pages this checkpoint stored anew (all of them on request: 4 MB to decode)
        const ttd::TTDCheckpoint* prev = idx > 0 && !allRam ? _ttd->GetCheckpoint(idx - 1) : nullptr;
        std::vector<uint8_t> page(4096);
        for (size_t p = 0; p < cp->ramPages.size(); p++)
            for (uint32_t sub = 0; sub < 4; sub++)
            {
                const uint32_t slot = cp->ramPages[p].pageSlots[sub];
                if (slot == ttd::TTDPageRef::kNeverTouched)
                    continue;
                if (prev && p < prev->ramPages.size() && prev->ramPages[p].pageSlots[sub] == slot)
                    continue;
                ASSERT_TRUE(_ttd->GetPageStore().GetPage(slot, page.data()));
                const uint8_t* ram = _context->pMemory->RAMPageAddress(static_cast<uint16_t>(p)) + sub * 4096;
                ASSERT_EQ(std::memcmp(ram, page.data(), 4096), 0) << where << ": RAM page " << p << " sub-page " << sub;
            }

        // The picture of the frame that ended here (a seek itself draws nothing into the framebuffer)
        const auto screen = _screens.find(cp->time.frame);
        if (picture && screen != _screens.end())
            EXPECT_EQ(ScreenHash(), screen->second) << where << ": the picture differs";
    }

    /// Seek to checkpoint `from` and run forward through `frames` recorded frames (the journal
    /// plays the input): every boundary must be the recorded one
    void ExpectExactReplay(size_t from, size_t frames, const std::string& what)
    {
        const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(from);
        ASSERT_NE(cp, nullptr) << what;
        ASSERT_TRUE(_ttd->SeekTo({cp->time.frame, 0})) << what;
        ExpectLiveMatchesCheckpoint(from, what + ": the restore", true, false);
        const size_t last = std::min(_ttd->GetCheckpointCount() - 1, from + frames);
        for (size_t idx = from + 1; idx <= last && !HasFailure(); idx++)
        {
            RunToBoundary();
            ExpectLiveMatchesCheckpoint(idx, what + ": frame " + std::to_string(idx - from) + " after the restore", idx == last);
        }
    }

    /// The decoded blob of `id` in checkpoint `idx`
    std::vector<uint8_t> BlobOf(size_t idx, ttd::PeripheralId id) const
    {
        const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(idx);
        const auto it = cp->peripheralBlobs.find(static_cast<uint8_t>(id));
        return it == cp->peripheralBlobs.end() ? std::vector<uint8_t>() : ttd::TTDPeripheralRegistry::DecodeBlob(static_cast<uint8_t>(id), it->second);
    }

    DebugKeyboardManager* Keys() const { return _context->pDebugManager->GetKeyboardManager(); }
    DebugMouseManager* MouseManager() const { return _context->pDebugManager->GetMouseManager(); }

    /// SETUP's IDE probe, not recorded, in turbo mode: F4 for every unit that never answers (an empty
    /// channel without the empty-channel fix), as the user presses it, until the boot starts
    void SkipIdeProbe()
    {
        _emulator->EnableTurboMode();
        for (int i = 0; i < 4 && !ScreenHas("Start from"); i++)
        {
            EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("[Press F4") || ScreenHas("Start from"); }, 800, 2);
            if (ScreenHas("[Press F4"))
            {
                Keys()->PressKey("f4");
                _emulator->RunNFrames(3, true);
                Keys()->ReleaseKey("f4");
                _emulator->RunNFrames(3, true);
            }
        }
        _emulator->DisableTurboMode();
        ASSERT_TRUE(ScreenHas("Start from")) << ScreenText();
        RunToBoundary();
    }
};

/// Recording a Sprinter starts (it was refused until S7) and its checkpoints carry every
/// Sprinter blob; a session survives a dump and a load into a fresh machine
TEST_F(TTDSprinterMachine_Test, RecordsWithEverySprinterBlob)
{
    PowerOn(true);
    Skip(5);
    StartRecording();
    Record(3);
    _ttd->StopRecording();
    ASSERT_GE(_ttd->GetCheckpointCount(), 4u);
    const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(3);
    for (ttd::PeripheralId id : {ttd::PeripheralId::SprinterPld, ttd::PeripheralId::Ds12887, ttd::PeripheralId::SprinterVideoRam,
                                 ttd::PeripheralId::Z84C15, ttd::PeripheralId::SprinterFastRam, ttd::PeripheralId::SprinterInput,
                                 ttd::PeripheralId::BetaDisk, ttd::PeripheralId::Wd1793Context, ttd::PeripheralId::KempstonMouse})
        EXPECT_EQ(cp->peripheralBlobs.count(static_cast<uint8_t>(id)), 1u) << "id " << int(id);
    ExpectExactReplay(0, 3, "a few frames of BIOS POST");
}

/// Mid PLD load: the full start, a checkpoint while the ROM's loader feeds the PLD (the bitstream count,
/// the hashes and the watchdog in the PLD blob, the loader's fast RAM window and the Z84C15's power-on
/// waits). Replays from there reach the configured machine and the BIOS with the same frames.
/// Boot-bound: ~130 frames of the loader and the BIOS, each replayed
TEST_F(TTDSprinterMachine_Test, ExactRestore_MidPldLoad)
{
    PowerOn(false);
    Skip(20);
    ASSERT_EQ(_decoder->GetPldState().configState, SprinterConfigState::Loading);
    StartRecording();
    size_t configuredAt = 0;
    Record(130, {}, [&] {
        if (!configuredAt && _decoder->GetPldState().configState == SprinterConfigState::Configured)
            configuredAt = _ttd->GetCheckpointCount() - 1;
    });
    _ttd->StopRecording();
    ASSERT_GT(configuredAt, 30u) << "the load must still run at the checkpoints the replays start from";

    const size_t mid = 30;
    const std::vector<uint8_t> pld = BlobOf(mid, ttd::PeripheralId::SprinterPld);
    ASSERT_GT(pld.size(), 1 + offsetof(SprinterPldState, bitstreamCount) + 4);
    EXPECT_EQ(pld[1 + offsetof(SprinterPldState, configState)], SprinterConfigState::Loading);
    uint32_t count = 0;
    std::memcpy(&count, &pld[1 + offsetof(SprinterPldState, bitstreamCount)], 4);
    EXPECT_GT(count, 0u);
    EXPECT_LT(count, 473720u) << "the checkpoint is in the middle of the bitstream";

    ExpectExactReplay(0, 130, "from the first checkpoint of the load");
    ExpectExactReplay(mid, configuredAt + 20 - mid, "from the middle of the load");
}

/// The DSS boot from the floppy (testdata/machines/sprinter/dss_1_62_92.img) recorded with typed
/// keys (journaled PC keys: PS/2 bytes on SIO A) and mouse moves (journaled Kempston counters,
/// the serial mouse's source), replayed from its start: every boundary - SIO A / B FIFOs and the
/// keyboard and mouse streams in the Z84C15 and input blobs, RAM, video RAM, the picture - equal.
/// Then exact restores at awkward points of the same recording: a frame boundary in the middle
/// of a floppy sector and one with a PS/2 byte on the wire.
/// Boot-bound: the floppy boot and DSS at 21 MHz (a few hundred frames), replayed
TEST_F(TTDSprinterMachine_Test, DssFloppyBoot_RecordAndReplayWithKeysAndMouse)
{
    const std::string image = TestPathHelper::GetTestDataPath("machines/sprinter/dss_1_62_92.img");
    if (!FileHelper::FileExists(image))
        GTEST_SKIP() << "testdata/machines/sprinter/dss_1_62_92.img is missing";
    // The floppy without SYSTEM.BAT's last line "fn" (Flex Navigator), so DSS stops at its prompt
    // (.recipe/machines/sprinter.md): SYSTEM.BAT is root entry 3, its data at LBA 80
    std::vector<uint8_t> floppy(1474560);
    {
        FILE* f = std::fopen(image.c_str(), "rb");
        ASSERT_NE(f, nullptr);
        ASSERT_EQ(std::fread(floppy.data(), 1, floppy.size(), f), floppy.size());
        std::fclose(f);
    }
    uint8_t* entry = floppy.data() + 19 * 512 + 2 * 32;
    ASSERT_EQ(std::memcmp(entry, "SYSTEM  BAT", 11), 0);
    const uint32_t size = entry[28] | entry[29] << 8 | entry[30] << 16 | static_cast<uint32_t>(entry[31]) << 24;
    ASSERT_EQ(std::memcmp(floppy.data() + 80 * 512 + size - 4, "fn\r\n", 4), 0);
    const uint32_t trimmed = size - 4;
    for (int i = 0; i < 4; i++)
        entry[28 + i] = static_cast<uint8_t>(trimmed >> (8 * i));
    const std::string copy = TestPathHelper::GetUniqueTestScratchPath("ttd-sprinter-dss.img");
    ASSERT_TRUE(FileHelper::SaveBufferToFile(copy, floppy.data(), floppy.size()));

    PowerOn(true);
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(copy, 1, &error)) << error;
    ASSERT_NO_FATAL_FAILURE(SkipIdeProbe());

    StartRecording();
    WD1793* fdc = _context->pBetaDisk;
    size_t midSector = 0;
    size_t midPs2 = 0;
    auto watch = [&] {
        const size_t idx = _ttd->GetCheckpointCount() - 1;
        if (!midSector && fdc->getLastDecodedCommand() == WD1793::WD_CMD_READ_SECTOR && fdc->getFSMState() != WD1793::S_IDLE &&
            fdc->getBytesToRead() > 0 && fdc->getBytesToRead() < 512)
            midSector = idx;
        if (!midPs2 && _decoder->GetInput().KeyboardStream().GetState().count != 0)
            midPs2 = idx;
    };

    // The floppy boot to the prompt
    for (int i = 0; i < 1500 && !ScreenHas("B:\\>"); i += 10)
        Record(10, {}, watch);
    ASSERT_TRUE(ScreenHas("B:\\>")) << ScreenText();

    // "ver" + Enter with mouse moves between the keys, the keys inside the frames
    const ZXKeysEnum keys[] = {ZXKEY_V, ZXKEY_E, ZXKEY_R, ZXKEY_ENTER};
    for (size_t k = 0; k < std::size(keys); k++)
    {
        Record(1, [&](int) {
            _emulator->RunTStates(_z80->_frameLimit - _z80->t - 2000, true);  // just before the boundary
            Keys()->PressKey(keys[k]);
        }, watch);
        Record(4, {}, watch);
        ASSERT_TRUE(MouseManager()->Move(static_cast<int>(3 + k), -2).ok());
        Keys()->ReleaseKey(keys[k]);
        Record(5, {}, watch);
    }
    Record(60, {}, watch);
    _ttd->StopRecording();
    EXPECT_TRUE(ScreenHas("B:\\>ver")) << ScreenText();
    ASSERT_GT(midSector, 0u) << "no frame boundary fell into a floppy sector";
    ASSERT_GT(midPs2, 0u) << "no frame boundary had a PS/2 byte on the wire";

    ExpectExactReplay(0, _ttd->GetCheckpointCount(), "the whole boot");
    ExpectExactReplay(midSector, 40, "from the middle of a floppy sector");
    ExpectExactReplay(midPs2, 40, "from a PS/2 byte on the wire");
    std::remove(copy.c_str());
}

/// The DSS boot from a hard disk built from the floppy's files, recorded; an exact restore from a
/// frame boundary in the middle of an IDE sector (the ATA unit's buffer half moved).
/// Boot-bound: DSS from the disk (up to the first boundary inside a sector), 40 frames replayed
TEST_F(TTDSprinterMachine_Test, ExactRestore_MidIdeSector)
{
    const std::vector<uint8_t> floppy = [] {
        std::vector<uint8_t> bytes;
        const std::string path = TestPathHelper::GetTestDataPath("machines/sprinter/dss_1_62_92.img");
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f)
            return bytes;
        bytes.resize(1474560);
        if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size())
            bytes.clear();
        std::fclose(f);
        return bytes;
    }();
    if (floppy.size() != 1474560u)
        GTEST_SKIP() << "testdata/machines/sprinter/dss_1_62_92.img is missing";
    const std::vector<uint8_t> loader(floppy.begin() + 512, floppy.begin() + 4 * 512);
    const std::string bat = "ver\r\n";
    std::vector<uint8_t> disk = BuildDssHdd(loader, {{"SYSTEM  DOS", FloppyRootFile(floppy, "SYSTEM  DOS")},
                                                           {"SYSTEM  EXE", FloppyRootFile(floppy, "SYSTEM  EXE")},
                                                           {"SYSTEM  BAT", std::vector<uint8_t>(bat.begin(), bat.end())}});
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("ttd-sprinter-hdd.img");
    ASSERT_TRUE(FileHelper::SaveBufferToFile(path, disk.data(), disk.size()));

    PowerOn(true);
    MediaSource source;
    source.path = path;
    InsertOptions options;
    options.immediate = true;
    options.access = AccessMode::Session;
    ASSERT_TRUE(_context->pMediaManager->Insert("ide0.master", source, options).Ok());
    ASSERT_NO_FATAL_FAILURE(SkipIdeProbe());

    StartRecording();
    AtaDevice* master = _context->pIdeController->Channel(0).Unit(0);
    ASSERT_NE(master, nullptr);
    size_t midSector = 0;
    for (int i = 0; i < 1500 && !midSector; i++)
    {
        Record(1, {}, [&] {
            const AtaDeviceState& s = master->State();
            if (s.bufferLen && s.bufferPos > 0 && s.bufferPos < s.bufferLen)
                midSector = _ttd->GetCheckpointCount() - 1;
        });
    }
    Record(40);
    _ttd->StopRecording();
    ASSERT_GT(midSector, 0u) << "no frame boundary fell into an IDE sector";

    ExpectExactReplay(midSector, 40, "from the middle of an IDE sector");
    std::remove(path.c_str());
}

/// Byte-exact SIO A / B input: a program polls both SIO channels with interrupts off and logs every
/// byte with its channel; keys and mouse moves arrive inside frames. Checkpoints with a PS/2 byte
/// on the wire and with a serial-mouse packet half sent are replayed: the log (RAM), the FIFOs, the
/// streams - every boundary equal; a restore in the middle of a packet resumes on the same byte.
/// The mouse goes through the MouseManager's journal with no Kempston interface fitted ([INPUT]
/// Mouse=NONE): moves and the left / right buttons reach the board mouse, whose counters travel in
/// blob 31 with the packet in flight.
/// ~120 frames at 3.5 MHz, replayed from three points
TEST_F(TTDSprinterMachine_Test, ExactRestore_MidPs2ByteAndMidMousePacket)
{
    PowerOn(true);
    Skip(10);  // the BIOS starts; the program below takes over the CPU with interrupts off
    _context->pMouse->SetPresent(false);  // the board mouse alone

    // DI; log at #9000: (channel, byte) pairs, then the iteration counter keeps the loop busy
    static const uint8_t program[] = {
        0xF3,                    // 8000 DI
        0xDD, 0x21, 0x00, 0x90,  // 8001 LD IX,#9000
        0xDB, 0x19,              // 8005 loop: IN A,(#19)   SIO A RR0
        0x0F,                    // 8007 RRCA
        0x30, 0x0D,              // 8008 JR NC,chkB
        0xDB, 0x18,              // 800A IN A,(#18)
        0xDD, 0x36, 0x00, 0x0A,  // 800C LD (IX+0),#0A
        0xDD, 0x77, 0x01,        // 8010 LD (IX+1),A
        0xDD, 0x23,              // 8013 INC IX
        0xDD, 0x23,              // 8015 INC IX
        0xDB, 0x1B,              // 8017 chkB: IN A,(#1B)  SIO B RR0
        0x0F,                    // 8019 RRCA
        0x30, 0xE9,              // 801A JR NC,loop
        0xDB, 0x1A,              // 801C IN A,(#1A)
        0xDD, 0x36, 0x00, 0x0B,  // 801E LD (IX+0),#0B
        0xDD, 0x77, 0x01,        // 8022 LD (IX+1),A
        0xDD, 0x23,              // 8025 INC IX
        0xDD, 0x23,              // 8027 INC IX
        0x18, 0xDA,              // 8029 JR loop
    };
    for (size_t i = 0; i < sizeof(program); i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    for (uint16_t a = 0x9000; a < 0x9400; a++)
        _context->pMemory->DirectWriteToZ80Memory(a, 0);
    // SIO B's receive clock as DSS 1.71 sets it (CTC ZC/TO0: 875 kHz / 45, x16 = 1 215 baud): the mouse's
    // characters are received only in tune
    Z84Lib::Z84C15& chip = _decoder->GetZ84();
    chip.Write(0x10, 0x55);
    chip.Write(0x10, 45);
    chip.Write(0x1B, 0x04);
    chip.Write(0x1B, 0x44);
    _z80->pc = 0x8000;
    RunToBoundary();

    StartRecording();
    size_t midPs2 = 0;
    size_t midPacket = 0;
    auto watch = [&] {
        const size_t idx = _ttd->GetCheckpointCount() - 1;
        if (!midPs2 && _decoder->GetInput().KeyboardStream().GetState().count != 0)
            midPs2 = idx;
        const uint8_t sent = _decoder->GetInput().SerialMouse().GetState().sent;
        if (!midPacket && sent > 0 && sent < 3)
            midPacket = idx;
    };
    auto late = [&](int) { _emulator->RunTStates(_z80->_frameLimit - _z80->t - 1500, true); };
    for (int round = 0; round < 6; round++)
    {
        Record(1, [&](int i) {
            late(i);
            Keys()->PressKey("f4");  // F4: #0C on the wire, 917 us = ~3 210 T per byte
        }, watch);
        Record(1, [&](int i) {
            late(i);
            ASSERT_TRUE(MouseManager()->Move(4 + round, round - 3).ok());  // a packet: 3 x 7.5 ms
        }, watch);
        Record(3, {}, watch);
        Record(1, [&](int i) {
            late(i);
            Keys()->ReleaseKey("f4");  // F0 0C
        }, watch);
        // A button held across a few frames: left in the even rounds, right in the odd ones
        const MouseButton button = round % 2 ? MouseButton::Right : MouseButton::Left;
        Record(1, [&](int i) {
            late(i);
            ASSERT_TRUE(MouseManager()->PressButton(button).ok());
        }, watch);
        Record(3, {}, watch);
        ASSERT_TRUE(MouseManager()->ReleaseButton(button).ok());
        Record(4, {}, watch);
    }
    _ttd->StopRecording();

    // The board counters are in blob 31 (bytes 85-87: X, Y, buttons): the last checkpoint holds the live ones
    const SprinterInput::BoardMouse board = _decoder->GetInput().GetBoardMouse();
    const std::vector<uint8_t> last = BlobOf(_ttd->GetCheckpointCount() - 1, ttd::PeripheralId::SprinterInput);
    ASSERT_EQ(last.size(), ttd::TTDSprinterInput::kSize);
    EXPECT_EQ(last[85], board.x);
    EXPECT_EQ(last[86], board.y);
    EXPECT_EQ(last[87], 0xFF) << "every button released";
    EXPECT_EQ(BlobOf(0, ttd::PeripheralId::SprinterInput)[85], static_cast<uint8_t>(board.x - (4 + 5 + 6 + 7 + 8 + 9)))
        << "the first checkpoint: before the moves";
    ASSERT_GT(midPs2, 0u) << "no boundary with a PS/2 byte on the wire";
    ASSERT_GT(midPacket, 0u) << "no boundary in the middle of a mouse packet";

    // Both channels reached the program
    bool sawA = false, sawB = false;
    for (uint16_t a = 0x9000; a < 0x9400; a += 2)
    {
        const uint8_t ch = _context->pMemory->DirectReadFromZ80Memory(a);
        sawA |= ch == 0x0A;
        sawB |= ch == 0x0B;
    }
    EXPECT_TRUE(sawA) << "no keyboard byte was logged";
    EXPECT_TRUE(sawB) << "no mouse byte was logged";
    // The first byte of a packet (bit 6) with bit 5 (left) and with bit 4 (right): D0 / D1 of the manager's mask
    bool sawLeft = false, sawRight = false;
    for (uint16_t a = 0x9000; a < 0x9400; a += 2)
    {
        const uint8_t b = _context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(a + 1));
        if (_context->pMemory->DirectReadFromZ80Memory(a) != 0x0B || (b & 0x40) == 0)
            continue;
        sawLeft |= (b & 0x30) == 0x20;
        sawRight |= (b & 0x30) == 0x10;
    }
    EXPECT_TRUE(sawLeft) << "no packet with the left button";
    EXPECT_TRUE(sawRight) << "no packet with the right button";

    ExpectExactReplay(0, _ttd->GetCheckpointCount(), "the whole session");
    ExpectExactReplay(midPs2, 30, "from a PS/2 byte on the wire");
    ExpectExactReplay(midPacket, 30, "from the middle of a mouse packet");
    _context->pMouse->SetPresent(true);
}

/// Seek anywhere: positions inside frames (the replay to them runs the journal), back and forth,
/// arrive at the state the recording had there
TEST_F(TTDSprinterMachine_Test, SeekAnywhere_InsideFramesBackAndForth)
{
    PowerOn(true);
    Skip(30);
    StartRecording();

    // The recording, with the CPU at three points inside frames noted
    struct Point
    {
        ttd::TTDTimePoint at;
        ttd::TTDCpuState cpu;
        uint64_t vram;
    };
    std::vector<Point> points;
    Record(40, [&](int i) {
        if (i % 13 != 5)
            return;
        _emulator->RunTStates(_z80->_frameLimit * static_cast<unsigned>(1 + points.size()) / 5, true);
        points.push_back({_ttd->CurrentPosition(), ttd::CaptureCpuState(*static_cast<const Z80State*>(_z80)),
                          ttd::HashBytes(_decoder->GetVideoRam().Data(), SprinterVideoRam::kSize)});
    });
    _ttd->StopRecording();
    ASSERT_EQ(points.size(), 3u);

    for (size_t k : {2u, 0u, 1u, 2u})
    {
        ASSERT_TRUE(_ttd->SeekTo(points[k].at)) << k;
        const ttd::TTDCpuState cpu = ttd::CaptureCpuState(*static_cast<const Z80State*>(_z80));
        EXPECT_EQ(std::memcmp(&cpu, &points[k].cpu, sizeof(cpu)), 0) << "seek to point " << k << ": PC " << std::hex << cpu.pc
                                                                     << " vs " << points[k].cpu.pc;
        EXPECT_EQ(ttd::HashBytes(_decoder->GetVideoRam().Data(), SprinterVideoRam::kSize), points[k].vram) << k;
    }
}

/// The block accelerator (phase S5): a program repeats `LD D,D : LD E,16` (the length) and
/// `LD C,C : LD (HL),A` (a 16-byte fill) with interrupts on; every other interrupt its handler runs
/// past the frame end before RETI, so frame boundaries fall both while a mode is armed (the mode, length, function and
/// buffer in the PLD blob) and inside the INT suspend window (an INT acknowledge blocked new
/// operations until the fetch after RETI: AccelIntSuspend=1). Restores from both continue exactly.
/// ~60 frames recorded, replayed from two points
TEST_F(TTDSprinterMachine_Test, ExactRestore_AcceleratorArmedAndInIntSuspendWindow)
{
    PowerOn(true);
    _context->config.sprinter.accel_int_suspend = 1;  // the option (default off since S6)
    Skip(150);  // BIOS POST and SETUP: the mode table places the frame INT
    ASSERT_FALSE(_decoder->GetIntSource().Positions().empty()) << "no frame INT to suspend the accelerator";
    ASSERT_NE(_decoder->GetAccelerator(), nullptr);

    static const uint8_t program[] = {
        0xF3,                    // 8000 DI
        0xED, 0x5E,              // 8001 IM 2
        0x3E, 0x81,              // 8003 LD A,#81       IM 2 table #8100-#8200, every byte #83: any vector -> #8383
        0xED, 0x47,              // 8005 LD I,A
        0x3E, 0x5A,              // 8007 LD A,#5A       the fill byte
        0x21, 0x00, 0xA0,        // 8009 LD HL,#A000    the fill target
        0xFB,                    // 800C EI
        0x52,                    // 800D loop: LD D,D   length mode
        0x1E, 0x10,              // 800E LD E,16        the operand read loads the length
        0x49,                    // 8010 LD C,C         fill mode
        0x77,                    // 8011 LD (HL),A      16 bytes; then NOPs with the fill still armed (8012..)
    };
    // after the NOPs: LD B,B (off: the JR's operand must be a plain fetch), INC HL, JR loop
    static const uint8_t tail[] = {0x40, 0x23, 0x18, 0x00};
    constexpr uint16_t kNops = 100;  // the JR back stays in range
    // Every other INT the handler waits past the frame end (a boundary inside the suspend window);
    // the others return at once (boundaries in the main loop, the fill armed)
    static const uint8_t handler[] = {
        0xF5,                    // 8383 PUSH AF        (plain: blocked)
        0xC5,                    // 8384 PUSH BC
        0x3A, 0x00, 0x90,        // 8385 LD A,(#9000)   the INT counter
        0x3C,                    // 8388 INC A
        0x32, 0x00, 0x90,        // 8389 LD (#9000),A
        0x01, 0x00, 0x0C,        // 838C LD BC,#0C00    ~150 K clocks at 21 MHz with the RAM waits: past the frame end
        0xE6, 0x01,              // 838F AND 1
        0x20, 0x03,              // 8391 JR NZ,wait     odd: the long wait
        0x01, 0x10, 0x00,        // 8393 LD BC,#0010    even: a short one
        0x0B,                    // 8396 wait: DEC BC
        0x78,                    // 8397 LD A,B
        0xB1,                    // 8398 OR C
        0x20, 0xFB,              // 8399 JR NZ,wait
        0xC1,                    // 839B POP BC
        0xF1,                    // 839C POP AF
        0xFB,                    // 839D EI
        0xED, 0x4D,              // 839E RETI           the next fetch unblocks
    };
    Memory* memory = _context->pMemory;
    uint16_t at = 0x8000;
    for (uint8_t b : program)
        memory->DirectWriteToZ80Memory(at++, b);
    for (uint16_t i = 0; i < kNops; i++)
        memory->DirectWriteToZ80Memory(at++, 0x00);
    for (uint8_t b : tail)
        memory->DirectWriteToZ80Memory(at++, b);
    memory->DirectWriteToZ80Memory(static_cast<uint16_t>(at - 1), static_cast<uint8_t>(0x800D - at));  // JR loop
    for (size_t i = 0; i < sizeof(handler); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8383 + i), handler[i]);
    // The PLD answers #FF, the Z84C15's sources their own vectors (the BIOS programmed them): one handler
    for (uint16_t a = 0x8100; a <= 0x8200; a++)
        memory->DirectWriteToZ80Memory(a, 0x83);
    memory->DirectWriteToZ80Memory(0x9000, 0);
    _decoder->GetPldState().allMode |= 0x01;  // ALL_MODE bit 0: the accelerator on
    _z80->iff1 = _z80->iff2 = 0;  // no BIOS interrupt before the program's own DI / IM 2
    _decoder->GetZ84().Reset();   // the BIOS's CTC / SIO / PIO interrupts off: the frame INT alone
    _z80->halted = 0;
    _z80->pc = 0x8000;
    _z80->sp = 0xBF00;
    RunToBoundary();

    StartRecording();
    size_t armed = 0;
    size_t suspended = 0;
    Record(60, {}, [&] {
        const SprinterAccelState& acc = _decoder->GetAccelerator()->State();
        const size_t idx = _ttd->GetCheckpointCount() - 1;
        if (!armed && acc.dir && !acc.blocked && acc.operations > 1)
            armed = idx;
        if (!suspended && acc.blocked)
            suspended = idx;
    });
    _ttd->StopRecording();
    ASSERT_GT(armed, 0u) << "no boundary with an accelerator mode armed";
    ASSERT_GT(suspended, 0u) << "no boundary inside the INT suspend window";
    EXPECT_GT(_decoder->GetAccelerator()->State().operations, 10u);

    ExpectExactReplay(armed, 30, "from a boundary with the accelerator armed");
    ExpectExactReplay(suspended, 30, "from inside the INT suspend window");
}

/// endregion </Exact restore on the real BIOS>
