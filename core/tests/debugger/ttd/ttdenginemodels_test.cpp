/// @file ttdenginemodels_test.cpp
/// @brief The engine on every machine with large memory: the controller
/// records each model and every frame it records restores the machine the
/// recording had there live (machine RAM, CPU, chipset, device state).
/// Machine RAM is the engine's region 0 on every model; this checks that the
/// one mechanism holds on 512 KB-4 MB machines and their paging schemes.
/// (Until Phase 6 the engine recorded next to v1's TimeTravelManager and v1's
/// checkpoints were the reference.)
///
/// Each case boots its model from ROM and records 150 frames: slower than the
/// 50 ms guideline, one acceptance check per model.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdcheckpoint.h"
#include "debugger/ttd/ttdperipheralregistry.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/atm/evoavr.h"
#include "emulator/ports/models/portdecoder_atm3.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "emulator/ports/portdecoder.h"

namespace
{
uint64_t Fnv1a(const uint8_t* data, size_t size)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i)
        h = (h ^ data[i]) * 1099511628211ull;
    return h;
}

/// What a restore must reproduce; RAM as a hash (a point per frame of up to 4 MB)
struct LiveState
{
    ttd::TTDTimePoint at;
    ttd::TTDCpuState cpu;
    ttd::TTDChipsetState chipset;
    uint64_t t = 0;
    uint64_t ram = 0;
    std::map<uint8_t, std::vector<uint8_t>> devices;
};
}  // namespace

class TTDEngineModels_Fixture : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;
    std::vector<LiveState> _live;   ///< the machine after each recorded frame

    void Start(const char* model, Emulator* emulator = nullptr)
    {
        _emulator = emulator ? emulator : EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << model;
        EmulatorContext* context = _emulator->GetContext();
        _ttd = context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _ttd->StopRecording();
            EmulatorTestHelper::CleanupEmulator(_emulator);
        }
        _emulator = nullptr;
        _ttd = nullptr;
        _live.clear();
    }

    ttd::TimeTravelEngine& Engine() const { return _ttd->GetEngine(); }

    std::map<uint8_t, std::vector<uint8_t>> DeviceStates() const
    {
        std::map<uint8_t, std::vector<uint8_t>> out;
        for (const ttd::TTDRegistryEntry& e : _ttd->GetPeripheralRegistry().Entries())
            if (e.device && e.device->TTDStateSize() != 0 && e.legacyId != ttd::PeripheralId::None)
                e.device->TTDSaveStateTo(out[static_cast<uint8_t>(e.legacyId)]);
        return out;
    }

    LiveState Capture() const
    {
        EmulatorContext* context = _emulator->GetContext();
        const Z80* z80 = context->pCore->GetZ80();
        LiveState s;
        s.at = _ttd->CurrentPosition();
        s.cpu = ttd::CaptureCpuState(*z80);
        s.chipset = ttd::CaptureChipsetState(context->emulatorState, static_cast<uint32_t>(z80->t));
        s.t = z80->t;
        s.ram = Fnv1a(context->pMemory->RAMPageAddress(0), size_t(_ttd->GetModelRamPages()) * 0x4000);
        s.devices = DeviceStates();
        return s;
    }

    void KeepLive() { _live.push_back(Capture()); }

    /// Records @p frames frames, keeping the machine after each
    void RecordFrames(int frames)
    {
        ASSERT_TRUE(_ttd->StartRecording());
        for (int f = 0; f < frames; ++f)
        {
            _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
            KeepLive();
        }
        _ttd->StopRecording();
    }

    /// The machine as a seek to checkpoint @p i shows it (a frame-aligned restore; the live machine stops a few T
    /// past the boundary, so no live point stands exactly there)
    LiveState SeekedToCheckpoint(size_t i)
    {
        const ttd::TTDPosition& p = Engine().Checkpoint(i)->position;
        ttd::TTDSeekResult r;
        EXPECT_TRUE(_ttd->SeekTo({p.frame, static_cast<uint32_t>(p.tInFrame)}, &r));
        return Capture();
    }

    /// The machine shows @p expected
    void ExpectLive(const LiveState& expected, bool devicesOnly = false) const
    {
        const LiveState seen = Capture();
        if (!devicesOnly)
        {
            EXPECT_EQ(std::memcmp(&seen.cpu, &expected.cpu, sizeof(seen.cpu)), 0) << "CPU";
            EXPECT_EQ(std::memcmp(&seen.chipset, &expected.chipset, sizeof(seen.chipset)), 0) << "chipset";
            EXPECT_EQ(seen.t, expected.t);
            EXPECT_EQ(seen.ram, expected.ram) << "RAM";
        }
        for (const auto& [id, bytes] : expected.devices)
            EXPECT_TRUE(seen.devices.count(id) && seen.devices.at(id) == bytes) << "device " << int(id);
    }

    /// The engine's tables, and every kept point (newest first) seeks to the machine it was
    void CheckEveryCheckpoint()
    {
        ASSERT_FALSE(Engine().Regions().empty());

        // The device table: the registry's devices, restored in ascending legacy ids except where a device names
        // another (the WD1793 context after the controller)
        const ttd::TTDDeviceTable& table = Engine().Devices();
        EXPECT_EQ(table.Entries().size(), _ttd->GetPeripheralRegistry().Count());
        std::vector<uint16_t> order;
        for (uint32_t i : table.RestoreOrder())
            order.push_back(static_cast<uint16_t>(table.Entries()[i].descriptor.legacyId));
        EXPECT_TRUE(std::is_sorted(order.begin(), order.end())) << "ascending ids";
        if (table.Find({ttd::TTDDeviceType::BetaDisk, "betadisk"}))
        {
            EXPECT_NE(table.Find({ttd::TTDDeviceType::Wd1793Context, "betadisk.context"}), nullptr);
        }
        EXPECT_EQ(Engine().Regions()[0].pieces, size_t(_ttd->GetModelRamPages()) * 4)
            << "region 0 covers the model's whole RAM";

        // FR-19: every device that runs behind the CPU (the sound cards) was
        // at the frame boundary at every capture
        EXPECT_EQ(Engine().SyncMissCount(), 0u)
            << (Engine().SyncMisses().empty() ? std::string()
                                              : Engine().SyncMisses()[0].device.instance + " at frame " +
                                                    std::to_string(Engine().SyncMisses()[0].frame) + ", offset " +
                                                    std::to_string(Engine().SyncMisses()[0].offset));

        ASSERT_FALSE(_live.empty());
        for (auto it = _live.rbegin(); it != _live.rend(); ++it)
        {
            SCOPED_TRACE("frame " + std::to_string(it->at.frame) + " T " + std::to_string(it->at.tInFrame));
            ttd::TTDSeekResult r;
            ASSERT_TRUE(_ttd->SeekTo(it->at, &r));
            ExpectLive(*it);
            if (HasFailure())
                return;   // the first divergence is the useful one
        }
    }

    /// The engine alone restores checkpoint @p i (CPU, chipset, memory regions, devices, banking)
    void RestoreFromTheEngine(size_t i)
    {
        EmulatorContext* context = _emulator->GetContext();
        const ttd::TTDEngineCheckpoint* cp = Engine().Checkpoint(i);
        ttd::RestoreCpuState(cp->cpu, static_cast<Z80State*>(context->pCore->GetZ80()));
        ttd::RestoreChipsetState(cp->chipset, &context->emulatorState);
        Engine().ForgetMemory();
        ASSERT_TRUE(Engine().RestoreToMemory(i).Ok());
        const ttd::TTDRestoreResult r = Engine().RestoreDevices(i, ttd::TTDRestoreContext{cp->position.frame, 0, false});
        EXPECT_EQ(r.status, ttd::TTDRestoreStatus::Exact) << r.message;
        context->pMemory->UpdateZ80Banks();
    }
};

class TTDEngineModels_Test : public TTDEngineModels_Fixture, public ::testing::WithParamInterface<const char*>
{
};

TEST_P(TTDEngineModels_Test, EveryFrameRestoresTheLiveMachine)
{
    ASSERT_NO_FATAL_FAILURE(Start(GetParam()));
    ASSERT_NO_FATAL_FAILURE(RecordFrames(150));
    CheckEveryCheckpoint();
}

/// The engine restores every device (Phase 2, Step 3): the machine runs on
/// past the recording, then the engine alone restores a checkpoint (CPU,
/// chipset, memory regions, devices, banking) and every device saves the
/// bytes it saves after the controller's seek there (EveryFrameRestoresTheLiveMachine
/// holds those seeks to the live machine)
TEST_P(TTDEngineModels_Test, EngineRestoresEveryDeviceAsItWas)
{
    ASSERT_NO_FATAL_FAILURE(Start(GetParam()));
    ASSERT_NO_FATAL_FAILURE(RecordFrames(40));

    for (size_t i : {size_t(5), size_t(20), Engine().CheckpointCount() - 1})
    {
        SCOPED_TRACE("checkpoint " + std::to_string(i));
        const LiveState seeked = SeekedToCheckpoint(i);
        ASSERT_FALSE(HasFailure());
        _emulator->RunNFrames(3, /*skipBreakpoints=*/true);   // the machine moves on
        ASSERT_NO_FATAL_FAILURE(RestoreFromTheEngine(i));
        ExpectLive(seeked, /*devicesOnly=*/true);
    }
}

/// FR-19 with every card that runs its own clock fitted (TSFM, MoonSound, and
/// the GS or NeoGS card): at every capture each one stood at the frame
/// boundary, every frame restores the live machine, and after the engine restores a
/// checkpoint every card's clock is at that boundary again
class TTDEngineCards_Test : public TTDEngineModels_Fixture, public ::testing::WithParamInterface<GSTypeKind>
{
protected:
    SoundCardScope _cards{TestSound::All};
};

TEST_P(TTDEngineCards_Test, CardsAreAtTheFrameBoundaryAtEveryCaptureAndRestore)
{
    Emulator* emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", TurboSoundKind::FM);
    ASSERT_NE(emulator, nullptr);
    ASSERT_TRUE(FitGeneralSoundCard(emulator->GetContext()->pSoundManager, GetParam()));
    ASSERT_NO_FATAL_FAILURE(Start("PENTAGON", emulator));
    ASSERT_NO_FATAL_FAILURE(RecordFrames(60));

    std::vector<std::string> behind;
    for (const ttd::TTDDeviceEntry& e : Engine().Devices().Entries())
        if (e.descriptor.runsBehindCpu)
            behind.push_back(e.descriptor.instance);
#ifdef UNREALNG_HAVE_OPL4
    EXPECT_EQ(behind.size(), 3u) << "TSFM, MoonSound and the GS card";
#else
    EXPECT_EQ(behind.size(), 2u) << "TSFM and the GS card";
#endif
    ASSERT_NO_FATAL_FAILURE(CheckEveryCheckpoint());

    for (size_t i : {size_t(10), size_t(40)})
    {
        SCOPED_TRACE("checkpoint " + std::to_string(i));
        _emulator->RunNFrames(3, /*skipBreakpoints=*/true);   // the machine moves on
        ASSERT_NO_FATAL_FAILURE(RestoreFromTheEngine(i));
    }
}

INSTANTIATE_TEST_SUITE_P(Cards, TTDEngineCards_Test, ::testing::Values(GSTypeKind::Z80, GSTypeKind::NGS),
                         [](const ::testing::TestParamInfo<GSTypeKind>& info) {
                             return info.param == GSTypeKind::NGS ? std::string("NeoGS") : std::string("GeneralSound");
                         });

INSTANTIATE_TEST_SUITE_P(LargeMemoryModels, TTDEngineModels_Test,
                         ::testing::Values("PENTAGON", "SCORPION", "PROFSCORP", "PROFI", "ATM710", "ATM450", "ATM3",
                                           "TSL", "TSL-VDAC2", "SPRINTER"),
                         [](const ::testing::TestParamInfo<const char*>& info) {
                             std::string name = info.param;
                             std::erase(name, '-');
                             return name;
                         });

#ifdef ENABLE_VDAC2

/// The VDAC2 card's FT812 written while recording: RAM_G, the display list
/// and the command FIFO change every frame through the chip's bus (ports #77 /
/// #57, as VDAC2 software writes), so its seven memory regions record what
/// eve-emu's dirty bitmap reports and each frame restores the live card
TEST_F(TTDEngineModels_Fixture, Vdac2MemoryWrittenWhileRecording)
{
    ASSERT_NO_FATAL_FAILURE(Start("TSL-VDAC2"));
    PortDecoder* ports = _emulator->GetContext()->pPortDecoder;
    auto out = [&](uint16_t port, uint8_t value) { ports->DecodePortOut(port, value, 0x0000); };
    constexpr uint16_t kConfig = 0x77, kData = 0x57;
    constexpr uint8_t kSelect = 0x06, kDeselect = 0x02;
    auto host = [&](uint8_t command, uint8_t parameter = 0) {
        out(kConfig, kSelect);
        out(kData, command);
        out(kData, parameter);
        out(kData, 0x00);
        out(kConfig, kDeselect);
    };
    auto write = [&](uint32_t address, const std::vector<uint8_t>& bytes) {
        out(kConfig, kSelect);
        out(kData, static_cast<uint8_t>(0x80 | ((address >> 16) & 0x3F)));
        out(kData, static_cast<uint8_t>(address >> 8));
        out(kData, static_cast<uint8_t>(address));
        for (uint8_t b : bytes)
            out(kData, b);
        out(kConfig, kDeselect);
    };
    // Wake the chip: ACTIVE, external clock x6, reset pulse (FT812 host commands)
    host(0x43);
    host(0x00);
    host(0x42);
    host(0x44);
    host(0x61, 0x46);
    host(0x00);
    host(0x68);
    _emulator->RunNFrames(5, /*skipBreakpoints=*/true);

    ASSERT_TRUE(_ttd->StartRecording());
    for (uint32_t f = 0; f < 40; ++f)
    {
        const uint8_t v = static_cast<uint8_t>(f * 37 + 1);
        write((f * 0x6A35u) % 0x100000u, {v, uint8_t(v ^ 0x5A), uint8_t(v + 1)});   // RAM_G, a page per frame
        write(0x300000u + (f % 16) * 8, {v, 0, 0, 0x26});                            // the display list
        write(0x308000u + (f % 64) * 4, {v, 1, 2, 3});                               // the command FIFO
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        KeepLive();
    }
    _ttd->StopRecording();

    size_t vdac2Regions = 0;
    for (uint32_t r = 0; r < Engine().Regions().size(); ++r)
        vdac2Regions += Engine().Regions()[r].ownerType == static_cast<uint16_t>(ttd::PeripheralId::Vdac2Memory) &&
                        !Engine().IsDeviceStateRegion(r);
    ASSERT_EQ(vdac2Regions, 7u) << "RAM_G, DL0, DL1, REG, CMD, SPECIAL, INFLIGHT";
    uint32_t ramG = 0;
    while (Engine().Regions()[ramG].name != "vdac2.RAM_G")
        ++ramG;
    size_t changed = 0;
    for (size_t i = 1; i < Engine().CheckpointCount(); ++i)
        changed += Engine().ChangeCount(i, ramG);
    EXPECT_GE(changed, 30u) << "the frames' RAM_G writes reach the engine as changed pieces";

    CheckEveryCheckpoint();
}

#endif  // ENABLE_VDAC2

namespace
{
/// The engine region of @p name at checkpoint @p i, its real bytes
std::vector<uint8_t> RegionAt(const ttd::TimeTravelEngine& engine, size_t i, const std::string& name)
{
    for (uint32_t r = 0; r < engine.Regions().size(); ++r)
        if (engine.Regions()[r].name == name)
        {
            std::vector<uint8_t> mem(size_t(engine.Regions()[r].pieces) * ttd::kTTDPieceSize, 0);
            EXPECT_TRUE(engine.RestoreRegion(i, r, mem.data()).Ok());
            mem.resize(engine.Regions()[r].bytes);
            return mem;
        }
    ADD_FAILURE() << "no region " << name;
    return {};
}

/// #FFBA writes of one I2C byte write to the SMUC EEPROM: START, select,
/// address, data, each with its ACK clock, STOP (SDA = bit 4, SCL = bit 6)
std::vector<uint8_t> SmucWriteByte(uint16_t address, uint8_t value)
{
    std::vector<uint8_t> out;
    auto lines = [&](bool sda, bool scl) { out.push_back(static_cast<uint8_t>((sda ? 0x10 : 0) | (scl ? 0x40 : 0))); };
    auto byte = [&](uint8_t b) {
        for (int bit = 7; bit >= 0; --bit)
        {
            const bool sda = (b >> bit) & 1;
            lines(sda, false);
            lines(sda, true);
            lines(sda, false);
        }
        lines(true, false);
        lines(true, true);
        lines(true, false);
    };
    lines(true, true);
    lines(false, true);
    lines(false, false);
    byte(static_cast<uint8_t>(0xA0 | ((address >> 7) & 0x0E)));
    byte(static_cast<uint8_t>(address & 0xFF));
    byte(value);
    lines(false, false);
    lines(false, true);
    lines(true, true);
    return out;
}
}  // namespace

/// The ZX-Evo AVR EEPROM (ATM3, TS-Conf) and the SMUC EEPROM (Scorpion) are
/// engine regions: written every frame while recording,
/// each checkpoint's region holds the EEPROM as it was at that frame
TEST_F(TTDEngineModels_Fixture, EepromsAreRegionsOfEveryCheckpoint)
{
    struct Case
    {
        const char* model;
        const char* region;
    };
    for (const Case c : {Case{"ATM3", "evo-avr.eeprom"}, Case{"TSL", "evo-avr.eeprom"}, Case{"SCORPION", "smuc.eeprom"}})
    {
        SCOPED_TRACE(c.model);
        ASSERT_NO_FATAL_FAILURE(Start(c.model));
        PortDecoder* decoder = _emulator->GetContext()->pPortDecoder;
        EvoAvr* avr = nullptr;
        if (auto* atm3 = dynamic_cast<PortDecoder_ATM3*>(decoder))
            avr = &atm3->GetEvoAvr();
        if (auto* tsconf = dynamic_cast<PortDecoder_TSConf*>(decoder))
            avr = &tsconf->GetEvoAvr();
        auto* scorpion = dynamic_cast<PortDecoder_Scorpion256*>(decoder);
        ASSERT_TRUE(avr || scorpion);

        auto contents = [&]() {
            if (avr)
                return std::vector<uint8_t>(avr->EepromData(), avr->EepromData() + EvoAvr::kEepromSize);
            std::vector<uint8_t> bytes(0x800);
            for (uint16_t a = 0; a < bytes.size(); ++a)
                bytes[a] = scorpion->GetSMUCNvram().GetEEPROMByte(a);
            return bytes;
        };

        ASSERT_TRUE(_ttd->StartRecording());
        std::vector<std::vector<uint8_t>> expected = {contents()};
        for (uint8_t f = 0; f < 12; ++f)
        {
            const uint16_t address = static_cast<uint16_t>((f * 0x155) % (avr ? 0x1000 : 0x800));
            const uint8_t value = static_cast<uint8_t>(0x30 + f);
            if (avr)
            {
                avr->SetVolatileState(EvoAvr::kExtFirmwareVersion, static_cast<uint8_t>(address >> 4), 0x01);   // EEPROM mode
                avr->WriteRegister(static_cast<uint8_t>(0xF0 + (address & 0x0F)), value);
            }
            else
            {
                for (uint8_t v : SmucWriteByte(address, value))
                    scorpion->GetSMUCNvram().WriteSerialLink(v);
            }
            if (f % 3 != 2)   // some frames write nothing
                _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
            else
                _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
            expected.push_back(contents());
            if (f % 3 == 2)
                expected.insert(expected.end() - 1, expected.back());   // the frame in between saw the same contents
        }
        _ttd->StopRecording();

        ASSERT_EQ(Engine().CheckpointCount(), expected.size());
        ASSERT_NE(expected.front(), expected.back()) << "the writes changed the EEPROM";
        for (size_t i = 0; i < Engine().CheckpointCount(); ++i)
            ASSERT_TRUE(RegionAt(Engine(), i, c.region) == expected[i]) << "checkpoint " << i;

        TearDown();
    }
}
