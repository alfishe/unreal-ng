/// @file timetravelmanager_shadowmodels_test.cpp
/// @brief Shadow mode on every machine with large memory: the engine records
/// each model next to v1 and every frame it records restores exactly as v1's
/// checkpoint of the same frame (machine RAM, CPU, chipset, device state).
/// Machine RAM is the engine's region 0 on every model; this checks that the
/// one mechanism holds on 512 KB-4 MB machines and their paging schemes.
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
#include "debugger/ttd/bench/ttdv1feeder.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"
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

/// The VDAC2 memory blob's zero-run tokens expanded to the regions' bytes
/// (format: Vdac2Card::TtdSaveMemory): u32 magic, u32 length, then tokens
bool ExpandVdac2Memory(const std::vector<uint8_t>& blob, std::vector<uint8_t>& out)
{
    out.clear();
    if (blob.size() < 8)
        return false;
    size_t pos = 8;
    while (pos + 4 <= blob.size())
    {
        uint32_t token;
        std::memcpy(&token, blob.data() + pos, 4);
        pos += 4;
        const size_t length = token & 0x7FFF'FFFFu;
        if (token & 0x8000'0000u)
            out.insert(out.end(), length, 0);
        else
        {
            if (pos + length > blob.size())
                return false;
            out.insert(out.end(), blob.begin() + pos, blob.begin() + pos + length);
            pos += length;
        }
    }
    return pos == blob.size();
}

}  // namespace

class TimeTravelManager_ShadowModels_Fixture : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    ttd::TimeTravelManager* _v1 = nullptr;
    ttd::TimeTravelEngine _engine;

    void Start(const char* model, Emulator* emulator = nullptr)
    {
        _emulator = emulator ? emulator : EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << model;
        EmulatorContext* context = _emulator->GetContext();
        _v1 = context->pTimeTravelManager;
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
    }

    /// Every checkpoint the engine recorded restores as v1's of the same frame
    void CheckEveryCheckpoint()
    {
        ASSERT_EQ(_engine.CheckpointCount(), _v1->GetCheckpointCount());
        ASSERT_FALSE(_engine.Regions().empty());

        // The device table: v1's devices, restored in v1's order except where a
        // device names another (the WD1793 context after the controller)
        const ttd::TTDDeviceTable& table = _engine.Devices();
        EXPECT_EQ(table.Entries().size(), _v1->GetPeripheralRegistry().Count());
        std::vector<uint16_t> order;
        for (uint32_t i : table.RestoreOrder())
            order.push_back(static_cast<uint16_t>(table.Entries()[i].descriptor.legacyId));
        EXPECT_TRUE(std::is_sorted(order.begin(), order.end())) << "the same order as v1 (ascending ids)";
        if (table.Find({ttd::TTDDeviceType::BetaDisk, "betadisk"}))
            EXPECT_NE(table.Find({ttd::TTDDeviceType::Wd1793Context, "betadisk.context"}), nullptr);
        EXPECT_EQ(_engine.Regions()[0].pieces, _v1->GetCheckpoint(0)->ramPages.size() * 4)
            << "region 0 covers the model's whole RAM";

        // FR-19: every device that runs behind the CPU (the sound cards) was
        // at the frame boundary at every capture
        EXPECT_EQ(_engine.SyncMissCount(), 0u)
            << (_engine.SyncMisses().empty() ? std::string()
                                             : _engine.SyncMisses()[0].device.instance + " at frame " +
                                                   std::to_string(_engine.SyncMisses()[0].frame) + ", offset " +
                                                   std::to_string(_engine.SyncMisses()[0].offset));

        std::vector<uint8_t> v1Ram, v1Present, engineRam, enginePresent;
        std::string err;
        for (size_t i = 0; i < _engine.CheckpointCount(); ++i)
        {
            const ttd::TTDCheckpoint* want = _v1->GetCheckpoint(i);
            const ttd::TTDEngineCheckpoint* got = _engine.Checkpoint(i);
            ASSERT_EQ(got->position.frame, want->time.frame);
            EXPECT_EQ(std::memcmp(&got->cpu, &want->cpu, sizeof(want->cpu)), 0) << "CPU, checkpoint " << i;
            EXPECT_EQ(std::memcmp(&got->chipset, &want->chipset, sizeof(want->chipset)), 0) << "chipset, checkpoint " << i;
            // Device state: identical blobs, except a device whose memory the engine
            // keeps as a region - then v1's state = the engine's state + that region
            for (const auto& [id, blob] : want->peripheralBlobs)
            {
                std::vector<uint8_t> rebuilt;
                ASSERT_TRUE(_engine.DeviceState(i, id, rebuilt)) << "device " << int(id) << ", checkpoint " << i;
                std::vector<uint8_t> full = ttd::TTDPeripheralRegistry::DecodeBlob(id, blob);
                if (rebuilt == full)
                    continue;
                if (id == static_cast<uint8_t>(ttd::PeripheralId::Vdac2Memory))
                {
                    // Zero runs dropped in v1's blob; the engine's is the header alone
                    std::vector<uint8_t> raw;
                    ASSERT_TRUE(ExpandVdac2Memory(full, raw)) << "checkpoint " << i;
                    full.swap(raw);
                    ASSERT_EQ(rebuilt.size(), 8u) << "checkpoint " << i;
                    rebuilt.clear();
                }
                bool found = false;
                for (uint32_t r = 1; r < _engine.Regions().size(); ++r)
                    if (_engine.Regions()[r].ownerType == id && !_engine.IsDeviceStateRegion(r))
                    {
                        std::vector<uint8_t> mem(size_t(_engine.Regions()[r].pieces) * ttd::kTTDPieceSize, 0);
                        ASSERT_TRUE(_engine.RestoreRegion(i, r, mem.data()).Ok());
                        rebuilt.insert(rebuilt.end(), mem.begin(), mem.begin() + _engine.Regions()[r].bytes);
                        found = true;
                    }
                ASSERT_TRUE(found) << "device " << int(id) << " differs and owns no region, checkpoint " << i;
                ASSERT_TRUE(rebuilt == full) << "device " << int(id) << " (state + region), checkpoint " << i;
            }
            ASSERT_TRUE(ttd::bench::DecodeV1Ram(*_v1, i, v1Ram, v1Present, err)) << err;
            engineRam.assign(v1Ram.size(), 0);
            ASSERT_TRUE(_engine.RestoreRegion(i, 0, engineRam.data(), &enginePresent).Ok());
            ASSERT_EQ(enginePresent, v1Present) << "pieces known, checkpoint " << i;
            ASSERT_TRUE(engineRam == v1Ram) << "RAM differs at checkpoint " << i;
        }
}

    void TearDown() override
    {
        if (_v1)
            _v1->SetShadowEngine(nullptr);
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};

class TimeTravelManager_ShadowModels_Test : public TimeTravelManager_ShadowModels_Fixture,
                                           public ::testing::WithParamInterface<const char*>
{
};

TEST_P(TimeTravelManager_ShadowModels_Test, EveryFrameRestoresAsV1)
{
    ASSERT_NO_FATAL_FAILURE(Start(GetParam()));

    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(150, /*skipBreakpoints=*/true);
    _v1->StopRecording();
    _v1->SetShadowEngine(nullptr);

    CheckEveryCheckpoint();
}

/// The engine restores every device as v1 does (Phase 2, Step 3): v1 restores
/// a checkpoint and every device's state is saved as the reference; the machine
/// runs on; then the engine restores the same checkpoint (CPU, chipset, memory
/// regions, devices, banking) and every device saves the same bytes again
TEST_P(TimeTravelManager_ShadowModels_Test, EngineRestoresEveryDeviceAsV1)
{
    ASSERT_NO_FATAL_FAILURE(Start(GetParam()));
    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(40, /*skipBreakpoints=*/true);
    _v1->StopRecording();
    _v1->SetShadowEngine(nullptr);
    ASSERT_EQ(_engine.CheckpointCount(), _v1->GetCheckpointCount());

    EmulatorContext* context = _emulator->GetContext();
    const ttd::TTDPeripheralRegistry& registry = _v1->GetPeripheralRegistry();
    auto states = [&]() {
        std::map<uint8_t, std::vector<uint8_t>> out;
        for (const auto& [id, device] : registry.Devices())
            if (device && device->TTDStateSize() != 0)
                device->TTDSaveStateTo(out[id]);
        return out;
    };
    for (size_t i : {size_t(5), size_t(20), _engine.CheckpointCount() - 1})
    {
        SCOPED_TRACE("checkpoint " + std::to_string(i));
        ASSERT_TRUE(_v1->RestoreCheckpointForTesting(i));
        const auto reference = states();

        _emulator->RunNFrames(3, /*skipBreakpoints=*/true);   // the machine moves on

        const ttd::TTDEngineCheckpoint* cp = _engine.Checkpoint(i);
        ttd::RestoreCpuState(cp->cpu, static_cast<Z80State*>(context->pCore->GetZ80()));
        ttd::RestoreChipsetState(cp->chipset, &context->emulatorState);
        _engine.ForgetMemory();
        ASSERT_TRUE(_engine.RestoreToMemory(i).Ok());
        const ttd::TTDRestoreResult r = _engine.RestoreDevices(i, ttd::TTDRestoreContext{cp->position.frame, 0, false});
        EXPECT_EQ(r.status, ttd::TTDRestoreStatus::Exact) << r.message;
        context->pMemory->UpdateZ80Banks();

        const auto restored = states();
        for (const auto& [id, bytes] : reference)
            EXPECT_TRUE(restored.at(id) == bytes) << "device " << int(id);
    }
}

/// FR-19 with every card that runs its own clock fitted (TSFM, MoonSound, and
/// the GS or NeoGS card): at every capture each one stood at the frame
/// boundary, every frame restores as v1's, and after the engine restores a
/// checkpoint every card's clock is at that boundary again
class TimeTravelManager_ShadowCards_Test : public TimeTravelManager_ShadowModels_Fixture,
                                          public ::testing::WithParamInterface<GSTypeKind>
{
protected:
    SoundCardScope _cards{TestSound::All};
};

TEST_P(TimeTravelManager_ShadowCards_Test, CardsAreAtTheFrameBoundaryAtEveryCaptureAndRestore)
{
    Emulator* emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", TurboSoundKind::FM);
    ASSERT_NE(emulator, nullptr);
    ASSERT_TRUE(FitGeneralSoundCard(emulator->GetContext()->pSoundManager, GetParam()));
    ASSERT_NO_FATAL_FAILURE(Start("PENTAGON", emulator));
    EmulatorContext* context = _emulator->GetContext();

    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(60, /*skipBreakpoints=*/true);
    _v1->StopRecording();
    _v1->SetShadowEngine(nullptr);

    std::vector<std::string> behind;
    for (const ttd::TTDDeviceEntry& e : _engine.Devices().Entries())
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
        const ttd::TTDEngineCheckpoint* cp = _engine.Checkpoint(i);
        ttd::RestoreCpuState(cp->cpu, static_cast<Z80State*>(context->pCore->GetZ80()));
        ttd::RestoreChipsetState(cp->chipset, &context->emulatorState);
        _engine.ForgetMemory();
        ASSERT_TRUE(_engine.RestoreToMemory(i).Ok());
        const ttd::TTDRestoreResult r = _engine.RestoreDevices(i, ttd::TTDRestoreContext{cp->position.frame, 0, false});
        EXPECT_EQ(r.status, ttd::TTDRestoreStatus::Exact) << r.message;
        context->pMemory->UpdateZ80Banks();
    }
}

INSTANTIATE_TEST_SUITE_P(Cards, TimeTravelManager_ShadowCards_Test, ::testing::Values(GSTypeKind::Z80, GSTypeKind::NGS),
                         [](const ::testing::TestParamInfo<GSTypeKind>& info) {
                             return info.param == GSTypeKind::NGS ? std::string("NeoGS") : std::string("GeneralSound");
                         });

INSTANTIATE_TEST_SUITE_P(LargeMemoryModels, TimeTravelManager_ShadowModels_Test,
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
/// eve-emu's dirty bitmap reports and each checkpoint restores as v1's blob
TEST_F(TimeTravelManager_ShadowModels_Fixture, Vdac2MemoryWrittenWhileRecording)
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

    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    for (uint32_t f = 0; f < 40; ++f)
    {
        const uint8_t v = static_cast<uint8_t>(f * 37 + 1);
        write((f * 0x6A35u) % 0x100000u, {v, uint8_t(v ^ 0x5A), uint8_t(v + 1)});   // RAM_G, a page per frame
        write(0x300000u + (f % 16) * 8, {v, 0, 0, 0x26});                            // the display list
        write(0x308000u + (f % 64) * 4, {v, 1, 2, 3});                               // the command FIFO
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    }
    _v1->StopRecording();
    _v1->SetShadowEngine(nullptr);

    size_t vdac2Regions = 0;
    for (uint32_t r = 0; r < _engine.Regions().size(); ++r)
        vdac2Regions += _engine.Regions()[r].ownerType == static_cast<uint16_t>(ttd::PeripheralId::Vdac2Memory) &&
                        !_engine.IsDeviceStateRegion(r);
    ASSERT_EQ(vdac2Regions, 7u) << "RAM_G, DL0, DL1, REG, CMD, SPECIAL, INFLIGHT";
    uint32_t ramG = 0;
    while (_engine.Regions()[ramG].name != "vdac2.RAM_G")
        ++ramG;
    size_t changed = 0;
    for (size_t i = 1; i < _engine.CheckpointCount(); ++i)
        changed += _engine.ChangeCount(i, ramG);
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
/// engine regions v1 does not record: written every frame while recording,
/// each checkpoint's region holds the EEPROM as it was at that frame
TEST_F(TimeTravelManager_ShadowModels_Fixture, EepromsAreRegionsOfEveryCheckpoint)
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

        _v1->SetShadowEngine(&_engine);
        ASSERT_TRUE(_v1->StartRecording());
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
        _v1->StopRecording();
        _v1->SetShadowEngine(nullptr);

        ASSERT_EQ(_engine.CheckpointCount(), expected.size());
        ASSERT_NE(expected.front(), expected.back()) << "the writes changed the EEPROM";
        for (size_t i = 0; i < _engine.CheckpointCount(); ++i)
            ASSERT_TRUE(RegionAt(_engine, i, c.region) == expected[i]) << "checkpoint " << i;

        TearDown();
        _emulator = nullptr;
        _v1 = nullptr;
    }
}
