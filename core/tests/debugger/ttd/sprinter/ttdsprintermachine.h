#pragma once

/// @file ttdsprintermachine.h
/// @brief The real Sprinter under a TTD recording (ttdsprinter_test.cpp, ttdsprintercoverage_test.cpp): a seek back to a
/// checkpoint followed by running forward must arrive at every later checkpoint with the same CPU, chipset, device
/// state, memory regions and picture (ExpectExactReplay, ExpectLiveMatchesCheckpoint).

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
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/ttdrecordedstate.h"
#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/mouse/debugmousemanager.h"
#include "debugger/ttd/machinestatehash.h"
#include "debugger/ttd/sprinter/ttdsprinter.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "debugger/ttd/ttdportsearch.h"
#include "emulator/state/devicestate.h"
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
#include "emulator/ports/models/sprinter/sprinterbios.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/ports/models/portdecoder_sprinter.h"
#include "emulator/video/screen.h"
#include "emulator/video/sprinter/screensprinter.h"
#include "emulator/machines/sprinter/sprinterdssmedia.h"
#include "emulator/machines/sprinter/sprinterfixture.h"
#include "emulator/video/sprinter/sprintergamevideo.h"
#include "emulator/ports/models/sprinter/sprinterpldconfig.h"
#include "emulator/ports/models/sprinter/sprinterpldgame.h"

/// The real machine (BIOS 3.04 selected explicitly) with TTD on. Not in turbo mode: the
/// picture of every frame is compared
class TTDSprinterMachine_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    Z80* _z80 = nullptr;

    /// The recorded run: the picture at every frame boundary, by frame
    std::map<uint64_t, uint64_t> _screens;

    /// A variant's machine (the ISA population): applied when the instance is created
    virtual void ConfigureMachine(CONFIG& config) { (void)config; }
    /// The BIOS image in data/rom/sprinter (3.04 pinned; DSS 1.71 needs 3.06 or later)
    virtual const char* BiosFile() const { return "sp2k-3.04.rom"; }

    void SetUp() override
    {
        if (!SprinterFixture::Rom304Available())
            GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";
        _manager = EmulatorManager::GetInstance();
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);
        _emulator = _manager->CreateEmulatorWithModelAndRAM("sprinter-ttd", "SPRINTER", 4096, LoggerLevel::LogError,
                                                            nullptr, [this](CONFIG& config) { ConfigureMachine(config); });
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
        _z80 = _context->pCore->GetZ80();
        _decoder->GetRtc().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC
        // Pinned to BIOS 3.04 (the shipped default is 3.06 Hotfix 2; its cold start is the corpus fixture
        // testdata/machines/sprinter/ttd/boot.ttd, TTD_Corpus_Test)
        if (!SprinterFixture::SelectBios(_context, BiosFile()))  // PowerOn resets
            GTEST_SKIP() << "data/rom/sprinter/" << BiosFile() << " not found";

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

    /// The live state of a device as the engine records it: without the memory it offers as regions
    static std::vector<uint8_t> LiveDeviceState(ttd::TTDSerializable* device)
    {
        std::vector<uint8_t> state;
        uint8_t id = 0;
        if (auto* source = dynamic_cast<ttd::ITTDRegionSource*>(device); source && source->TTDStateWithoutRegions(id, state))
            return state;
        device->TTDSaveStateTo(state);
        return state;
    }

    /// The live machine against checkpoint `idx` of the recording: CPU, chipset, every device the engine
    /// recorded there, every memory region (machine RAM, the video and fast RAM) and the picture of that frame.
    /// `allRam`: every piece of every region; otherwise the pieces this checkpoint stored anew
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

        // Devices: each one the engine recorded at this checkpoint, against the live device
        const ttd::TimeTravelEngine& engine = _ttd->GetEngine();
        const size_t index = engine.FirstCheckpoint() + idx;
        size_t devices = 0;
        for (const ttd::TTDDeviceEntry& entry : engine.Devices().Entries())
        {
            const uint8_t id = static_cast<uint8_t>(entry.descriptor.legacyId);
            std::vector<uint8_t> recorded;
            if (!engine.DeviceState(index, id, recorded))
                continue;
            ttd::TTDSerializable* device = _ttd->GetPeripheralRegistry().GetDevice(static_cast<ttd::PeripheralId>(id));
            ASSERT_NE(device, nullptr) << where << ": device " << int(id) << " missing";
            const std::vector<uint8_t> live = LiveDeviceState(device);
            ASSERT_EQ(live.size(), recorded.size()) << where << ": device " << int(id);
            size_t first = 0;
            while (first < recorded.size() && live[first] == recorded[first])
                first++;
            EXPECT_EQ(first, recorded.size()) << where << ": device " << int(id) << " differs from byte " << first;
            devices++;
        }
        EXPECT_GT(devices, 0u) << where;

        // Memory: every region, the pieces the session had seen by then
        const ttd::TimeTravelEngine* previous = idx > 0 && !allRam ? &engine : nullptr;
        for (uint32_t r = 0; r < engine.Regions().size(); r++)
        {
            const ttd::TTDRegionDesc& region = engine.Regions()[r];
            if (!region.memory)
                continue;
            std::vector<uint8_t> recorded(size_t(region.pieces) * ttd::kTTDPieceSize);
            std::vector<uint8_t> present;
            const ttd::TTDRestoreResult restored = engine.RestoreRegion(index, r, recorded.data(), &present);
            ASSERT_TRUE(restored.Ok()) << where << ": region " << region.name << ": " << restored.message;
            for (uint32_t p = 0; p < region.pieces; p++)
            {
                if (p < present.size() && !present[p])
                    continue;
                if (previous && engine.VersionAt(index, r, p) == engine.VersionAt(index - 1, r, p))
                    continue;
                const size_t offset = size_t(p) * ttd::kTTDPieceSize;
                const size_t length = std::min<size_t>(ttd::kTTDPieceSize, region.bytes - offset);
                ASSERT_EQ(std::memcmp(region.memory + offset, recorded.data() + offset, length), 0)
                    << where << ": " << region.name << " piece " << p;
            }
        }

        // The picture of the frame that ended here (a seek itself draws nothing into the framebuffer)
        const auto screen = _screens.find(cp->time.frame);
        if (picture && screen != _screens.end())
            EXPECT_EQ(ScreenHash(), screen->second) << where << ": the picture differs";
    }

    /// Seek to checkpoint `from` and run forward through `frames` recorded frames (the journal
    /// plays the input): every boundary must be the recorded one
    void ExpectExactReplay(size_t from, size_t frames, const std::string& what, const std::function<void()>& afterSeek = {})
    {
        const ttd::TTDCheckpoint* cp = _ttd->GetCheckpoint(from);
        ASSERT_NE(cp, nullptr) << what;
        ASSERT_TRUE(_ttd->SeekTo({cp->time.frame, 0})) << what;
        if (afterSeek)
            afterSeek();   // what a user may do there before running on (a frame-cache build replays elsewhere)
        ExpectLiveMatchesCheckpoint(from, what + ": the restore", true, false);
        const size_t last = std::min(_ttd->GetCheckpointCount() - 1, from + frames);
        for (size_t idx = from + 1; idx <= last && !HasFailure(); idx++)
        {
            RunToBoundary();
            ExpectLiveMatchesCheckpoint(idx, what + ": frame " + std::to_string(idx - from) + " after the restore", idx == last);
        }
    }

    /// The recorded state of `id` at checkpoint `idx` (as the engine keeps it)
    std::vector<uint8_t> BlobOf(size_t idx, ttd::PeripheralId id) const { return ttdtest::RecordedDeviceState(*_ttd, idx, id); }

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

/// The machine as it ships: every sound device its config names (the AY in the PLD, the NeoGS behind the ISA ZX-bus
/// adapter), which the test runner otherwise leaves out (SoundCardScope). Coverage tests record the whole machine
class TTDSprinterShipped_Test : public TTDSprinterMachine_Test
{
protected:
    SoundCardScope _shippedSound;   ///< held while SetUp creates the machine
};
